// MacroProjectLoadRevealTests.cpp -- a project with an open and a collapsed macro (LFO and ADSR cards inside, the
// cards whose constructors lay themselves out) comes to life without anything moving: the reveal only fades and
// scales cards in place, an open macro's cards pop together with its border, and a collapsed macro pops as one card
// (LoadRevealAnimator.h). Built the way MacroProjectLoadTests.cpp builds a load: graph and macros first, then cards.

#include "../fixtures/saved-project/SavedProjectFixture.h"
#include "MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/ADSRModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

namespace lr = synth::ui::load_reveal;

struct RevealCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<std::pair<NodeID, juce::Point<int>>> saved;

    RevealCanvas() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }
    ~RevealCanvas() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }

    NodeID add(std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        saved.emplace_back(node->nodeID, juce::Point<int>(x, y));
        return node->nodeID;
    }

    juce::String addMacro(const std::vector<NodeID>& members, bool collapsed, juce::Rectangle<int> bounds) {
        synth::Macro macro;
        macro.name = collapsed ? "Folded" : "Open";
        macro.collapsed = collapsed;
        macro.bounds = bounds;
        for (auto id : members)
            macro.members.push_back(uuidOf(engine, id));
        return editor.getMacros().add(macro);
    }

    void expectNothingMoved(const char* when) {
        for (const auto& [id, at] : saved)
            if (auto* card = findComponent(editor, id); card != nullptr && card->isVisible())
                EXPECT_EQ(card->getPosition(), at) << when << ": node " << (int)id.uid;
    }
};

MacroCardComponent* macroCard(GraphEditor& editor) {
    for (auto* child : editor.getChildren())
        for (auto* grandchild : child->getChildren())
            if (auto* card = dynamic_cast<MacroCardComponent*>(grandchild); card != nullptr && card->isVisible())
                return card;
    return nullptr;
}

} // namespace

TEST(MacroProjectLoadReveal, OpenAndCollapsedMacrosComeToLifeWithoutMoving) {
    RevealCanvas c;
    const auto lfoOpen = c.add(std::make_unique<LFOModule>(), 400, 300);
    const auto oscOpen = c.add(std::make_unique<OscillatorModule>(), 700, 300);
    const auto lfoFolded = c.add(std::make_unique<LFOModule>(), 400, 900);
    const auto adsrFolded = c.add(std::make_unique<ADSRModule>(), 700, 900);
    const auto loose = c.add(std::make_unique<OscillatorModule>(), 1300, 300);
    const auto openId = c.addMacro({lfoOpen, oscOpen}, false, {380, 280, 600, 400});
    c.addMacro({lfoFolded, adsrFolded}, true, {400, 900, 160, 60});
    c.engine.getGraph().addConnection({{lfoFolded, 0}, {oscOpen, 2}});
    c.engine.getGraph().addConnection({{oscOpen, 0}, {loose, 2}});
    c.editor.updateComponents(); // the load builds every card
    c.expectNothingMoved("after the build");
    auto* folded = macroCard(c.editor);
    ASSERT_NE(folded, nullptr);
    const auto foldedBounds = folded->getBounds();

    auto& reveal = c.editor.getLoadReveal();
    reveal.start(lr::Motion::full, {}, /*drive=*/false);
    ASSERT_TRUE(reveal.isLive());
    EXPECT_EQ(reveal.groupOfNode(lfoOpen.uid), reveal.groupOfNode(oscOpen.uid)) << "an open macro pops together";
    EXPECT_EQ(reveal.groupOfNode(lfoFolded.uid), reveal.groupOfCard(folded))
        << "a collapsed macro pops as one card, its hidden members with it";
    EXPECT_EQ(reveal.groupOfNode(adsrFolded.uid), reveal.groupOfCard(folded));
    EXPECT_LT(reveal.timeline().appearedMs(reveal.groupOfCard(folded)),
              reveal.timeline().appearedMs(reveal.groupOfNode(oscOpen.uid)))
        << "the collapsed LFO feeds the open macro, so it comes first";
    EXPECT_EQ(reveal.hullAlpha(openId), 0.0f) << "the open border starts hidden";

    for (double t = 0.0; t <= reveal.endMs(); t += 10.0) {
        reveal.applyAtMs(t);
        c.expectNothingMoved("mid-reveal");
        EXPECT_EQ(folded->getBounds(), foldedBounds);
        if (!reveal.isLive())
            break;
    }
    reveal.applyAtMs(reveal.endMs());
    EXPECT_FALSE(reveal.isLive());
    EXPECT_EQ(reveal.hullAlpha(openId), 1.0f);
    for (const auto& [id, at] : c.saved)
        if (auto* card = findComponent(c.editor, id); card != nullptr && card->isVisible()) {
            EXPECT_EQ(card->getAlpha(), 1.0f);
            EXPECT_TRUE(card->getTransform().isIdentity());
        }
    EXPECT_EQ(folded->getAlpha(), 1.0f);
    EXPECT_TRUE(folded->getTransform().isIdentity());
    c.expectNothingMoved("after the reveal");
}

TEST(MacroProjectLoadReveal, AMidRevealTeardownRestoresEveryCard) {
    RevealCanvas c;
    const auto a = c.add(std::make_unique<LFOModule>(), 400, 300);
    const auto b = c.add(std::make_unique<ADSRModule>(), 700, 300);
    c.editor.updateComponents();
    auto& reveal = c.editor.getLoadReveal();
    reveal.start(lr::Motion::full, {}, false);
    reveal.applyAtMs(60.0);
    ASSERT_TRUE(reveal.isLive());
    reveal.finish();
    for (auto id : {a, b}) {
        auto* card = findComponent(c.editor, id);
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(card->getAlpha(), 1.0f);
        EXPECT_TRUE(card->getTransform().isIdentity());
    }
}

namespace {

// A project as saved with its port widgets never docked: every port module sits at the canvas origin.
struct PortCanvas : RevealCanvas {
    struct PortRef {
        NodeID id;
        juce::String macroId; // empty for a port module no macro lists
    };
    std::vector<PortRef> ports;

    PortRef addPort(bool inlet, const juce::String& macroId) {
        std::unique_ptr<juce::AudioProcessor> processor;
        if (inlet)
            processor = std::make_unique<MacroInletModule>();
        else
            processor = std::make_unique<MacroOutletModule>();
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", 0);
        node->properties.set("y", 0);
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* macro = editor.getMacros().find(macroId)) {
            macro->members.push_back(uuid);
            synth::MacroPort port;
            port.nodeUuid = uuid;
            port.isInput = inlet;
            port.name = inlet ? "In" : "Out";
            port.order = (int)macro->ports.size();
            macro->ports.push_back(port);
        }
        ports.push_back({node->nodeID, macroId});
        return ports.back();
    }

    // Every port widget is hidden, or sits exactly where its macro's hull edge strip puts it, and none is at the
    // origin.
    void expectPortsDocked(const char* when) {
        for (const auto& port : ports) {
            auto* card = findComponent(editor, port.id);
            ASSERT_NE(card, nullptr) << when;
            if (!card->isVisible())
                continue;
            EXPECT_NE(card->getPosition(), juce::Point<int>(0, 0)) << when << ": a port widget sits at the origin";
            const auto* macro = editor.getMacros().find(port.macroId);
            ASSERT_NE(macro, nullptr) << when << ": a visible port widget with no macro";
            EXPECT_FALSE(editor.getMacros().isEffectivelyCollapsed(macro->id)) << when << ": a hidden macro's port";
            bool onStrip = false;
            for (const auto& entry : editor.getMacroController().macroHullPortLayout(macro->id))
                onStrip = onStrip || (entry.widgetBounds.getPosition() == card->getPosition());
            EXPECT_TRUE(onStrip) << when << ": a port widget off its macro's hull edge strip";
        }
    }
};

} // namespace

TEST(MacroProjectLoadReveal, PortWidgetsOfOpenFoldedNestedAndStrayMacrosNeverSitAtTheOrigin) {
    PortCanvas c;
    const auto a = c.add(std::make_unique<LFOModule>(), 400, 300);
    const auto b = c.add(std::make_unique<OscillatorModule>(), 800, 300);
    const auto f1 = c.add(std::make_unique<LFOModule>(), 400, 900);
    const auto f2 = c.add(std::make_unique<ADSRModule>(), 800, 900);
    const auto n1 = c.add(std::make_unique<LFOModule>(), 400, 1500);
    const auto n2 = c.add(std::make_unique<ADSRModule>(), 800, 1500);
    const auto openId = c.addMacro({a, b}, false, {380, 280, 600, 400});
    const auto foldedId = c.addMacro({f1, f2}, true, {400, 900, 160, 60});
    const auto childId = c.addMacro({n1, n2}, false, {380, 1480, 600, 400});
    const auto p1 = c.add(std::make_unique<OscillatorModule>(), 1300, 1500);
    const auto parentId = c.addMacro({p1}, true, {380, 1480, 160, 60});
    ASSERT_TRUE(c.editor.getMacros().setParent(childId, parentId));
    for (const auto& id : {openId, foldedId, childId}) {
        c.addPort(true, id);
        c.addPort(false, id);
    }
    c.addPort(true, {});         // an inlet no macro lists
    c.addPort(false, {});        // and an outlet
    c.editor.updateComponents(); // the load builds every card, then docks the ports
    c.expectPortsDocked("after the build");

    auto& reveal = c.editor.getLoadReveal();
    reveal.start(lr::Motion::full, {}, /*drive=*/false);
    ASSERT_TRUE(reveal.isLive());
    c.expectPortsDocked("reveal start");
    for (double t = 0.0; t <= reveal.endMs(); t += 10.0) {
        reveal.applyAtMs(t);
        c.expectPortsDocked("mid-reveal");
        if (!reveal.isLive())
            break;
    }
    reveal.applyAtMs(reveal.endMs());
    c.expectPortsDocked("after the reveal");

    // Opening the folded macro and folding the open ones again moves the ports with their macros.
    c.editor.getMacros().find(foldedId)->collapsed = false;
    c.editor.getMacros().find(openId)->collapsed = true;
    c.editor.getMacros().find(parentId)->collapsed = false;
    c.editor.updateComponents();
    c.expectPortsDocked("after opening the folded and nested macros");
    c.editor.getMacros().find(parentId)->collapsed = true;
    c.editor.updateComponents();
    c.expectPortsDocked("after folding the parent");
}

// A saved project whose port widgets were never docked, opened the way the app opens a file: no port widget sits at
// the canvas origin, and the ones shown are on their macro's hull edge strip.
TEST(MacroProjectLoadReveal, ASavedProjectWithUndockedPortWidgetsOpensWithNoneAtTheOrigin) {
    synth::test::SavedProjectFixture project(/*withPortWidgets=*/true);
    ASSERT_TRUE(project.wasSaved());
    auto mc = project.open();
    ASSERT_NE(mc, nullptr);
    auto& editor = mc->getGraphEditor();
    int portCards = 0;
    for (auto* node : mc->getAudioEngine().getGraph().getNodes()) {
        auto* processor = node->getProcessor();
        if (dynamic_cast<MacroInletModule*>(processor) == nullptr &&
            dynamic_cast<MacroOutletModule*>(processor) == nullptr)
            continue;
        ++portCards;
        auto* card = findComponent(editor, node->nodeID);
        ASSERT_NE(card, nullptr);
        if (!card->isVisible())
            continue;
        EXPECT_NE(card->getPosition(), juce::Point<int>(0, 0)) << "a port widget sits at the origin";
        const auto uuid = node->properties["uuid"].toString();
        const auto* macro = editor.getMacros().findByMember(uuid);
        ASSERT_NE(macro, nullptr);
        bool onStrip = false;
        for (const auto& entry : editor.getMacroController().macroHullPortLayout(macro->id))
            onStrip = onStrip || entry.widgetBounds.getPosition() == card->getPosition();
        EXPECT_TRUE(onStrip) << "a port widget off its macro's hull edge strip";
    }
    const auto* audio1 = editor.getMacros().find(synth::test::SavedProjectFixture::trackMacroId(*mc, 0));
    ASSERT_NE(audio1, nullptr);
    EXPECT_TRUE(audio1->memberIsPort(project.wiredStrayInletUuid()))
        << "a stray inlet cabled into one macro is that macro's port again after the open";
    EXPECT_GE(portCards, 9) << "the fixture's own ports and the added ones were all checked";
}

// A port module no macro lists is hidden, so no cable is drawn to it either: a cable that ends where nothing is shown
// would run to the edge of the canvas.
TEST(MacroProjectLoadReveal, ACablesEndOnAHiddenStrayPortIsNotDrawn) {
    PortCanvas c;
    const auto a = c.add(std::make_unique<LFOModule>(), 400, 300);
    const auto b = c.add(std::make_unique<OscillatorModule>(), 800, 300);
    const auto loose = c.add(std::make_unique<OscillatorModule>(), 1300, 300);
    c.addMacro({a, b}, false, {380, 280, 600, 400});
    const auto stray = c.addPort(true, {});
    c.engine.getGraph().addConnection({{stray.id, 0}, {b, 2}});
    c.engine.getGraph().addConnection({{loose, 0}, {b, 2}});
    c.editor.updateComponents();
    ASSERT_FALSE(findComponent(c.editor, stray.id)->isVisible());
    int drawn = 0;
    for (const auto& cable : c.editor.buildVisibleCables()) {
        EXPECT_NE(cable.id.srcUid, stray.id.uid);
        EXPECT_NE(cable.id.dstUid, stray.id.uid);
        ++drawn;
    }
    EXPECT_GE(drawn, 1) << "the cable of an ordinary card is still drawn";
}
