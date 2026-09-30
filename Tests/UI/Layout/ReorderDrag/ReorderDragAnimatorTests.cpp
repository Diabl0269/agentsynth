// ReorderDragAnimatorTests.cpp: the shared drag-to-reorder maths, driven with an injected clock.
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Layout/UIAnimation.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::ReorderDragAnimator;
using Slot = ReorderDragAnimator::Slot;

struct Rig {
    double nowMs = 1000.0;
    ReorderDragAnimator animator{[this] { return nowMs; }};
};

// Three items of unequal extent along one axis: [0,100) [100,140) [140,240).
std::vector<Slot> unequalSlots() { return {{0, 100}, {100, 40}, {140, 100}}; }
// Equal items with a gap, the way mixer columns sit.
std::vector<Slot> columnSlots() { return {{0, 140}, {144, 140}, {288, 140}, {432, 140}}; }

} // namespace

TEST(ReorderDragAnimatorTests, StaysAClickBelowTheDragThreshold) {
    Rig r;
    r.animator.begin(unequalSlots(), 0, 10.0f, 10.0f);
    EXPECT_FALSE(r.animator.dragTo(13.0f));
    EXPECT_FALSE(r.animator.isReordering());
    EXPECT_FALSE(r.animator.needsFrames());
    EXPECT_TRUE(r.animator.dragTo(14.0f)) << "4 px is the threshold";
    EXPECT_TRUE(r.animator.isDragging());
}

TEST(ReorderDragAnimatorTests, DraggedItemKeepsTheGrabOffsetAndIsHeldInsideTheStrip) {
    Rig r;
    r.animator.begin(columnSlots(), 1, 30.0f, 174.0f);
    r.animator.dragTo(200.0f);
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 170.0f) << "pointer minus grab";
    r.animator.dragTo(-500.0f);
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 0.0f);
    r.animator.dragTo(5000.0f);
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 432.0f + 140.0f - 140.0f);
}

// Insertion is decided by the dragged item's centre crossing a neighbour's midpoint, so with
// unequal widths it flips at one fixed pointer position in each direction and cannot ping-pong.
TEST(ReorderDragAnimatorTests, InsertionFollowsTheCentreCrossingMidpointsWithUnequalWidths) {
    Rig r;
    r.animator.begin(unequalSlots(), 0, 0.0f, 0.0f);
    // Item 0 is 100 wide: its centre is start + 50. Item 1's midpoint is 120, item 2's is 190.
    r.animator.dragTo(69.0f);
    EXPECT_EQ(r.animator.getInsertionIndex(), 0);
    r.animator.dragTo(71.0f);
    EXPECT_EQ(r.animator.getInsertionIndex(), 1);
    r.animator.dragTo(139.0f);
    EXPECT_EQ(r.animator.getInsertionIndex(), 1);
    r.animator.dragTo(141.0f); // clamped at the strip end (start 140), which is the last slot
    EXPECT_EQ(r.animator.getInsertionIndex(), 2);
    r.animator.dragTo(100.0f);
    EXPECT_EQ(r.animator.getInsertionIndex(), 1);
    const std::vector<int> order{1, 0, 2};
    EXPECT_EQ(r.animator.getNewOrder(), order);
}

TEST(ReorderDragAnimatorTests, FeedingTheSamePointerRepeatedlyNeverChangesTheAnswer) {
    Rig r;
    r.animator.begin(unequalSlots(), 2, 0.0f, 200.0f);
    for (float pointer : {150.0f, 118.0f, 60.0f, 20.0f}) {
        r.animator.dragTo(pointer);
        const int first = r.animator.getInsertionIndex();
        r.nowMs += 300.0;
        r.animator.dragTo(pointer);
        EXPECT_EQ(r.animator.getInsertionIndex(), first) << "pointer " << pointer;
    }
}

TEST(ReorderDragAnimatorTests, NeighboursGlideToMakeRoomOverTheMakeRoomDurationWithEaseOutCubic) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.dragTo(200.0f); // centre 270: past column 1's midpoint (214), before column 2's (358)
    ASSERT_EQ(r.animator.getInsertionIndex(), 1);
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), 144.0f) << "starts from its rest position";
    r.nowMs += ReorderDragAnimator::kMakeRoomMs * 0.5;
    const float half = 144.0f - 144.0f * synth::ui::easeOutCubic(0.5f);
    EXPECT_NEAR(r.animator.getLayoutStart(1), half, 0.01f);
    r.nowMs += ReorderDragAnimator::kMakeRoomMs;
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), 0.0f) << "column 1 now sits in the vacated first slot";
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(2), 288.0f) << "columns past the gap do not move";
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(0), 144.0f) << "the gap marker is at the insertion slot";
}

// A second insertion change while a glide is in flight starts from where the item is drawn now.
TEST(ReorderDragAnimatorTests, RetargetStartsFromTheCurrentAnimatedOffsetNeverFromRest) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.dragTo(200.0f);
    r.nowMs += 80.0;
    const float midFlight = r.animator.getLayoutStart(1);
    ASSERT_GT(midFlight, 0.0f);
    ASSERT_LT(midFlight, 144.0f);

    r.animator.dragTo(0.0f); // back over the first slot: column 1 is sent back to rest
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), midFlight) << "no jump at the moment of retargeting";
    r.nowMs += ReorderDragAnimator::kMakeRoomMs * 0.25;
    EXPECT_GT(r.animator.getLayoutStart(1), midFlight) << "and heads back towards 144";
    EXPECT_LT(r.animator.getLayoutStart(1), 144.0f);
}

TEST(ReorderDragAnimatorTests, TweenGenerationChangesOnlyWhenNewTweensStart) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.dragTo(10.0f);
    const auto afterLift = r.animator.getTweenGeneration();
    r.animator.dragTo(20.0f);
    EXPECT_EQ(r.animator.getTweenGeneration(), afterLift) << "moving inside the same slot starts nothing";
    r.animator.dragTo(200.0f);
    EXPECT_NE(r.animator.getTweenGeneration(), afterLift);
}

// The drop: the dragged item glides from where it was released into its final slot and the
// animator then goes quiet -- needsFrames() false is what stops the owner's repaints.
TEST(ReorderDragAnimatorTests, ReleaseSettlesIntoTheFinalSlotThenStopsNeedingFrames) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.dragTo(200.0f);
    r.nowMs += 500.0; // make-room glides are done
    ASSERT_FALSE(r.animator.needsFrames()) << "a held drag with the pointer at rest needs no frames";
    ASSERT_EQ(r.animator.getInsertionIndex(), 1);

    r.animator.release({144.0f, 0.0f, 288.0f, 432.0f}); // column 1 first, column 0 second
    EXPECT_TRUE(r.animator.isReordering());
    EXPECT_TRUE(r.animator.isLifted());
    EXPECT_TRUE(r.animator.needsFrames());
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 200.0f) << "starts at the drop position";
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), 0.0f) << "column 1 already sits in its new slot";

    r.nowMs += ReorderDragAnimator::kSettleMs * 0.5;
    const float expected = 144.0f + (200.0f - 144.0f) * (1.0f - synth::ui::easeOutCubic(0.5f));
    EXPECT_NEAR(r.animator.getDraggedStart(), expected, 0.01f);
    EXPECT_GT(r.animator.getLift(), 0.0f);
    EXPECT_LT(r.animator.getLift(), 1.0f);

    r.nowMs += ReorderDragAnimator::kSettleMs;
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 144.0f);
    EXPECT_FALSE(r.animator.isLifted());
    EXPECT_FALSE(r.animator.needsFrames());
    EXPECT_TRUE(r.animator.finishIfSettled());
    EXPECT_FALSE(r.animator.isReordering());
    EXPECT_FALSE(r.animator.finishIfSettled());
}

TEST(ReorderDragAnimatorTests, ReleaseMidGlideStartsNeighboursSettleFromWhereTheyAre) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.dragTo(200.0f);
    r.nowMs += 40.0; // column 1 is mid make-room glide
    const float before = r.animator.getLayoutStart(1);
    r.animator.release({144.0f, 0.0f, 288.0f, 432.0f});
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), before) << "no jump at the moment of release";
    r.nowMs += ReorderDragAnimator::kSettleMs;
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), 0.0f);
}

TEST(ReorderDragAnimatorTests, AnOffScreenOwnerLandsEverythingInstantly) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f, /*animate=*/false);
    r.animator.dragTo(200.0f);
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), 0.0f) << "no glide without frames to drive it";
    EXPECT_FLOAT_EQ(r.animator.getLift(), 1.0f);
    r.animator.release({144.0f, 0.0f, 288.0f, 432.0f});
    EXPECT_FALSE(r.animator.isReordering());
    EXPECT_FALSE(r.animator.needsFrames());
}

// Cancel returns the item to where it was picked up with ease-in, and nothing is committed.
TEST(ReorderDragAnimatorTests, AbortSendsTheDraggedItemHomeEasingInAndNeighboursBack) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.dragTo(200.0f);
    r.nowMs += 500.0;
    r.animator.abort();
    EXPECT_TRUE(r.animator.isReordering());
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 200.0f);

    r.nowMs += ReorderDragAnimator::kSettleMs * 0.5;
    const float easedIn = 200.0f * (1.0f - synth::ui::easeInCubic(0.5f));
    EXPECT_NEAR(r.animator.getDraggedStart(), easedIn, 0.01f);
    EXPECT_GT(r.animator.getDraggedStart(), 100.0f) << "ease-in leaves slowly: more than half still to go";

    r.nowMs += ReorderDragAnimator::kSettleMs;
    EXPECT_FLOAT_EQ(r.animator.getDraggedStart(), 0.0f);
    EXPECT_FLOAT_EQ(r.animator.getLayoutStart(1), 144.0f) << "the neighbour is back in its own slot";
    EXPECT_TRUE(r.animator.finishIfSettled());
}

TEST(ReorderDragAnimatorTests, AbortBeforeTheThresholdIsJustACancel) {
    Rig r;
    r.animator.begin(columnSlots(), 0, 0.0f, 0.0f);
    r.animator.abort();
    EXPECT_FALSE(r.animator.isReordering());
    EXPECT_FALSE(r.animator.dragTo(300.0f)) << "a cancelled gesture ignores further pointer events";
}
