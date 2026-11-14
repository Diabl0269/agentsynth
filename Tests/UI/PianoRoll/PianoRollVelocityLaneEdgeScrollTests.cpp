// The velocity strip's pen and ramp follow the roll's view: held at the strip's left or right edge they scroll the
// roll sideways on its own edge timer (never up or down: the strip sits under the grid), the ramp stays anchored to the
// note it began on and reaches the notes that scroll in, release and Esc stop it, and the gesture is one undo step.

#include "../Layout/ViewportDragFollow.h"
#include "PianoRollVelocityTestHelpers.h"

#include <gtest/gtest.h>

using Gesture = synth::ui::PianoRollVelocityLane::Gesture;

namespace {

constexpr double kZoomedIn = 200.0; // px per beat: about four of the clip's eight beats show
constexpr int kShift = juce::ModifierKeys::shiftModifier;

struct EdgeFixture : VelocityLaneFixture {
    Bed bed;
    EdgeFixture() {
        bed = makeBed({100, 100, 100, 100, 100, 100, 100});
        open(bed.clipId, kZoomedIn);
    }
    juce::Point<float> rightEdge(int velocity) {
        return {(float)lane().getWidth() - 2.0f, lane().yForVelocity(velocity)};
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i)
            roll.tickAutoScrollForTest();
    }
};

} // namespace

TEST(PianoRollVelocityLaneEdgeScroll, ARampHeldAtTheRightEdgeScrollsTheRollAndReachesTheNotesThatScrollIn) {
    EdgeFixture f;
    auto& lane = f.lane();
    ASSERT_GT(f.roll.beatToX(7.0), (double)f.roll.getWidth()) << "the last note starts off screen";
    const double topRow = f.roll.getTopRowPositionForTest();
    const auto anchor = f.at(1.0, 100);
    const auto edge = f.rightEdge(20);
    lane.mouseDown(leftClick(lane, anchor, kShift));
    ASSERT_EQ(lane.getGesture(), Gesture::Ramp);
    lane.mouseDrag(leftDrag(lane, edge, anchor, kShift));
    ASSERT_TRUE(f.roll.isAutoScrollTimerRunningForTest());

    f.ticks(40);
    EXPECT_GT(f.roll.getRollViewState().firstVisibleBeat, 0.0) << "the roll scrolled right";
    EXPECT_DOUBLE_EQ(f.roll.getTopRowPositionForTest(), topRow) << "never up or down";
    lane.mouseUp(leftDrag(lane, edge, anchor, kShift));
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest()) << "release stops it";

    EXPECT_EQ(f.velocityOf(f.bed.notes[0]), 100) << "the ramp stays anchored to the note it began on";
    EXPECT_LT(f.velocityOf(f.bed.notes[6]), 100) << "the note that scrolled in under the pointer is on the ramp";
    for (size_t i = 1; i < f.bed.notes.size(); ++i)
        EXPECT_LE(f.velocityOf(f.bed.notes[i]), f.velocityOf(f.bed.notes[i - 1])) << "one straight falling ramp";
    f.undo.undo();
    for (const auto id : f.bed.notes)
        EXPECT_EQ(f.velocityOf(id), 100);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole ramp is one undo step";

    // A pen stroke pressed with the pointer already inside the right edge zone stays put until the pointer moves toward
    // it.
    EdgeFixture still;
    auto& stillLane = still.lane();
    const juce::Point<float> inZone((float)stillLane.getWidth() - 12.0f, still.lane().yForVelocity(20));
    const auto from =
        inZone; // a pen stroke can start anywhere between the notes, so the press itself lands in the zone
    const juce::Point<float> towardEdge(inZone.x + 6.0f, inZone.y);
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             stillLane.mouseDown(leftClick(stillLane, from));
             stillLane.mouseDrag(leftDrag(stillLane, inZone, from));
         },
         [&] { still.ticks(1); }, [&] { stillLane.mouseDrag(leftDrag(stillLane, towardEdge, from)); },
         [&] { return still.roll.getRollViewState().firstVisibleBeat; },
         [&] { stillLane.mouseUp(leftDrag(stillLane, towardEdge, from)); }});
}

TEST(PianoRollVelocityLaneEdgeScroll, APenHeldAtTheRightEdgeSetsEveryNoteThatPassesUnderIt) {
    EdgeFixture f;
    auto& lane = f.lane();
    const auto press = f.at(0.5, 30); // between notes: a pen stroke
    const auto edge = f.rightEdge(30);
    lane.mouseDown(leftClick(lane, press));
    ASSERT_EQ(lane.getGesture(), Gesture::Pen);
    lane.mouseDrag(leftDrag(lane, edge, press));
    f.ticks(40);
    lane.mouseUp(leftDrag(lane, edge, press));
    for (const auto id : f.bed.notes)
        EXPECT_EQ(f.velocityOf(id), 30) << "every note the still pointer passed over";
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollVelocityLaneEdgeScroll, EscapeAndMovingBackInStopTheScroll) {
    EdgeFixture f;
    auto& lane = f.lane();
    const auto anchor = f.at(1.0, 100);
    lane.mouseDown(leftClick(lane, anchor, kShift));
    lane.mouseDrag(leftDrag(lane, f.rightEdge(20), anchor, kShift));
    ASSERT_TRUE(f.roll.isAutoScrollTimerRunningForTest());
    lane.mouseDrag(leftDrag(lane, f.at(2.0, 20), anchor, kShift));
    f.ticks(4); // the speed eases out over a few frames, then the follow stops itself
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest()) << "back in the middle: no scroll";
    const double rested = f.roll.getRollViewState().firstVisibleBeat;
    f.ticks(4);
    EXPECT_EQ(f.roll.getRollViewState().firstVisibleBeat, rested) << "and the roll stays still";
    lane.mouseDrag(leftDrag(lane, f.rightEdge(20), anchor, kShift));
    f.ticks(10);
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest()) << "Esc stops it";
    lane.mouseUp(leftDrag(lane, f.rightEdge(20), anchor, kShift));
    for (const auto id : f.bed.notes)
        EXPECT_EQ(f.velocityOf(id), 100);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollVelocityLaneEdgeScroll, AStickDragOnlyChangesAValueAndNeverScrolls) {
    EdgeFixture f;
    auto& lane = f.lane();
    const auto press = f.at(1.0, 100);
    lane.mouseDown(leftClick(lane, press));
    ASSERT_EQ(lane.getGesture(), Gesture::Stick);
    lane.mouseDrag(leftDrag(lane, f.rightEdge(40), press));
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest());
    lane.mouseUp(leftDrag(lane, f.rightEdge(40), press));
}
