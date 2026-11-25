// AutomationLanesShapeCountTests.cpp -- the cycle count of the timeline lane's Draw box: with Snap off, Option+drag up
// or down and `[` `]` set how many cycles a box stamps in the same width, the count never passes what fits under the
// lane's point cap, a Snap grid sets the cycles instead and says so, and the lane draws the same count as the shape
// windows' curve pen for the same span. Real events on the real panel's lane editor.

#include "../../../UI/Layout/NativeWindowTestGuard.h"
#include "../../ModuleViews/CurveEditor/CurveEditorTestHelpers.h"
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "AutomationLanesTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/ModuleViews/CurveEditor/CurveDrawGesture.h"
#include "UI/ModuleViews/ShapeEditor/CycleCountStepper.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawStroke.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"
#include <cmath>

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::CurveDrawGesture;
using synth::ui::CycleCountStepper;
using synth::ui::DrawShape;
using synth::ui::EditTool;
using synth::ui::test::curveLeftClick;
using synth::ui::test::makeCurveMouseEvent;

namespace {

constexpr float kStep = CycleCountStepper::kStepPx;
const juce::KeyPress kOpen('[');
const juce::KeyPress kClose(']');

// An open Bass track with one [0, 100] lane and Snap off, the Draw tool and a Sine chosen.
struct CountLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;
    juce::Point<float> from, anchor;

    explicit CountLane(DrawShape shape = DrawShape::Sine, bool snapOff = true) {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        panel.getViewState().snapEnabled = !snapOff;
        panel.setActiveTool(EditTool::Draw);
        panel.setDrawShape(shape);
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    synth::ui::AutomationLaneShapeGesture& gesture() { return editor->getShapeGesture(); }
    float xAt(double beat) { return (float)panel.getViewState().beatToX(beat); }
    float yAt(double value) { return (float)editor->valueToY(value); }

    // A plain drag from `beat0` to `beat1` and no release yet.
    void press(double beat0, double beat1) {
        from = {xAt(beat0), yAt(80.0)};
        anchor = from;
        editor->mouseDown(makeClickEvent(*editor, from, leftButton()));
        editor->mouseDrag(makeDragEvent(*editor, {xAt(beat1), yAt(20.0)}, from, leftButton()));
        end_ = {xAt(beat1), yAt(20.0)};
    }
    // Option down with the pointer `steps` count steps above where it went down (the first call is where it goes down).
    void option(float steps) {
        if (!optionDown_) {
            optionDown_ = true;
            optionY_ = end_.y;
        }
        const juce::Point<float> p{end_.x, optionY_ - steps * kStep};
        editor->mouseDrag(makeDragEvent(*editor, p, from, leftButton(juce::ModifierKeys::altModifier)));
    }
    void release() { editor->mouseUp(makeDragEvent(*editor, end_, from, leftButton())); }
    bool key(const juce::KeyPress& k) { return editor->keyPressed(k); }
    // Cycles the last-drawn sine stamp holds, read from its points.
    int drawnSineCycles() const { return ((int)theLane().points.size() - 1) / synth::ui::kSinePointsPerCycle; }
    juce::String chip() { return gesture().getChipText(); }

private:
    juce::Point<float> end_;
    bool optionDown_ = false;
    float optionY_ = 0.0f;
};

juce::String cyclesChip(int cycles, const char* length = "1 bar") {
    return juce::String(cycles) + " cycles" + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + length;
}

} // namespace

// Regression test for FRO2311: the lane's Draw box had no count gesture, unlike the shape windows' pen.
TEST(AutomationLanesShapeCountTest, OptionDragUpAndDownChangesTheCountInTheSameWidthWithNoDeadStep) {
    CountLane f;
    f.press(0.0, 4.0);
    ASSERT_EQ(f.chip(), cyclesChip(4)) << "a beat a cycle until the count is set";
    f.option(0.0f); // Option goes down here: nothing changes yet
    EXPECT_EQ(f.chip(), cyclesChip(4));
    f.option(3.0f);
    EXPECT_EQ(f.chip(), cyclesChip(7)) << "three steps up: three more cycles, the box as it was";
    f.option(-1.0f);
    EXPECT_EQ(f.chip(), cyclesChip(3)) << "one step below where Option went down: one fewer";
    f.option(3.0f);
    EXPECT_EQ(f.chip(), cyclesChip(7));
    f.undo.clearUndoHistory();
    f.release();
    EXPECT_EQ(f.theLane().points.size(), 7u * synth::ui::kSinePointsPerCycle + 1u);
    EXPECT_NEAR(f.gesture().getCycleWidthBeats().value(), 4.0 / 7.0, 1e-9) << "remembered for the next box";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.theLane().points.empty()) << "the whole stamp is ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesShapeCountTest, TheCountStopsAtWhatFitsUnderThePointCapAndStepsBackAtOnce) {
    CountLane f;
    // The lane keeps 16058 points outside the box: a Sine fits 20 cycles (16 points each, plus the closing point).
    std::vector<synth::AutomationLane::Breakpoint> filler;
    for (int i = 0; i < 16058; ++i)
        filler.push_back({100.0 + 0.01 * i, 50.0, 0.0f, 1});
    ASSERT_TRUE(f.doc.editBreakpoints(f.lane, {}, filler));
    f.undo.clearUndoHistory();
    constexpr int kMost = 20;

    f.press(0.0, 4.0);
    f.option(0.0f);
    f.option(100.0f);
    EXPECT_EQ(f.chip(), cyclesChip(kMost).replace(" cycles", " cycles (most that fit)"))
        << "far past the cap the count is the most that fit";
    f.option(99.0f);
    EXPECT_EQ(f.chip(), cyclesChip(kMost - 1)) << "one step back lowers the count at once: no hidden steps";
    f.option(100.0f);
    EXPECT_EQ(f.chip(), cyclesChip(kMost)) << "exactly the most that fit is not past it";
    f.option(101.0f);
    EXPECT_TRUE(f.chip().contains("20 cycles (most that fit)")) << f.chip();
    f.release();
    EXPECT_TRUE(f.gesture().getStatusText().isEmpty()) << "the count asked is the count drawn, never refused";
    EXPECT_EQ(f.theLane().points.size(), 16058u + (size_t)kMost * synth::ui::kSinePointsPerCycle + 1u);
}

TEST(AutomationLanesShapeCountTest, TheBracketKeysStepTheCountInABoxAndBetweenBoxesAndStepBackReturns) {
    CountLane f;
    f.press(0.0, 4.0);
    EXPECT_TRUE(f.key(kClose));
    EXPECT_EQ(f.chip(), cyclesChip(5));
    EXPECT_TRUE(f.key(kOpen));
    EXPECT_TRUE(f.key(kOpen));
    EXPECT_EQ(f.chip(), cyclesChip(3));
    f.release();
    ASSERT_EQ(f.drawnSineCycles(), 3);

    EXPECT_TRUE(f.key(kClose)) << "between boxes: the count per bar";
    EXPECT_EQ(f.gesture().getNoteText(), "4 cycles per bar");
    EXPECT_TRUE(f.key(kOpen));
    EXPECT_EQ(f.gesture().getNoteText(), "3 cycles per bar");
    f.key(kClose);
    f.press(0.0, 4.0);
    EXPECT_EQ(f.chip(), cyclesChip(4)) << "the next box takes the stepped count";
    f.key(kOpen);
    f.key(kOpen);
    f.key(kOpen);
    f.key(kOpen);
    EXPECT_EQ(f.chip(), juce::String(juce::CharPointer_UTF8("1 cycle \xc2\xb7 1 bar"))) << "never below one";
    f.key(kClose);
    EXPECT_EQ(f.chip(), cyclesChip(2));
    f.release();
}

TEST(AutomationLanesShapeCountTest, WithSnapOnTheGridSetsTheCyclesAndOptionAndTheKeysSayWhy) {
    CountLane f(DrawShape::Sine, false);
    f.press(0.0, 4.0);
    ASSERT_EQ(f.chip(), cyclesChip(4)) << "a cycle a quarter-beat... of the grid: four a bar";
    f.option(0.0f);
    f.option(3.0f);
    EXPECT_TRUE(f.chip().contains("A grid sets the cycles")) << f.chip();
    EXPECT_TRUE(f.key(kClose)) << "the key is taken, saying why";
    EXPECT_TRUE(f.gesture().getNoteText().contains("Grid Off")) << f.gesture().getNoteText();
    EXPECT_FALSE(f.gesture().getCycleWidthBeats().has_value()) << "nothing was remembered";
    f.release();
    EXPECT_EQ(f.theLane().points.size(), 4u * synth::ui::kSinePointsPerCycle + 1u) << "still the grid's four cycles";
}

// The sibling-gap guard for the two Draw surfaces: the same span and the same count drawn on the shape windows' pen
// and on the lane's box are the same number of cycles, up and down and past the most that fit.
TEST(AutomationLanesShapeCountTest, LaneAndCurveEditorDrawTheSameCycleCountForTheSameSpanAndCount) {
    for (const float steps : {5.0f, -2.0f, 20.0f}) {
        CountLane lane;
        lane.press(0.0, 4.0);
        lane.option(0.0f);
        lane.option(steps);
        lane.release();

        synth::ui::test::ShapeRig rig;
        synth::ui::ShapeListEditor editor(rig.adapter, 0);
        editor.setPen(DrawShape::Sine);
        auto& curve = editor.getCurveEditor();
        curve.getDrawGesture().setCycleWidth(0.8 / 4.0); // the lane's default: four cycles in the span
        const auto at = [&](double x, float level) { return curve.pixelForDrawPoint({x, (double)level}); };
        const auto from = at(0.1, 0.5f);
        const auto to = at(0.9, 0.5f);
        const auto event = [&](juce::Point<float> p, bool alt) {
            return makeCurveMouseEvent(curve, p,
                                       juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                                                          (alt ? juce::ModifierKeys::altModifier : 0)),
                                       true, from);
        };
        curve.mouseDown(curveLeftClick(curve, from));
        curve.mouseDrag(event(to, false));
        curve.mouseDrag(event(to, true));
        curve.mouseDrag(event({to.x, to.y - steps * CurveDrawGesture::kCountStepPx}, true));
        const double curveCycles = curve.getDrawGesture().result().cycles;
        curve.mouseUp(event(to, false));

        EXPECT_NEAR(curveCycles, (double)lane.drawnSineCycles(), 1e-6) << "Option steps: " << steps;
        EXPECT_GE(curveCycles, 1.0);
    }
}

TEST(AutomationLanesShapeCountTest, TheBracketKeysAreTheLanesOnlyWithTheDrawToolAndAPeriodicShapeAndNoAppShortcut) {
    ShortcutManager shortcuts;
    EXPECT_TRUE(shortcuts.getActionsForKeyPress(kOpen).isEmpty()) << "no default action owns [";
    EXPECT_TRUE(shortcuts.getActionsForKeyPress(kClose).isEmpty()) << "no default action owns ]";

    CountLane f;
    EXPECT_TRUE(f.key(kClose)) << "Draw with a Sine: the lane takes it";
    EXPECT_FALSE(f.key(juce::KeyPress('[', juce::ModifierKeys::commandModifier, 0))) << "Cmd+[ stays with the app";
    f.panel.setDrawShape(DrawShape::Free);
    EXPECT_FALSE(f.key(kClose)) << "a Free pen has no cycles";
    f.panel.setDrawShape(DrawShape::Sine);
    f.panel.setActiveTool(EditTool::Select);
    EXPECT_FALSE(f.key(kClose)) << "another tool leaves the key alone";
}

TEST(AutomationLanesShapeCountTest, TheBracketKeysTypeIntoAFocusedTextFieldInsteadOfStepping) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    CountLane f;
    juce::TextEditor field;
    field.setSize(120, 24);
    field.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    field.setVisible(true);
    field.grabKeyboardFocus();
    if (!field.hasKeyboardFocus(true))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    EXPECT_FALSE(f.key(kClose)) << "a text field has the keyboard: the lane does not take the bracket";
    EXPECT_TRUE(f.gesture().getNoteText().isEmpty());
    field.giveAwayKeyboardFocus();
    field.removeFromDesktop();
    EXPECT_TRUE(f.key(kClose)) << "and the lane takes it again once the field lets go";
}

TEST(CycleCountStepperTest, StepsClampToTheEndsAndARebaseLeavesNoHiddenStep) {
    const auto up = CycleCountStepper::step(5, 1, 6);
    EXPECT_EQ(up.cycles, 6);
    EXPECT_FALSE(up.over);
    const auto past = CycleCountStepper::step(6, 1, 6);
    EXPECT_EQ(past.cycles, 6);
    EXPECT_TRUE(past.over);
    EXPECT_EQ(CycleCountStepper::step(1, -1, 6).cycles, 1);

    CycleCountStepper stepper;
    stepper.begin(100.0f, 3, 10);
    EXPECT_FALSE(stepper.dragTo(100.0f, 10).has_value()) << "no step yet";
    EXPECT_EQ(stepper.dragTo(100.0f - 40.0f * kStep, 10)->cycles, 10);
    EXPECT_EQ(stepper.dragTo(100.0f - 39.0f * kStep, 10)->cycles, 9) << "one step back changes the count at once";
    EXPECT_EQ(stepper.dragTo(100.0f + 40.0f * kStep, 10)->cycles, 1);
    EXPECT_EQ(stepper.dragTo(100.0f + 39.0f * kStep, 10)->cycles, 2);
}
