// AutomationLanesShapeCountTests.cpp -- the cycle count of the timeline lane's Draw box: with Snap off, Option+drag up
// or down and `[` `]` set how many cycles a box stamps in the same width, the count never passes what fits under the
// lane's point cap, a Snap grid sets the cycles instead and says so. Real events on the real panel's lane editor.

#include "../../../UI/Layout/NativeWindowTestGuard.h"
#include "CountLane.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/ModuleViews/ShapeEditor/CycleCountStepper.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawStroke.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"
#include <cmath>

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::CycleCountStepper;
using synth::ui::DrawShape;
using synth::ui::EditTool;

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
