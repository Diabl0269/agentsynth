// SegmentBendDragTests.cpp -- the pure maths of dragging the segment between two dots (SegmentBendDrag.h): which way a
// bend turns, the lane's half-way fraction and tension, the hold threshold and the fine drag.
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include <cmath>
#include <gtest/gtest.h>
#include <string>

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
    EXPECT_FALSE(laneBendAt(kStartY, kEndY, 0.5, extreme + holdOvershootPx() - 1.0, 0.0f, false).hold);
    const auto at = laneBendAt(kStartY, kEndY, 0.5, extreme + holdOvershootPx() - 1.0, 0.0f, false);
    EXPECT_NEAR(at.tension, 1.0f, 1e-6f);
    const auto held = laneBendAt(kStartY, kEndY, 0.5, extreme + holdOvershootPx() + 1.0, 0.0f, false);
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

TEST(SegmentBendDrag, AHoldComesBackAfterAboutElevenPixelsTowardTheEnd) {
    LaneBendDrag drag(kStartY, kEndY, 0.0f, true, 60.0);
    EXPECT_TRUE(drag.moveTo(60.0 - 5.0, false).hold);
    EXPECT_TRUE(drag.moveTo(60.0 - 9.0, false).hold);
    const auto out = drag.moveTo(60.0 - (kHoldExitPx + 1.0), false);
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

// asymmetric-gesture-zone, the property: for every segment direction and height, the hold is reached by going past the
// START value's extreme by holdOvershootPx and never by going the other way, however far.
TEST(SegmentBendDrag, TheHoldZoneIsTheStartSideOfEverySegmentRisingOrFalling) {
    for (const double span : {-300.0, -100.0, -30.0, 30.0, 100.0, 300.0}) {
        const double startY = 400.0;
        const double endY = startY + span;
        const double toward = std::copysign(1.0, span);         // pixel direction from the start toward the end
        const double extreme = (minMidFraction() - 0.5) * span; // travel that reaches the tension +1 extreme
        const double over = holdOvershootPx();
        const auto shortOfIt = laneBendAt(startY, endY, 0.5, extreme - toward * (over - 2.0), 0.0f, false);
        const auto pastIt = laneBendAt(startY, endY, 0.5, extreme - toward * (over + 2.0), 0.0f, false);
        const auto wrongWay = laneBendAt(startY, endY, 0.5, toward * 5000.0, 0.0f, false);
        EXPECT_FALSE(shortOfIt.hold) << span;
        EXPECT_TRUE(pastIt.hold) << span;
        EXPECT_FALSE(wrongWay.hold) << span;
        EXPECT_EQ(wrongWay.tension, -1.0f) << span;
    }
}

TEST(SegmentBendDrag, TheOvershootIsTheSameForEverySegmentHeight) {
    EXPECT_DOUBLE_EQ(holdOvershootPx(), kHoldOvershootPx);
}

TEST(SegmentBendDrag, TheRoomPastTheExtremeCapsTheOvershootButNeverBelowTheMinimum) {
    EXPECT_DOUBLE_EQ(holdOvershootPx(1000.0), kHoldOvershootPx);
    EXPECT_DOUBLE_EQ(holdOvershootPx(10.0), 10.0);
    EXPECT_DOUBLE_EQ(holdOvershootPx(2.0), kMinHoldOvershootPx);
    EXPECT_DOUBLE_EQ(holdOvershootPx(-5.0), kMinHoldOvershootPx);
    // A falling segment from y=8 to y=108 inside a lane 0..116: the extreme sits 6.25 px below its start, so 14.25 px
    // of lane above it less the edge margin; a rising one mirrored has the same room below.
    EXPECT_NEAR(holdRoomPx(8.0, 108.0, 0.0, 116.0), 8.0 + minMidFraction() * 100.0 - kHoldEdgeMarginPx, 1e-9);
    EXPECT_NEAR(holdRoomPx(108.0, 8.0, 0.0, 116.0), 116.0 - (108.0 - minMidFraction() * 100.0) - kHoldEdgeMarginPx,
                1e-9);
}

// dead-zone-flip, the property: across every segment direction and height, from a straight line to the hold, the drawn
// shape never stands still and never jumps. The half-way fraction moves with the pointer up to the extreme, then
// `toHold` takes over and grows by at most a pixel's share of the overshoot per pixel, reaching 1 exactly where the
// hold begins, never a dead zone ending in a flip.
// asymmetric-gesture-zone at the lane's edges: a segment starting at the lane's top (falling) or bottom (rising) value
// holds with the pointer still inside the lane, for short and tall lanes alike.
TEST(SegmentBendDrag, ASegmentStartingAtTheLaneEdgeHoldsInsideTheLane) {
    constexpr double kPad = 8.0;
    for (const double laneHeight : {40.0, 80.0, 200.0, 400.0}) {
        const double top = kPad;
        const double bottom = laneHeight - kPad;
        for (const bool falling : {true, false}) {
            const double startY = falling ? top : bottom;
            const double endY = falling ? bottom : top;
            const double midY = startY + 0.5 * (endY - startY);
            LaneBendDrag drag(startY, endY, 0.0f, false, midY, 0.0, laneHeight);
            const double edge = falling ? 0.0 : laneHeight - 1.0; // the lane's first and last pixel rows
            const auto atEdge = drag.moveTo(edge, false);
            EXPECT_TRUE(atEdge.hold) << "lane " << laneHeight << (falling ? " falling" : " rising");
            EXPECT_GE(drag.overshootPx(), kMinHoldOvershootPx);
        }
    }
}

// dead-zone-flip + asymmetric-gesture-zone + preview-vs-commit over the whole lane, the property:
// for every start row x end row (lane edges included) x starting tension x press along the line (near either dot too)
// x direction, a drag from the press to the lane's edge, every pixel on the way, (1) moves the half-way point on every
// pixel (but across the hold's overshoot, where the curve rests at its steepest), never by more than its share, (2)
// reaches the limit at the edge: tension -1 on the end value's side, the hold on the start value's side, (3) returns
// only shapes a release can commit: a hold carries tension +1 and a curve a tension inside the range, and (4) is a pure
// function of the pointer row: going back to a row gives that row's shape again.
TEST(SegmentBendDrag, EveryPixelFromAnyPressToEitherLaneEdgeMovesTheShapeAndReachesTheLimit) {
    constexpr double kTop = 0.0;
    constexpr double kBottom = 200.0;
    const double rows[] = {0.0, 1.0, 8.0, 30.0, 50.0, 100.0, 150.0, 170.0, 192.0, 199.0};
    for (const double startY : rows)
        for (const double endY : rows) {
            const double span = endY - startY;
            if (std::abs(span) < 4.0)
                continue;
            for (const float tension : {-0.5f, 0.0f, 0.5f})
                for (const double along : {0.1, 0.3, 0.5, 0.7, 0.9}) {
                    const double f0 = midFractionForTension(tension);
                    // The line at `along` of the way across the segment's beats sits at fraction along^gamma.
                    const double gamma = std::log(f0) / std::log(0.5);
                    const double pressY = startY + std::pow(along, gamma) * span;
                    const double toward = span > 0.0 ? 1.0 : -1.0;
                    const std::string where = std::to_string(startY) + "->" + std::to_string(endY) + " t " +
                                              std::to_string(tension) + " at " + std::to_string(pressY);

                    // Toward the end value: the half-way fraction grows every pixel until the lane's last row.
                    LaneBendDrag toEnd(startY, endY, tension, false, pressY, kTop, kBottom);
                    const double endEdge = toward > 0.0 ? kBottom - 1.0 : kTop;
                    const double room = std::abs(endEdge - pressY);
                    double last = f0;
                    LaneBend b;
                    for (double px = 1.0; px < room + 1.0; px += 1.0) {
                        const double y = toward > 0.0 ? std::min(pressY + px, endEdge) : std::max(pressY - px, endEdge);
                        b = toEnd.moveTo(y, false);
                        const double f = midFractionForTension(b.tension);
                        EXPECT_FALSE(b.hold) << where;
                        EXPECT_GE(f, last) << where << " px " << px;
                        if (f0 < maxMidFraction() - 1e-4 && f < maxMidFraction() - 2e-3)
                            EXPECT_GT(f, last) << where << " px " << px << ": the curve stood still";
                        last = f;
                    }
                    if (room >= 1.0)
                        EXPECT_NEAR(last, maxMidFraction(), 1e-3) << where << ": the limit arrives at the edge";

                    // Toward the start value: the curve to its steepest, rest across the overshoot, then the hold.
                    LaneBendDrag toStart(startY, endY, tension, false, pressY, kTop, kBottom);
                    const double startEdge = toward > 0.0 ? kTop : kBottom - 1.0;
                    const double roomStart = std::abs(pressY - startEdge);
                    last = f0;
                    bool held = false;
                    for (double px = 1.0; px < roomStart + 1.0; px += 1.0) {
                        const double y =
                            toward > 0.0 ? std::max(pressY - px, startEdge) : std::min(pressY + px, startEdge);
                        b = toStart.moveTo(y, false);
                        const double f = midFractionForTension(b.tension);
                        if (b.hold) {
                            held = true;
                            EXPECT_EQ(b.tension, 1.0f) << where << ": a hold is the steepest curve made a step";
                        } else {
                            EXPECT_FALSE(held) << where << ": back out of the hold on the way up";
                            EXPECT_LE(f, last + 1e-12) << where << " px " << px;
                            if (f > minMidFraction() + 2e-3)
                                EXPECT_LT(f, last) << where << " px " << px << ": the curve stood still";
                        }
                        EXPECT_GE(b.tension, -1.0f);
                        EXPECT_LE(b.tension, 1.0f);
                        last = f;
                    }
                    if (roomStart >= 1.0)
                        EXPECT_TRUE(held) << where << ": the hold is reached by the lane's edge";
                    // Back to the press row: the press shape again, whatever the way there was.
                    const auto home = toStart.moveTo(pressY, false);
                    EXPECT_NEAR(midFractionForTension(home.tension), f0, 1e-4) << where;
                    EXPECT_FALSE(home.hold) << where;
                }
        }
}
