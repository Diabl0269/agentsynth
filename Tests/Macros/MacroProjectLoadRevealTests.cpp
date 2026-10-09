// MacroProjectLoadRevealTests.cpp -- a project with an open and a collapsed macro (LFO and ADSR cards inside, the
// cards whose constructors lay themselves out) comes to life without anything moving: the reveal only fades and
// scales cards in place, an open macro's cards pop together with its border, and a collapsed macro pops as one card
// (LoadRevealAnimator.h). Built the way MacroProjectLoadTests.cpp builds a load: graph and macros first, then cards.

#include "MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/ADSRModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
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
