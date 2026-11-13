// SegmentBendDragTests.cpp -- the pure maths of dragging the segment between two dots (SegmentBendDrag.h): which way a
// bend turns, the lane's half-way fraction and tension, the hold threshold and the fine drag.
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {
// A rising segment 100 px tall: starts at y=100, ends at y=0.
constexpr double kStartY = 100.0;
constexpr double kEndY = 0.0;
// The pointer travel (y down) that takes the half-way point from a straight line to the tension +1 extreme.
double travelToExtreme() { return (minMidFraction() - 0.5) * (kEndY - kStartY); }
} // namespace

TEST(SegmentBendDrag, UpwardSignFlipsForRisingAndFalling) {
    EXPECT_EQ(upwardBendSign(0.2, 0.8), -1.0f);
    EXPECT_EQ(upwardBendSign(0.8, 0.2), 1.0f);
}

TEST(SegmentBendDrag, MidFractionAndTensionRoundTrip) {
    EXPECT_NEAR(midFractionForTension(0.0f), 0.5, 1e-12);
    EXPECT_NEAR(minMidFraction(), 0.0625, 1e-12);
    EXPECT_NEAR(maxMidFraction(), std::pow(0.5, 0.25), 1e-12);
    for (float t = -1.0f; t <= 1.0f; t += 0.125f)
        EXPECT_NEAR(tensionForMidFraction(midFractionForTension(t)), t, 1e-5f) << t;
}

TEST(SegmentBendDrag, TensionForMidFractionClamps) {
    EXPECT_EQ(tensionForMidFraction(0.0001), 1.0f);
    EXPECT_EQ(tensionForMidFraction(-3.0), 1.0f);
    EXPECT_EQ(tensionForMidFraction(0.999), -1.0f);
}

TEST(SegmentBendDrag, UpMakesARisingSegmentBulgeUp) {
    // Pointer up 10 px on a rising segment: the half-way point is higher, so the fraction grows and tension drops.
    const auto bend = laneBendAt(kStartY, kEndY, 0.5, -10.0, 0.0f, false);
    EXPECT_FALSE(bend.hold);
    EXPECT_LT(bend.tension, 0.0f);
    EXPECT_NEAR(midFractionForTension(bend.tension), 0.6, 1e-5);
}

TEST(SegmentBendDrag, UpMakesAFallingSegmentBulgeUp) {
    // Falling: starts at y=0 (high), ends at y=100. Up 10 px moves the half-way point toward the start value.
    const auto bend = laneBendAt(0.0, 100.0, 0.5, -10.0, 0.0f, false);
    EXPECT_FALSE(bend.hold);
    EXPECT_GT(bend.tension, 0.0f);
    EXPECT_NEAR(midFractionForTension(bend.tension), 0.4, 1e-5);
}

TEST(SegmentBendDrag, HoldThresholdOnOffAndBack) {
    const double extreme = travelToExtreme();
    EXPECT_FALSE(laneBendAt(kStartY, kEndY, 0.5, extreme + kHoldOvershootPx - 1.0, 0.0f, false).hold);
    const auto at = laneBendAt(kStartY, kEndY, 0.5, extreme + kHoldOvershootPx - 1.0, 0.0f, false);
    EXPECT_NEAR(at.tension, 1.0f, 1e-6f);
    const auto held = laneBendAt(kStartY, kEndY, 0.5, extreme + kHoldOvershootPx + 1.0, 0.0f, false);
    EXPECT_TRUE(held.hold);
    EXPECT_EQ(held.tension, 1.0f);
    // Back above the threshold before release: a curve again.
    EXPECT_FALSE(laneBendAt(kStartY, kEndY, 0.5, extreme + 2.0, 0.0f, false).hold);
}

TEST(SegmentBendDrag, TowardTheEndValueOnlyClamps) {
    const auto bend = laneBendAt(kStartY, kEndY, 0.5, -500.0, 0.0f, false);
    EXPECT_FALSE(bend.hold);
    EXPECT_EQ(bend.tension, -1.0f);
}

TEST(SegmentBendDrag, FlatSegmentChangesNothing) {
    const auto bend = laneBendAt(50.0, 50.2, 0.5, 80.0, 0.3f, false);
    EXPECT_FALSE(bend.hold);
    EXPECT_EQ(bend.tension, 0.3f);
    EXPECT_TRUE(laneBendAt(50.0, 50.0, 0.5, 80.0, 0.3f, true).hold);
}

TEST(SegmentBendDrag, AHoldComesBackAfterAboutTwelvePixelsTowardTheEnd) {
    LaneBendDrag drag(kStartY, kEndY, 0.0f, true, 60.0);
    EXPECT_TRUE(drag.moveTo(60.0 - 5.0, false).hold);
    EXPECT_TRUE(drag.moveTo(60.0 - 9.0, false).hold);
    const auto out = drag.moveTo(60.0 - (kHoldOvershootPx + 1.0), false);
    EXPECT_FALSE(out.hold);
    EXPECT_NEAR(out.tension, 1.0f, 1e-5f);
    EXPECT_TRUE(drag.moveTo(60.0, false).hold) << "dragging back re-enters the hold";
    EXPECT_EQ(drag.moveTo(60.0, false).tension, 0.0f) << "a hold left alone keeps the tension it had";
}

TEST(SegmentBendDrag, ShiftScalesEachStepByATenth) {
    LaneBendDrag fine(kStartY, kEndY, 0.0f, false, 50.0);
    LaneBendDrag plain(kStartY, kEndY, 0.0f, false, 50.0);
    const auto a = fine.moveTo(40.0, true);
    const auto b = plain.moveTo(49.0, false);
    EXPECT_NEAR(a.tension, b.tension, 1e-6f);
    // Releasing Shift mid-drag: the steps already taken stay as they were.
    const auto c = fine.moveTo(30.0, false);
    const auto d = plain.moveTo(39.0, false);
    EXPECT_NEAR(c.tension, d.tension, 1e-6f);
}
