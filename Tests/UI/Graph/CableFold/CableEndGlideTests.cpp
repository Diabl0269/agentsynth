// A modulation cable's end glides, not jumps, when its card's More row folds or unfolds: it moves between the knob
// it lands on and the More button over the row's fade (docs/layout/cables.md, docs/layout/animation.md).
//
// Kinds: motion-over-time (a More toggle with a cable attached is a glide, not an instant jump).
//
// Cables on knobs that sit on a card's tabs (Pluck, Mallet, Wavetable): while its tab shows, each cable ends on its
// own knob with a dot beside it; while it does not, a knob-only target's cable parks at the card's left edge by the
// tab strip (each one at its own place) and a target with a gutter jack goes back to that jack; a tab switch glides
// the ends (docs/layout/cables.md, docs/layout/module-card.md#tab-strips).
//
// Kinds: sibling-gap (tabbed knobs showed no dot and their cables met at one point), motion-over-time (the switch
// glides the ends under Full, Reduce Motion and Off).

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "../CardBody/CardCanvas.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/Mallet/MalletModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/Pluck/PluckModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/CableEndGlide.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Graph/ModDot/ModDotButton.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include <gtest/gtest.h>
#include <map>

using namespace cardbody_test;
using synth::ui::AnimationMode;

namespace {

struct GlideRig {
    CardCanvas canvas;
    NodeID lfoId, oscId;
    ModuleComponent* lfo = nullptr;
    ModuleComponent* osc = nullptr;
    synth::CardBody* body = nullptr;

    // A Pan cable on a Filter whose Pan knob (knob-only, no gutter jack) sits in the More row, with the row open; or,
    // `withJack`, a Level cable on an Oscillator whose Level knob also has a gutter jack.
    explicit GlideRig(bool withJack = false) {
        FilterModule filterProbe;
        OscillatorModule oscProbe;
        lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
        oscId =
            withJack
                ? canvas.add(std::make_unique<OscillatorModule>(), 600, 0, automaticLayoutHiding(oscProbe, {"level"}))
                : canvas.add(std::make_unique<FilterModule>(), 600, 0, automaticLayoutHiding(filterProbe, {"pan"}));
        canvas.editor.updateComponents();
        lfo = canvas.card(lfoId);
        osc = canvas.card(oscId);
        body = osc->getCardBody();
        body->setMoreUnfolded(true);
        const auto knob =
            osc->localPointToGlobal(body->findWidget(withJack ? "level" : "pan")->getBounds().getCentre());
        canvas.editor.beginConnectionDrag(lfo, 0, false, false, lfo->localPointToGlobal(juce::Point<int>(0, 0)));
        canvas.editor.dragConnection(knob);
        canvas.editor.endConnectionDrag(knob);
    }

    CableEndGlide& glide() { return graph_editor_paint::CanvasMemo::of(canvas.editor)->cableEndGlide(); }

    std::optional<GraphEditor::VisibleCable> cable() {
        for (const auto& c : canvas.editor.buildVisibleCables())
            if (c.destNodeId == oscId.uid)
                return c;
        return std::nullopt;
    }
    juce::Point<float> end() { return cable().value().p2; }
};

float distance(juce::Point<float> a, juce::Point<float> b) { return a.getDistanceFrom(b); }

} // namespace

TEST(CableEndGlide, FoldingTheRowGlidesTheCableEndFromTheKnobToTheMoreButton) {
    GlideRig rig;
    ASSERT_TRUE(rig.cable().has_value());
    ASSERT_TRUE(rig.cable()->landsOnKnob);
    const auto onKnob = rig.end();

    FadeAnimateGuard guard;
    rig.body->setMoreUnfolded(false);
    ASSERT_TRUE(rig.glide().isLive());
    EXPECT_LT(distance(rig.end(), onKnob), 0.5f) << "the first frame has not moved";

    rig.glide().advanceForTest(0.5f);
    const auto mid = rig.end();
    rig.glide().completeForTest();
    const auto onButton = rig.end();

    EXPECT_GT(distance(onKnob, onButton), 5.0f) << "the knob and the More button are different places";
    EXPECT_LT(distance(mid, onKnob + (onButton - onKnob) * 0.5f), 0.5f) << "halfway through, halfway there";
    EXPECT_FALSE(rig.cable()->landsOnKnob);
    const auto button = rig.osc->getPosition().toFloat() +
                        juce::Point<float>(10.0f, (float)rig.body->getMoreButton()->getBounds().getCentreY());
    EXPECT_LT(distance(onButton, button), 0.5f) << "it ends at the left edge of the More button";
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
}

TEST(CableEndGlide, UnfoldingTheRowGlidesTheCableEndFromTheButtonToTheKnob) {
    GlideRig rig;
    rig.body->setMoreUnfolded(false);
    const auto onButton = rig.end();

    FadeAnimateGuard guard;
    rig.body->setMoreUnfolded(true);
    ASSERT_TRUE(rig.glide().isLive());
    EXPECT_LT(distance(rig.end(), onButton), 0.5f);
    rig.glide().advanceForTest(0.5f);
    const auto mid = rig.end();
    rig.glide().completeForTest();
    const auto onKnob = rig.end();
    EXPECT_TRUE(rig.cable()->landsOnKnob);
    EXPECT_GT(distance(onKnob, onButton), 5.0f);
    EXPECT_LT(distance(mid, onButton + (onKnob - onButton) * 0.5f), 0.5f);
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
}

TEST(CableEndGlide, TheGlideFollowsACardThatMovesWhileItRuns) {
    GlideRig rig;
    FadeAnimateGuard guard;
    const auto before = rig.end();
    rig.body->setMoreUnfolded(false);
    rig.osc->setTopLeftPosition(rig.osc->getX() + 40, rig.osc->getY() + 25);
    EXPECT_LT(distance(rig.end(), before + juce::Point<float>(40.0f, 25.0f)), 0.5f)
        << "the recorded point is relative to the card, so the end moves with it";
}

TEST(CableEndGlide, WithAnimationsOffTheEndLandsAtOnce) {
    GlideRig rig;
    FadeAnimateGuard guard(AnimationMode::off);
    rig.body->setMoreUnfolded(false);
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
    EXPECT_FALSE(rig.cable()->landsOnKnob);
    const auto button = rig.osc->getPosition().toFloat() +
                        juce::Point<float>(10.0f, (float)rig.body->getMoreButton()->getBounds().getCentreY());
    EXPECT_LT(distance(rig.end(), button), 0.5f);
}

TEST(CableEndGlide, ACardNotOnScreenLandsAtOnce) {
    GlideRig rig; // headless: no animate-off-screen guard
    rig.body->setMoreUnfolded(false);
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.cable()->landsOnKnob);
}

TEST(CableEndGlide, UnderReduceMotionItIsAPlainLinearMove) {
    GlideRig rig;
    FadeAnimateGuard guard(AnimationMode::reduced);
    const auto onKnob = rig.end();
    rig.body->setMoreUnfolded(false);
    ASSERT_TRUE(rig.glide().isLive());
    rig.glide().completeForTest();
    EXPECT_GT(distance(rig.end(), onKnob), 5.0f);
    EXPECT_FALSE(rig.glide().isLive());
}

// Regression test for FRO1509: a knob that also has a gutter jack kept its end on the knob for the whole fade and
// jumped to the jack when the fade ended.
TEST(CableEndGlide, AKnobWithAGutterJackGlidesToTheJackWhenTheRowFolds) {
    GlideRig rig(true);
    ASSERT_TRUE(rig.cable().has_value());
    ASSERT_TRUE(rig.cable()->landsOnKnob);
    const auto onKnob = rig.end();

    FadeAnimateGuard guard;
    rig.body->setMoreUnfolded(false);
    ASSERT_TRUE(rig.glide().isLive());
    rig.glide().advanceForTest(0.5f);
    const auto mid = rig.end();
    rig.glide().advanceForTest(0.999f);
    const auto nearEnd = rig.end();
    synth::ui::FadeVisibility::stepAllForTest(1.0f); // the row's own fade ends: the jack rejoins the gutter
    rig.glide().completeForTest();
    const auto onJack = rig.end();

    EXPECT_FALSE(rig.cable()->landsOnKnob);
    EXPECT_GT(distance(onKnob, onJack), 5.0f) << "the knob and the gutter jack are different places";
    EXPECT_LT(distance(mid, onKnob + (onJack - onKnob) * 0.5f), 0.5f) << "halfway through, halfway to the jack";
    EXPECT_LT(distance(nearEnd, onJack), 2.0f) << "it arrives at the jack, so the end of the fade is no jump";
    auto* module = dynamic_cast<ModuleBase*>(rig.osc->getModule());
    ASSERT_NE(module, nullptr);
    const auto jack = rig.osc->getPosition().toFloat() +
                      rig.osc->getPortCenter(module->mapInputChannel(5).visibleJackIndex, true).toFloat();
    EXPECT_LT(distance(onJack, jack), 0.5f) << "the end rests on the card's own gutter jack";
    EXPECT_FALSE(rig.glide().isLive());
}

namespace {

juce::AudioParameterFloat* amountParam(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))
                           : nullptr;
}

struct Wire {
    juce::String paramId;
    int channel = -1;
    bool knobOnly = false;
    NodeID atten;
};

// One card with an LFO cabled to each of `paramIds`, every LFO at its own place left of the card.
struct TabRig {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    NodeID cardId;
    std::vector<Wire> wires;
    ModuleComponent* card = nullptr;
    synth::CardBody* body = nullptr;

    TabRig(std::unique_ptr<ModuleBase> module, const std::vector<juce::String>& paramIds) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        editor->setSize(2400, 1800);
        auto& graph = engine.getGraph();
        auto* mb = module.get();
        auto node = graph.addNode(std::move(module));
        node->properties.set("x", 700);
        node->properties.set("y", 60);
        cardId = node->nodeID;
        for (int i = 0; i < (int)paramIds.size(); ++i) {
            auto lfo = graph.addNode(std::make_unique<LFOModule>());
            lfo->properties.set("x", 20);
            lfo->properties.set("y", 40 + i * 300);
            Wire wire{paramIds[(size_t)i]};
            for (const auto& t : mb->getModulationTargets())
                if (t.paramId == wire.paramId) {
                    wire.channel = t.channelIndex;
                    wire.knobOnly = t.knobOnly;
                }
            wire.atten = engine.addModRouting(lfo->nodeID, 0, cardId, wire.channel);
            if (auto* amount = amountParam(graph, wire.atten))
                amount->setValueNotifyingHost(amount->convertTo0to1(0.5f));
            wires.push_back(wire);
        }
        editor->updateComponents();
        editor->timerCallback();
        card = findModuleComp(*editor, node->getProcessor());
        body = card != nullptr ? card->getCardBody() : nullptr;
    }

    ~TabRig() {
        editor.reset();
        engine.shutdown();
    }

    CableEndGlide& glide() { return graph_editor_paint::CanvasMemo::of(*editor)->cableEndGlide(); }

    // Where each wire's cable ends on the card now, in canvas space.
    std::map<int, GraphEditor::VisibleCable> cables() {
        std::map<int, GraphEditor::VisibleCable> out;
        for (const auto& c : editor->buildVisibleCables())
            if (c.destNodeId == cardId.uid)
                out[c.destChannel] = c;
        return out;
    }
    juce::Point<float> end(const Wire& w) { return cables().at(w.channel).p2; }

    juce::Slider* knob(const Wire& w) { return dynamic_cast<juce::Slider*>(body->findWidget(w.paramId)); }

    // Shows the tab `w`'s knob is on.
    void showTabOf(const Wire& w) {
        const auto& plan = body->getPlan();
        for (const auto& item : plan.items)
            if (item.param != nullptr && item.param->paramID == w.paramId && item.section >= 0) {
                const int group = plan.sections[(size_t)item.section].tabGroup;
                ASSERT_GE(group, 0) << w.paramId << " is on no tab";
                const auto& sections = plan.tabGroups[(size_t)group].sections;
                const auto at = std::find(sections.begin(), sections.end(), item.section);
                body->selectTab(group, (int)std::distance(sections.begin(), at));
                editor->timerCallback();
                return;
            }
        FAIL() << w.paramId << " is not on the card";
    }

    bool shown(const Wire& w) { return knob(w) != nullptr && knob(w)->isVisible(); }

    float amount(const Wire& w) { return amountParam(engine.getGraph(), w.atten)->get(); }
};

std::unique_ptr<ModuleBase> makeModule(const juce::String& type) {
    if (type == "Pluck")
        return std::make_unique<PluckModule>();
    if (type == "Mallet")
        return std::make_unique<MalletModule>();
    return std::make_unique<WavetableOscillatorModule>();
}

// Each sibling with cables on knobs of several tabs, two of them on one tab, so two ends park at once. The
// Wavetable mixes its knob-only Unison with knobs that also have gutter jacks.
const std::vector<std::pair<juce::String, std::vector<juce::String>>> kSiblings = {
    {"Pluck", {"decay", "brightness", "pickPosition", "tune"}},
    {"Mallet", {"decay", "brightness", "hardness", "tune"}},
    {"Wavetable", {"unison", "detune", "fine", "phase"}},
};

// Checks every wire with the tab of `picked` shown: its own knob, dot and Tab stop when shown; parked or on its jack
// otherwise; and no two ends at one place.
void expectEndsFor(TabRig& rig, const Wire& picked, const juce::String& type) {
    SCOPED_TRACE((type + ", tab of " + picked.paramId).toStdString());
    const auto cables = rig.cables();
    const auto cardPos = rig.card->getPosition().toFloat();
    const auto stops = rig.card->getKeyboardControls();
    for (const auto& w : rig.wires) {
        SCOPED_TRACE(w.paramId.toStdString());
        ASSERT_EQ(cables.count(w.channel), 1u);
        const auto& cable = cables.at(w.channel);
        auto* button = rig.card->getModDotButton(w.channel);
        if (rig.shown(w)) {
            const auto anchor = rig.card->getModTargetKnobAnchor(w.channel);
            ASSERT_TRUE(anchor.has_value());
            EXPECT_TRUE(cable.landsOnKnob);
            EXPECT_LT(cable.p2.getDistanceFrom(cardPos + *anchor), 0.5f) << "the cable ends on its own knob";
            ASSERT_NE(button, nullptr) << "a shown knob with a cable shows its dot";
            EXPECT_TRUE(button->isVisible());
            EXPECT_LT(button->getBounds().getCentre().toFloat().getDistanceFrom(*anchor), 1.0f);
            EXPECT_TRUE(button->getTitle().isNotEmpty()) << "a screen reader names the dot";
            EXPECT_TRUE(button->getTooltip().isNotEmpty());
            const auto knobAt = std::find(stops.begin(), stops.end(), static_cast<juce::Component*>(rig.knob(w)));
            ASSERT_NE(knobAt, stops.end());
            ASSERT_NE(knobAt + 1, stops.end());
            EXPECT_EQ(*(knobAt + 1), static_cast<juce::Component*>(button)) << "Tab reaches the dot after its knob";
        } else {
            EXPECT_TRUE(button == nullptr || !button->isVisible()) << "a hidden knob shows no dot";
            EXPECT_FALSE(cable.landsOnKnob);
            if (w.knobOnly) {
                auto* strip = rig.body->getTabStrip(0);
                ASSERT_NE(strip, nullptr);
                EXPECT_NEAR(cable.p2.x, cardPos.x + 10.0f, 0.5f) << "parked at the card's left edge";
                EXPECT_GE(cable.p2.y, cardPos.y + (float)strip->getBounds().getCentreY() - 0.5f);
                EXPECT_LT(cable.p2.y, cardPos.y + (float)rig.card->getHeight()) << "inside the card";
            } else {
                auto* mb = dynamic_cast<ModuleBase*>(rig.card->getModule());
                const int jack = mb->mapInputChannel(w.channel).visibleJackIndex;
                EXPECT_LT(cable.p2.getDistanceFrom(cardPos + rig.card->getPortCenter(jack, true).toFloat()), 0.5f)
                    << "back on its own gutter jack";
            }
        }
    }
    for (const auto& a : rig.wires)
        for (const auto& b : rig.wires)
            if (a.channel < b.channel)
                EXPECT_GT(cables.at(a.channel).p2.getDistanceFrom(cables.at(b.channel).p2), 3.0f)
                    << a.paramId << " and " << b.paramId << " end at one place";
}

} // namespace

TEST(TabbedKnobCables, EveryCableEndsOnItsOwnKnobWithItsDotOnTheShownTabAndParksApartOtherwise) {
    for (const auto& [type, params] : kSiblings) {
        TabRig rig(makeModule(type), params);
        ASSERT_NE(rig.body, nullptr) << type;
        for (const auto& w : rig.wires) {
            ASSERT_GE(w.channel, 0) << type << " / " << w.paramId << " takes no cable";
            rig.showTabOf(w);
            ASSERT_TRUE(rig.shown(w));
            expectEndsFor(rig, w, type);
        }
    }
}

TEST(TabbedKnobCables, ATabSwitchKeepsTheCardSizeAndItsJackColumn) {
    for (const auto& [type, params] : kSiblings) {
        TabRig rig(makeModule(type), params);
        const auto size = rig.card->getBounds();
        const auto jack = rig.card->getPortCenter(0, true);
        for (const auto& w : rig.wires) {
            rig.showTabOf(w);
            EXPECT_EQ(rig.card->getBounds(), size) << type;
            EXPECT_EQ(rig.card->getPortCenter(0, true), jack) << type;
        }
    }
}

// A real press-drag on each shown dot moves only the amount of that dot's own cable, as one undo step.
TEST(TabbedKnobCables, DraggingADotChangesOnlyItsOwnCablesAmountInOneUndoStep) {
    for (const auto& [type, params] : kSiblings) {
        TabRig rig(makeModule(type), params);
        for (const auto& w : rig.wires) {
            SCOPED_TRACE((type + " / " + w.paramId).toStdString());
            rig.showTabOf(w);
            while (rig.undo.canUndo())
                rig.undo.undo();
            auto* knob = rig.knob(w);
            const auto anchor = rig.card->getModTargetKnobAnchor(w.channel);
            ASSERT_TRUE(anchor.has_value());
            const auto dot = knob->getLocalPoint(rig.card, anchor->roundToInt());
            std::map<int, float> before;
            for (const auto& other : rig.wires)
                before[other.channel] = rig.amount(other);
            const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            knob->mouseDown(makeModuleClickWithMods(*knob, dot, plain));
            knob->mouseDrag(makeModuleClickWithMods(*knob, dot + juce::Point<int>(0, 20), plain));
            knob->mouseUp(makeModuleClickWithMods(*knob, dot + juce::Point<int>(0, 20), plain));
            for (const auto& other : rig.wires) {
                if (other.channel == w.channel)
                    EXPECT_NEAR(rig.amount(other), before[other.channel] - 0.20f, 0.01f) << "its own amount moved";
                else
                    EXPECT_FLOAT_EQ(rig.amount(other), before[other.channel]) << other.paramId << " moved too";
            }
            ASSERT_TRUE(rig.undo.undo());
            EXPECT_NEAR(rig.amount(w), before[w.channel], 1e-4f);
            EXPECT_FALSE(rig.undo.canUndo()) << "the drag is one undo step";
        }
    }
}

// The switch from the String tab to the Tune tab: Decay's end leaves its knob for the strip, Tune's arrives on its
// knob, each from where it was drawn, halfway at half time.
TEST(TabbedKnobCables, ATabSwitchGlidesEveryCableEndUnderFullMotion) {
    for (const auto& [type, params] : kSiblings) {
        SCOPED_TRACE(type.toStdString());
        TabRig rig(makeModule(type), params);
        const auto& from = rig.wires.front();
        const auto& to = rig.wires.back();
        rig.showTabOf(from);
        std::map<int, juce::Point<float>> before;
        for (const auto& w : rig.wires)
            before[w.channel] = rig.end(w);

        FadeAnimateGuard guard;
        rig.showTabOf(to);
        ASSERT_TRUE(rig.glide().isLive());
        for (const auto& w : rig.wires)
            EXPECT_LT(rig.end(w).getDistanceFrom(before[w.channel]), 0.5f) << "the first frame has not moved";
        rig.glide().advanceForTest(0.5f);
        std::map<int, juce::Point<float>> mid;
        for (const auto& w : rig.wires)
            mid[w.channel] = rig.end(w);
        rig.glide().completeForTest();
        EXPECT_FALSE(rig.glide().isLive());
        EXPECT_FALSE(rig.glide().isDriverRunningForTest());
        for (const auto& w : rig.wires) {
            const auto after = rig.end(w);
            EXPECT_LT(mid[w.channel].getDistanceFrom(before[w.channel] + (after - before[w.channel]) * 0.5f), 0.5f)
                << w.paramId << ": halfway through, halfway there";
        }
        EXPECT_GT(before[from.channel].getDistanceFrom(rig.end(from)), 5.0f) << "the old tab's cable left its knob";
        EXPECT_GT(before[to.channel].getDistanceFrom(rig.end(to)), 5.0f) << "the new tab's cable reached its knob";
        EXPECT_TRUE(rig.cables().at(to.channel).landsOnKnob);
    }
}

TEST(TabbedKnobCables, UnderReduceMotionTheSwitchIsAPlainMove) {
    TabRig rig(makeModule("Pluck"), kSiblings.front().second);
    rig.showTabOf(rig.wires.front());
    const auto before = rig.end(rig.wires.back());
    FadeAnimateGuard guard(AnimationMode::reduced);
    rig.showTabOf(rig.wires.back());
    ASSERT_TRUE(rig.glide().isLive());
    EXPECT_LT(rig.end(rig.wires.back()).getDistanceFrom(before), 0.5f);
    rig.glide().completeForTest();
    EXPECT_GT(rig.end(rig.wires.back()).getDistanceFrom(before), 5.0f);
    EXPECT_FALSE(rig.glide().isLive());
}

TEST(TabbedKnobCables, WithAnimationsOffTheEndsLandAtOnce) {
    TabRig rig(makeModule("Pluck"), kSiblings.front().second);
    rig.showTabOf(rig.wires.front());
    FadeAnimateGuard guard(AnimationMode::off);
    rig.showTabOf(rig.wires.back());
    EXPECT_FALSE(rig.glide().isLive());
    EXPECT_FALSE(rig.glide().isDriverRunningForTest());
    const auto& tune = rig.wires.back();
    EXPECT_TRUE(rig.cables().at(tune.channel).landsOnKnob);
    EXPECT_LT(rig.end(tune).getDistanceFrom(rig.card->getPosition().toFloat() +
                                            *rig.card->getModTargetKnobAnchor(tune.channel)),
              0.5f);
}
