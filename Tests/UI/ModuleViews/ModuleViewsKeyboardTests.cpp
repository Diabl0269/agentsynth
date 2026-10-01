// ModuleViewsKeyboardTests.cpp
// The module views reached from the keyboard and a screen reader: the spoken-text builders, the EQ
// curve's band keys, the curve editor's point keys (including that one key press is one undo gesture),
// the threshold control's slider, and the names and tooltips of the read-only visualizers.

#include "CurveEditor/CurveEditorTestHelpers.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FilterModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/VisualBuffer.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include "UI/ModuleViews/SampleWaveformComponent.h"
#include "UI/ModuleViews/ScopeComponent.h"
#include "UI/ModuleViews/ThresholdControlComponent.h"
#include "UI/ModuleViews/WavetableDisplayComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace synth::ui;

namespace {

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

juce::String spoken(juce::Component& view) {
    auto handler = view.createAccessibilityHandler();
    EXPECT_NE(handler, nullptr);
    auto* value = handler != nullptr ? handler->getValueInterface() : nullptr;
    EXPECT_NE(value, nullptr);
    return value != nullptr ? value->getCurrentValueAsString() : juce::String();
}

// ---------------------------------------------------------------------------
// EQ curve
// ---------------------------------------------------------------------------

struct EqRig {
    ParametricEQModule eq;
    std::unique_ptr<EQCurveComponent> curve;
    int starts = 0;
    int ends = 0;

    EqRig() {
        eq.setBandEnabled(0, true);
        eq.setBandFreq(0, 1000.0f);
        eq.setBandGain(0, 0.0f);
        eq.setBandEnabled(1, true);
        eq.setBandFreq(1, 2000.0f);
        eq.setBandGain(1, 3.0f);
        curve = std::make_unique<EQCurveComponent>(eq);
        curve->setBounds(0, 0, 400, 180);
        curve->onGestureStart = [this] { ++starts; };
        curve->onGestureEnd = [this] { ++ends; };
    }
    ParametricEQModule::BandSnapshot band(int b) const { return eq.getBandSnapshots()[(size_t)b]; }
    bool press(int code, int mods = 0) { return curve->keyPressed(key(code, mods)); }
};

} // namespace

TEST(ModuleViewsTextTest, FrequencySpeaksHertzBelowAKilohertzAndKilohertzAbove) {
    EXPECT_EQ(describeFrequency(850.0f), "850 Hz");
    EXPECT_EQ(describeFrequency(1200.0f), "1.2 kHz");
    EXPECT_EQ(describeFrequency(1000.0f), "1 kHz");
    EXPECT_EQ(describeFrequency(20000.0f), "20 kHz");
}

TEST(ModuleViewsTextTest, GainAlwaysCarriesASign) {
    EXPECT_EQ(describeGainDb(3.0f), "+3.0 dB");
    EXPECT_EQ(describeGainDb(-2.54f), "-2.5 dB");
    EXPECT_EQ(describeGainDb(0.0f), "+0.0 dB");
    EXPECT_EQ(describeGainDb(-0.04f), "+0.0 dB");
}

TEST(ModuleViewsTextTest, EqBandReadsFrequencyGainAndQ) {
    EXPECT_EQ(describeEqBand(1, true, 1200.0f, 3.0f, 0.7071f), "Band 2, 1.2 kHz, +3.0 dB, Q 0.7");
    EXPECT_EQ(describeEqBand(3, false, 8000.0f, 0.0f, 1.0f), "Band 4, off");
    EXPECT_EQ(describeEqCurveSummary(2, 4), "No band selected, 2 of 4 bands on");
}

TEST(ModuleViewsTextTest, CurvePointReadsPositionTimeAndLevel) {
    EXPECT_EQ(describeCurvePoint(2, 5, 0.25, 0.8f), "Point 3 of 5, time 0.25, level 0.80");
    EXPECT_EQ(describeCurvePoint(0, 5, 0.0, 0.0f), "Point 1 of 5, time 0, level 0.00");
    EXPECT_EQ(describeCurvePoint(1, 5, 0.005, 1.0f), "Point 2 of 5, time 0.005, level 1.00");
    EXPECT_EQ(describeCurveSummary(5), "Curve with 5 points");
    EXPECT_EQ(describeCurveSummary(1), "Curve with 1 point");
}

TEST(EQCurveKeyboard, IsATabStopWithANameAndATooltip) {
    EqRig rig;
    EXPECT_TRUE(rig.curve->getWantsKeyboardFocus());
    EXPECT_TRUE(rig.curve->getTitle().isNotEmpty());
    EXPECT_TRUE(rig.curve->getTooltip().isNotEmpty());
}

TEST(EQCurveKeyboard, LeftAndRightSelectBandsAndClampAtTheEnds) {
    EqRig rig;
    EXPECT_EQ(rig.curve->getSelectedBand(), -1);
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey));
    EXPECT_EQ(rig.curve->getSelectedBand(), 0);
    for (int expected : {1, 2, 3, 3, 3}) {
        rig.press(juce::KeyPress::rightKey);
        EXPECT_EQ(rig.curve->getSelectedBand(), expected);
    }
    for (int expected : {2, 1, 0, 0}) {
        rig.press(juce::KeyPress::leftKey);
        EXPECT_EQ(rig.curve->getSelectedBand(), expected);
    }
    EXPECT_EQ(rig.starts, 0) << "moving the selection edits nothing, so it records no undo step";
}

TEST(EQCurveKeyboard, TabbingInSelectsTheFirstBandButAClickDoesNot) {
    EqRig rig;
    rig.curve->focusGained(juce::Component::focusChangedByMouseClick);
    EXPECT_EQ(rig.curve->getSelectedBand(), -1);
    rig.curve->focusGained(juce::Component::focusChangedByTabKey);
    EXPECT_EQ(rig.curve->getSelectedBand(), 0);
}

TEST(EQCurveKeyboard, UpAndDownChangeGainOneDbPerPressAsOneGesture) {
    EqRig rig;
    rig.press(juce::KeyPress::rightKey); // band 1
    EXPECT_TRUE(rig.press(juce::KeyPress::upKey));
    EXPECT_NEAR(rig.band(0).gainDb, 1.0f, 0.01f);
    EXPECT_EQ(rig.starts, 1);
    EXPECT_EQ(rig.ends, 1);
    rig.press(juce::KeyPress::downKey);
    rig.press(juce::KeyPress::downKey);
    EXPECT_NEAR(rig.band(0).gainDb, -1.0f, 0.01f);
    EXPECT_EQ(rig.starts, 3);
    EXPECT_EQ(rig.ends, 3);
    rig.press(juce::KeyPress::upKey, kShift);
    EXPECT_NEAR(rig.band(0).gainDb, -0.75f, 0.01f);
}

TEST(EQCurveKeyboard, GainStopsAtTheRangeEndWithoutAnEmptyUndoStep) {
    EqRig rig;
    rig.eq.setBandGain(0, ParametricEQModule::kMaxGainDb);
    EQCurveComponent curve(rig.eq);
    int starts = 0;
    curve.onGestureStart = [&] { ++starts; };
    curve.keyPressed(key(juce::KeyPress::rightKey));
    curve.keyPressed(key(juce::KeyPress::upKey));
    EXPECT_NEAR(rig.band(0).gainDb, ParametricEQModule::kMaxGainDb, 0.01f);
    EXPECT_EQ(starts, 0);
}

TEST(EQCurveKeyboard, AltLeftAndRightMoveFrequencyBySemitones) {
    EqRig rig;
    rig.press(juce::KeyPress::rightKey);
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kAlt));
    EXPECT_NEAR(rig.band(0).freqHz, 1000.0f * std::pow(2.0f, 1.0f / 12.0f), 0.5f);
    rig.press(juce::KeyPress::leftKey, kAlt);
    EXPECT_NEAR(rig.band(0).freqHz, 1000.0f, 0.5f);
    EXPECT_EQ(rig.curve->getSelectedBand(), 0) << "Alt+arrows edit, they do not move the selection";
    EXPECT_EQ(rig.starts, 2);
    EXPECT_EQ(rig.ends, 2);
}

TEST(EQCurveKeyboard, ReturnSwitchesTheSelectedBandOnAndOffAndKeepsItSelected) {
    EqRig rig;
    for (int i = 0; i < 3; ++i)
        rig.press(juce::KeyPress::rightKey); // band 3, which is off
    ASSERT_EQ(rig.curve->getSelectedBand(), 2);
    ASSERT_FALSE(rig.eq.isBandEnabled(2));
    EXPECT_TRUE(rig.press(juce::KeyPress::returnKey));
    EXPECT_TRUE(rig.eq.isBandEnabled(2));
    EXPECT_EQ(rig.curve->getSelectedBand(), 2);
    rig.press(juce::KeyPress::returnKey);
    EXPECT_FALSE(rig.eq.isBandEnabled(2));
    EXPECT_EQ(rig.curve->getSelectedBand(), 2);
    EXPECT_EQ(rig.starts, 2);
    EXPECT_EQ(rig.ends, 2);
}

TEST(EQCurveKeyboard, ABandThatIsOffIsNotEditedByTheArrows) {
    EqRig rig;
    for (int i = 0; i < 3; ++i)
        rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::upKey);
    EXPECT_NEAR(rig.band(2).gainDb, 0.0f, 0.01f);
    EXPECT_EQ(rig.curve->getSelectedBand(), 2) << "the selection stays on the band that is off";
    EXPECT_EQ(rig.starts, 0);
}

TEST(EQCurveKeyboard, DigitsSelectAndSwitchOnAndDeleteSwitchesOff) {
    EqRig rig;
    EXPECT_TRUE(rig.press('4'));
    EXPECT_TRUE(rig.eq.isBandEnabled(3));
    EXPECT_EQ(rig.curve->getSelectedBand(), 3);
    rig.press(juce::KeyPress::deleteKey);
    EXPECT_FALSE(rig.eq.isBandEnabled(3));
    EXPECT_EQ(rig.curve->getSelectedBand(), 3);
}

TEST(EQCurveKeyboard, PageKeysChangeQ) {
    EqRig rig;
    rig.press(juce::KeyPress::rightKey);
    const float before = rig.band(0).q;
    rig.press(juce::KeyPress::pageUpKey);
    EXPECT_GT(rig.band(0).q, before);
    rig.press(juce::KeyPress::pageDownKey);
    EXPECT_NEAR(rig.band(0).q, before, 0.001f);
}

TEST(EQCurveKeyboard, CommandAndControlChordsAreLeftToTheApp) {
    EqRig rig;
    EXPECT_FALSE(rig.press(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier));
    EXPECT_FALSE(rig.press(juce::KeyPress::rightKey, juce::ModifierKeys::ctrlModifier));
}

TEST(EQCurveKeyboard, TheSpokenValueFollowsTheSelectedBand) {
    EqRig rig;
    EXPECT_EQ(spoken(*rig.curve), "No band selected, 2 of 4 bands on");
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    EXPECT_EQ(spoken(*rig.curve), "Band 2, 2 kHz, +3.0 dB, Q 0.7");
    rig.press(juce::KeyPress::upKey);
    EXPECT_EQ(spoken(*rig.curve), "Band 2, 2 kHz, +4.0 dB, Q 0.7");
    rig.press(juce::KeyPress::rightKey);
    EXPECT_EQ(spoken(*rig.curve), "Band 3, off");
}

// ---------------------------------------------------------------------------
// Curve editor
// ---------------------------------------------------------------------------

namespace {

struct CurveRig {
    CurveEditorComponent editor;
    std::vector<juce::String> events;

    explicit CurveRig(CurveModel model) {
        editor.setBounds(0, 0, 300, 120);
        editor.setModel(std::move(model));
        editor.onGestureStart = [this] { events.push_back("start"); };
        editor.onGestureEnd = [this] { events.push_back("end"); };
        editor.onNodeChanged = [this](int) { events.push_back("node"); };
        editor.onPointsChanged = [this] { events.push_back("points"); };
    }
    bool press(int code, int mods = 0) { return editor.keyPressed(key(code, mods)); }
    const CurveNode& node(int i) const { return editor.getModel().getNode(i); }
};

CurveModel freeModel(int interiorPoints) {
    CurveModel model(CurveMode::Free);
    std::vector<CurveNode> nodes;
    nodes.push_back(CurveNode{0.0, 0.5f, false, true});
    for (int i = 1; i <= interiorPoints; ++i)
        nodes.push_back(CurveNode{(double)i / (double)(interiorPoints + 1), 0.5f, true, true});
    nodes.push_back(CurveNode{1.0, 0.5f, false, true});
    model.setNodes(nodes);
    return model;
}

} // namespace

TEST(CurveEditorKeyboard, IsATabStopWithANameAndATooltip) {
    CurveRig rig(test::buildEnvelopeModel());
    EXPECT_TRUE(rig.editor.getWantsKeyboardFocus());
    EXPECT_TRUE(rig.editor.getTitle().isNotEmpty());
    EXPECT_TRUE(rig.editor.getTooltip().isNotEmpty());
}

TEST(CurveEditorKeyboard, LeftAndRightSelectMovablePointsAndClampAtTheEnds) {
    CurveRig rig(test::buildEnvelopeModel());
    EXPECT_EQ(rig.editor.getSelectedIndex(), -1);
    rig.press(juce::KeyPress::rightKey);
    EXPECT_EQ(rig.editor.getSelectedIndex(), 1) << "the pinned origin is skipped";
    for (int expected : {2, 3, 4, 4}) {
        EXPECT_TRUE(rig.press(juce::KeyPress::rightKey));
        EXPECT_EQ(rig.editor.getSelectedIndex(), expected);
    }
    for (int expected : {3, 2, 1, 1}) {
        rig.press(juce::KeyPress::leftKey);
        EXPECT_EQ(rig.editor.getSelectedIndex(), expected);
    }
    EXPECT_TRUE(rig.events.empty()) << "selecting edits nothing";
}

TEST(CurveEditorKeyboard, AltRightMovesThePointInTimeAsOneGestureAroundTheChange) {
    CurveRig rig(test::buildEnvelopeModel());
    for (int i = 0; i < 3; ++i)
        rig.press(juce::KeyPress::rightKey); // the sustain point, index 3
    const double before = rig.node(3).x;
    EXPECT_TRUE(rig.press(juce::KeyPress::rightKey, kAlt));
    EXPECT_GT(rig.node(3).x, before);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "node", "end"}));
    const double moved = rig.node(3).x;
    rig.events.clear();
    rig.press(juce::KeyPress::leftKey, kAlt);
    EXPECT_LT(rig.node(3).x, moved);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "node", "end"}));
    EXPECT_EQ(rig.editor.getSelectedIndex(), 3);
}

TEST(CurveEditorKeyboard, AltUpAndDownMoveTheLevelAndClampAtTheRangeEnds) {
    CurveRig rig(test::buildEnvelopeModel());
    for (int i = 0; i < 3; ++i)
        rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::upKey, kAlt);
    EXPECT_NEAR(rig.node(3).y, 0.45f, 1e-5f);
    for (int i = 0; i < 20; ++i)
        rig.press(juce::KeyPress::upKey, kAlt);
    EXPECT_FLOAT_EQ(rig.node(3).y, 1.0f);
    rig.events.clear();
    rig.press(juce::KeyPress::upKey, kAlt);
    EXPECT_TRUE(rig.events.empty()) << "a press that changes nothing records no undo step";
    rig.press(juce::KeyPress::downKey, kAlt | kShift);
    EXPECT_NEAR(rig.node(3).y, 0.99f, 1e-5f);
}

TEST(CurveEditorKeyboard, AnAxisThatIsPinnedDoesNotMove) {
    CurveRig rig(test::buildEnvelopeModel());
    rig.press(juce::KeyPress::rightKey); // the attack peak: x movable, level pinned at 1
    rig.press(juce::KeyPress::upKey, kAlt);
    rig.press(juce::KeyPress::downKey, kAlt);
    EXPECT_FLOAT_EQ(rig.node(1).y, 1.0f);
    EXPECT_TRUE(rig.events.empty());
}

TEST(CurveEditorKeyboard, ASnappingGridMovesOneCellPerPressAndShiftBypassesIt) {
    CurveRig rig(freeModel(1));
    rig.editor.setVisibleRangeOverride(1.0);
    rig.editor.setGrid(CurveEditorComponent::CurveGrid{4, 4});
    rig.editor.setSnapToGrid(true);
    rig.press(juce::KeyPress::rightKey); // the pinned start still has a movable level, so it is selectable
    rig.press(juce::KeyPress::rightKey); // the middle point, x 0.5
    ASSERT_EQ(rig.editor.getSelectedIndex(), 1);
    rig.press(juce::KeyPress::rightKey, kAlt);
    EXPECT_NEAR(rig.node(1).x, 0.75, 1e-9);
    rig.press(juce::KeyPress::upKey, kAlt);
    EXPECT_NEAR(rig.node(1).y, 0.75f, 1e-5f);
    rig.press(juce::KeyPress::rightKey, kAlt | kShift);
    EXPECT_NEAR(rig.node(1).x, 0.75 + 0.25 / 5.0, 1e-9) << "Shift moves a fifth of a cell, unsnapped";
}

TEST(CurveEditorKeyboard, DeleteRemovesTheSelectedPointInFreeModeOnly) {
    CurveRig rig(freeModel(2));
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey); // index 1
    ASSERT_EQ(rig.editor.getModel().getNumNodes(), 4);
    EXPECT_TRUE(rig.press(juce::KeyPress::deleteKey));
    EXPECT_EQ(rig.editor.getModel().getNumNodes(), 3);
    EXPECT_EQ(rig.editor.getSelectedIndex(), -1);
    EXPECT_EQ(rig.events, (std::vector<juce::String>{"start", "points", "end"}));

    CurveRig fixed(test::buildEnvelopeModel());
    EXPECT_FALSE(fixed.press(juce::KeyPress::deleteKey)) << "a Fixed curve leaves Delete to the app";
}

TEST(CurveEditorKeyboard, TheSpokenValueFollowsTheSelectedPoint) {
    CurveRig rig(freeModel(3));
    EXPECT_EQ(spoken(rig.editor), "Curve with 5 points");
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey);
    EXPECT_EQ(spoken(rig.editor), "Point 3 of 5, time 0.5, level 0.50");
    rig.press(juce::KeyPress::upKey, kAlt);
    EXPECT_EQ(spoken(rig.editor), "Point 3 of 5, time 0.5, level 0.55");
}

TEST(CurveEditorKeyboard, TabbingInSelectsAPointButAClickDoesNot) {
    CurveRig rig(test::buildEnvelopeModel());
    rig.editor.focusGained(juce::Component::focusChangedByMouseClick);
    EXPECT_EQ(rig.editor.getSelectedIndex(), -1);
    rig.editor.focusGained(juce::Component::focusChangedByTabKey);
    EXPECT_EQ(rig.editor.getSelectedIndex(), 1);
}

TEST(CurveEditorKeyboard, ADragThatReordersAFreePointKeepsItSelected) {
    CurveRig rig(freeModel(2));
    rig.press(juce::KeyPress::rightKey);
    rig.press(juce::KeyPress::rightKey); // index 1, x 1/3
    const int moved = rig.editor.dragNodeTo(1, rig.editor.getLocalBounds().getBottomRight().toFloat());
    EXPECT_EQ(rig.editor.getSelectedIndex(), moved);
}

// ---------------------------------------------------------------------------
// Threshold control and the read-only visualizers
// ---------------------------------------------------------------------------

namespace {

class FakeThresholdSource : public ThresholdMeterSource {
public:
    float getMeterLevel() const override { return 0.0f; }
    float getEffectiveThreshold() const override { return 0.5f; }
    bool isOverThreshold() const override { return false; }
    int getTriggerCount() const override { return 0; }
    ThresholdScale getThresholdScale() const override { return ThresholdScale::Unipolar; }
    juce::String getThresholdParamID() const override { return "trigThreshold"; }
};

} // namespace

TEST(ThresholdControlKeyboard, TheAttachedSliderIsTheTabStopAndArrowsAdjustIt) {
    FakeThresholdSource source;
    juce::AudioParameterFloat param(juce::ParameterID("trigThreshold", 1), "Threshold", 0.0f, 1.0f, 0.5f);
    ThresholdControlComponent control(source, &param);
    control.setBounds(0, 0, 200, control.getPreferredHeight());

    auto* slider = control.getSlider();
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(slider->getWantsKeyboardFocus());
    EXPECT_FALSE(control.getWantsKeyboardFocus()) << "the container would be a second stop for the same value";
    EXPECT_EQ(slider->getTitle(), "Threshold");
    EXPECT_TRUE(slider->getTooltip().isNotEmpty());
    EXPECT_TRUE(control.getTitle().isNotEmpty());
    EXPECT_TRUE(control.getTooltip().isNotEmpty());

    const double before = slider->getValue();
    EXPECT_TRUE(slider->keyPressed(key(juce::KeyPress::upKey)));
    EXPECT_GT(slider->getValue(), before);
    EXPECT_TRUE(slider->keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_NEAR(slider->getValue(), before, 1e-6);
}

TEST(ThresholdControlKeyboard, TheMeterOnlyFormIsNamedAndNotATabStop) {
    FakeThresholdSource source;
    ThresholdControlComponent control(source);
    EXPECT_FALSE(control.getWantsKeyboardFocus());
    EXPECT_TRUE(control.getTitle().isNotEmpty());
    EXPECT_TRUE(control.getDescription().isNotEmpty());
    EXPECT_TRUE(control.getTooltip().isNotEmpty());
}

TEST(ModuleViewsReadOnly, EveryVisualizerHasANameADescriptionAndATooltipAndIsNotATabStop) {
    VisualBuffer buffer(256);
    ScopeComponent scope(buffer);
    FilterModule filter;
    FrequencyResponseComponent response(filter);
    WavetableOscillatorModule wavetable;
    WavetableDisplayComponent display(wavetable);
    SamplerModule sampler;
    SampleWaveformComponent waveform(sampler);

    for (juce::Component* view : std::initializer_list<juce::Component*>{&scope, &response, &display, &waveform}) {
        EXPECT_TRUE(view->getTitle().isNotEmpty());
        EXPECT_TRUE(view->getDescription().isNotEmpty());
        EXPECT_FALSE(view->getWantsKeyboardFocus());
        auto* tip = dynamic_cast<juce::TooltipClient*>(view);
        ASSERT_NE(tip, nullptr);
        EXPECT_TRUE(tip->getTooltip().isNotEmpty());
    }
}
