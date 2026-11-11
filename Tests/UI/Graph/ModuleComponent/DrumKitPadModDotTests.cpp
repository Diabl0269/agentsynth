// DrumKitPadModDotTests.cpp
// The mod dot on a Drum Kit pad knob is a real control: a press-drag on it changes the amount of the picked pad's own
// cable, as one undo step. A cable whose pad is not picked parks at the card's left edge by the pad panel, apart from
// any other, and picking a pad glides the ends between the knob and that place under Full, Reduce Motion and Off.
//
// Kinds: dead-control (the dot painted but no press reached a gesture), motion-over-time (the pick glides the ends).

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../CardBody/CardCanvas.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/GraphEditor/CableEndGlide.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Graph/ModDot/ModDotButton.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include <map>

using namespace cardbody_test;
using synth::ui::AnimationMode;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;
using PadParam = DrumKitModule::PadParam;

namespace {

juce::AudioParameterFloat* amountParam(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))
                           : nullptr;
}

// An LFO on the Kick's Level and one on the Snare's Level, each through its own attenuverter at 0.5.
struct PadDotScene {
    CardCanvas canvas;
    NodeID kitId;
    std::map<int, NodeID> atten; // pad index -> its cable's attenuverter
    std::map<int, int> channel;  // pad index -> its Level's slot channel

    PadDotScene() {
        kitId = canvas.add(std::make_unique<DrumKitModule>(), 700, 0);
        for (int pad : {0, 1}) {
            const auto lfo = canvas.add(synth::AIStateMapper::createModule("LFO"), 0, pad * 300);
            channel[pad] = kit().bindModSlot("pad" + juce::String(pad + 1) + "Level");
            atten[pad] = canvas.engine.addModRouting(lfo, 0, kitId, channel[pad]);
            auto* amount = amountParam(canvas.engine.getGraph(), atten[pad]);
            amount->setValueNotifyingHost(amount->convertTo0to1(0.5f));
        }
        refresh();
    }
    void refresh() {
        canvas.editor.updateComponents();
        canvas.editor.timerCallback();
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(kitId)); }
    ModuleComponent& card() { return *canvas.card(kitId); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }
    juce::Slider& levelKnob() { return pads().controls().knob(PadParam::Level); }
    CableEndGlide& glide() { return graph_editor_paint::CanvasMemo::of(canvas.editor)->cableEndGlide(); }
    float amount(int pad) { return amountParam(canvas.engine.getGraph(), atten[pad])->get(); }

    void pick(int pad) {
        auto& grid = pads().grid();
        grid.mouseDown(makeModuleClickWithMods(grid, grid.padBounds(pad).getCentre(),
                                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
        refresh();
    }
    std::map<int, GraphEditor::VisibleCable> cables() {
        std::map<int, GraphEditor::VisibleCable> out;
        for (const auto& c : canvas.editor.buildVisibleCables())
            if (c.destNodeId == kitId.uid)
                out[c.destChannel] = c;
        return out;
    }
    juce::Point<float> end(int pad) { return cables().at(channel[pad]).p2; }
    juce::Point<float> knobEnd(int pad) {
        return card().getPosition().toFloat() + *card().getModTargetKnobAnchor(channel[pad]);
    }
};

// Pad `shown` is picked: its cable ends on the Level knob with a dot, the other pad's parks by the pad panel.
void expectPicked(PadDotScene& scene, int shown) {
    const int other = 1 - shown;
    const auto cables = scene.cables();
    ASSERT_EQ(cables.count(scene.channel[shown]), 1u);
    ASSERT_EQ(cables.count(scene.channel[other]), 1u);
    ASSERT_TRUE(scene.card().getModTargetKnobAnchor(scene.channel[shown]).has_value());
    EXPECT_TRUE(cables.at(scene.channel[shown]).landsOnKnob);
    EXPECT_LT(scene.end(shown).getDistanceFrom(scene.knobEnd(shown)), 0.5f);
    auto* dot = scene.card().getModDotButton(scene.channel[shown]);
    ASSERT_NE(dot, nullptr);
    EXPECT_TRUE(dot->isVisible());
    EXPECT_TRUE(dot->getTitle().isNotEmpty());
    EXPECT_TRUE(dot->getTooltip().isNotEmpty());

    EXPECT_FALSE(scene.card().getModTargetKnobAnchor(scene.channel[other]).has_value());
    EXPECT_FALSE(cables.at(scene.channel[other]).landsOnKnob);
    const auto cardPos = scene.card().getPosition().toFloat();
    const auto panelTop = scene.card().getLocalPoint(&scene.pads().controls(), juce::Point<int>()).y;
    EXPECT_NEAR(scene.end(other).x, cardPos.x + 10.0f, 0.5f) << "parked at the card's left edge";
    EXPECT_GE(scene.end(other).y, cardPos.y + (float)panelTop - 0.5f) << "by the pad panel";
    EXPECT_GT(scene.end(other).getDistanceFrom(scene.end(shown)), 3.0f);
}

} // namespace

TEST(DrumKitPadModDot, ThePickedPadsCableEndsOnItsKnobAndTheOtherParksByThePanel) {
    PadDotScene scene;
    scene.pick(1);
    ASSERT_EQ(scene.pads().controls().boundPad(), 1);
    expectPicked(scene, 1);
    scene.pick(0);
    expectPicked(scene, 0);
}

// Regression test for FRO1677: a press-drag on a pad knob's mod dot did nothing.
TEST(DrumKitPadModDot, DraggingThePadDotChangesOnlyThatPadsCableInOneUndoStep) {
    PadDotScene scene;
    for (int pad : {1, 0}) {
        SCOPED_TRACE(pad);
        scene.pick(pad);
        while (scene.canvas.undo.canUndo())
            scene.canvas.undo.undo();
        scene.refresh();
        auto& knob = scene.levelKnob();
        const auto dot =
            knob.getLocalPoint(&scene.card(), scene.card().getModTargetKnobAnchor(scene.channel[pad])->roundToInt());
        const float before = scene.amount(pad), otherBefore = scene.amount(1 - pad);
        const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
        knob.mouseDown(makeModuleClickWithMods(knob, dot, plain));
        knob.mouseDrag(makeModuleClickWithMods(knob, dot + juce::Point<int>(0, 20), plain));
        knob.mouseUp(makeModuleClickWithMods(knob, dot + juce::Point<int>(0, 20), plain));
        EXPECT_NEAR(scene.amount(pad), before - 0.20f, 0.01f) << "the picked pad's own amount moved";
        EXPECT_FLOAT_EQ(scene.amount(1 - pad), otherBefore) << "the other pad's cable did not";
        ASSERT_TRUE(scene.canvas.undo.undo());
        EXPECT_NEAR(scene.amount(pad), before, 1e-4f);
        EXPECT_FALSE(scene.canvas.undo.canUndo()) << "the drag is one undo step";
    }
}

TEST(DrumKitPadModDot, PickingAPadGlidesTheCableEndsUnderFullMotion) {
    PadDotScene scene;
    scene.pick(1);
    const auto snareFrom = scene.end(1), kickFrom = scene.end(0);
    FadeAnimateGuard guard;
    scene.pick(0);
    EXPECT_FALSE(scene.glide().isLive()) << "the ends wait while the old pad's knobs fade out";
    scene.pads().controls().completeFadeForTest(); // the fade-out ends and binds the kick
    scene.refresh();
    ASSERT_TRUE(scene.glide().isLive());
    EXPECT_LT(scene.end(1).getDistanceFrom(snareFrom), 0.5f) << "the first frame has not moved";
    scene.glide().advanceForTest(0.5f);
    const auto snareMid = scene.end(1), kickMid = scene.end(0);
    scene.glide().completeForTest();
    scene.pads().controls().completeFadeForTest();
    EXPECT_FALSE(scene.glide().isLive());
    EXPECT_LT(snareMid.getDistanceFrom(snareFrom + (scene.end(1) - snareFrom) * 0.5f), 0.5f);
    EXPECT_LT(kickMid.getDistanceFrom(kickFrom + (scene.end(0) - kickFrom) * 0.5f), 0.5f);
    expectPicked(scene, 0);
}

TEST(DrumKitPadModDot, UnderReduceMotionThePickIsAPlainMove) {
    PadDotScene scene;
    scene.pick(1);
    const auto kickFrom = scene.end(0);
    FadeAnimateGuard guard(AnimationMode::reduced);
    scene.pick(0);
    ASSERT_TRUE(scene.glide().isLive());
    EXPECT_LT(scene.end(0).getDistanceFrom(kickFrom), 0.5f);
    scene.glide().completeForTest();
    scene.pads().controls().completeFadeForTest();
    EXPECT_FALSE(scene.glide().isLive());
    expectPicked(scene, 0);
}

TEST(DrumKitPadModDot, WithAnimationsOffTheEndsLandAtOnce) {
    PadDotScene scene;
    scene.pick(1);
    FadeAnimateGuard guard(AnimationMode::off);
    scene.pick(0);
    EXPECT_FALSE(scene.glide().isLive());
    EXPECT_FALSE(scene.glide().isDriverRunningForTest());
    expectPicked(scene, 0);
}
