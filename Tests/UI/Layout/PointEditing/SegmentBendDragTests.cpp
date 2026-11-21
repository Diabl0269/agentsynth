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
    EXPECT_FALSE(laneBendAt(kStartY, kEndY, 0.5, extreme + holdOvershootPx(100.0) - 1.0, 0.0f, false).hold);
    const auto at = laneBendAt(kStartY, kEndY, 0.5, extreme + holdOvershootPx(100.0) - 1.0, 0.0f, false);
    EXPECT_NEAR(at.tension, 1.0f, 1e-6f);
    const auto held = laneBendAt(kStartY, kEndY, 0.5, extreme + holdOvershootPx(100.0) + 1.0, 0.0f, false);
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
        const double over = holdOvershootPx(span);
        const auto shortOfIt = laneBendAt(startY, endY, 0.5, extreme - toward * (over - 2.0), 0.0f, false);
        const auto pastIt = laneBendAt(startY, endY, 0.5, extreme - toward * (over + 2.0), 0.0f, false);
        const auto wrongWay = laneBendAt(startY, endY, 0.5, toward * 5000.0, 0.0f, false);
        EXPECT_FALSE(shortOfIt.hold) << span;
        EXPECT_TRUE(pastIt.hold) << span;
        EXPECT_FALSE(wrongWay.hold) << span;
        EXPECT_EQ(wrongWay.tension, -1.0f) << span;
    }
}

TEST(SegmentBendDrag, ATallSegmentAsksForMoreOvershootThanASmallOne) {
    EXPECT_DOUBLE_EQ(holdOvershootPx(20.0), kHoldOvershootPx);
    EXPECT_DOUBLE_EQ(holdOvershootPx(-400.0), kHoldOvershootShare * 400.0);
    EXPECT_GT(holdOvershootPx(400.0), holdOvershootPx(100.0));
}

TEST(SegmentBendDrag, TheRoomPastTheExtremeCapsTheOvershootButNeverBelowTheMinimum) {
    EXPECT_DOUBLE_EQ(holdOvershootPx(400.0, 30.0), 30.0);
    EXPECT_DOUBLE_EQ(holdOvershootPx(400.0, 1000.0), kHoldOvershootShare * 400.0);
    EXPECT_DOUBLE_EQ(holdOvershootPx(400.0, 2.0), kMinHoldOvershootPx);
    EXPECT_DOUBLE_EQ(holdOvershootPx(400.0, -5.0), kMinHoldOvershootPx);
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
TEST(SegmentBendDrag, FromStraightToHoldTheShapeMovesEveryPixelAndNeverJumps) {
    for (const double span : {-300.0, -100.0, -30.0, 30.0, 100.0, 300.0}) {
        const double startY = 400.0;
        const double endY = startY + span;
        const double toward = std::copysign(1.0, span);
        const double over = holdOvershootPx(span);
        const double travelToHold = (0.5 - minMidFraction()) * std::abs(span) + over;
        double lastShape = -1.0;
        bool held = false;
        for (double px = 0.0; px <= travelToHold + 3.0; px += 1.0) {
            const auto bend = laneBendAt(startY, endY, 0.5, -toward * px, 0.0f, false);
            // One number for where the drawn shape is: the curve's half-way fraction toward the start, then the
            // flattening onto the step.
            const double shape = (0.5 - midFractionForTension(bend.tension)) + bend.toHold;
            if (!held) {
                EXPECT_GT(shape, lastShape) << "span " << span << " px " << px << ": the shape stood still";
                if (lastShape >= 0.0)
                    EXPECT_LE(shape - lastShape, std::max(1.0 / over, 1.0 / std::abs(span)) + 1e-6)
                        << "span " << span << " px " << px << ": the shape jumped";
            }
            if (bend.hold) {
                EXPECT_FLOAT_EQ(bend.toHold, 1.0f) << span;
                EXPECT_GE(px, travelToHold - 1.0) << span;
                held = true;
            }
            lastShape = shape;
        }
        EXPECT_TRUE(held) << span;
    }
}

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

// dead-zone-flip + asymmetric-gesture-zone over the whole lane, the property: for every start row x end row (lane edges
// included) x starting tension, a drag from the press to either edge of the lane moves the drawn shape on every pixel,
// never by more than its share, and reaches the end state at the edge: tension -1 on the end value's side, and the
// hold on the start value's side wherever the lane has room for its overshoot.
TEST(SegmentBendDrag, EveryPixelFromThePressToEitherLaneEdgeMovesTheShape) {
    constexpr double kTop = 0.0;
    constexpr double kBottom = 200.0;
    const double rows[] = {0.0, 1.0, 8.0, 50.0, 100.0, 150.0, 192.0, 199.0};
    for (const double startY : rows)
        for (const double endY : rows) {
            const double span = endY - startY;
            if (std::abs(span) < 4.0)
                continue;
            for (const float tension : {-0.5f, 0.0f, 0.5f}) {
                const double pressY = startY + midFractionForTension(tension) * span;
                const double toward = span > 0.0 ? 1.0 : -1.0;
                const std::string where =
                    std::to_string(startY) + "->" + std::to_string(endY) + " t " + std::to_string(tension);

                // Toward the end value: the half-way fraction grows every pixel until the lane's last row.
                LaneBendDrag toEnd(startY, endY, tension, false, pressY, kTop, kBottom);
                const double endEdge = toward > 0.0 ? kBottom - 1.0 : kTop;
                const double need = (maxMidFraction() - midFractionForTension(tension)) * std::abs(span);
                const double room = std::abs(endEdge - pressY);
                const double maxStep = std::max(1.0, need / std::max(room, 1e-9)) / std::abs(span) + 1e-6;
                double last = midFractionForTension(tension);
                for (double px = 1.0; px <= room; px += 1.0) {
                    const auto bend = toEnd.moveTo(pressY + toward * px, false);
                    const double f = midFractionForTension(bend.tension);
                    EXPECT_FALSE(bend.hold) << where;
                    EXPECT_GT(f, last) << where << " px " << px << ": the curve stood still";
                    EXPECT_LE(f - last, maxStep) << where << " px " << px << ": the curve jumped";
                    last = f;
                }
                EXPECT_NEAR(midFractionForTension(toEnd.moveTo(endEdge, false).tension), maxMidFraction(), 1e-6)
                    << where << ": the limit arrives at the edge";

                // Toward the start value: the curve, then the flattening, every pixel until the hold.
                LaneBendDrag toStart(startY, endY, tension, false, pressY, kTop, kBottom);
                const double over = toStart.overshootPx();
                const double startEdge = toward > 0.0 ? kTop : kBottom - 1.0;
                double lastShape = -1.0;
                bool held = false;
                for (double px = 1.0; px <= std::abs(pressY - startEdge) && !held; px += 1.0) {
                    const auto bend = toStart.moveTo(pressY - toward * px, false);
                    const double shape = (0.5 - midFractionForTension(bend.tension)) + bend.toHold;
                    held = bend.hold;
                    if (!held) {
                        EXPECT_GT(shape, lastShape) << where << " px " << px << ": the shape stood still";
                        if (lastShape >= 0.0)
                            EXPECT_LE(shape - lastShape, std::max(1.0 / over, 1.0 / std::abs(span)) + 1e-6)
                                << where << " px " << px << ": the shape jumped";
                    }
                    lastShape = shape;
                }
                if (holdRoomPx(startY, endY, kTop, kBottom) >= kMinHoldOvershootPx &&
                    std::abs(pressY - startEdge) >= (pressY - (startY + minMidFraction() * span)) * toward + over)
                    EXPECT_TRUE(held) << where << ": the hold is reached inside the lane";
            }
        }
}
