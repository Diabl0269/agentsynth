// SegmentBendDragTests.cpp -- the pure maths of dragging the segment between two dots (SegmentBendDrag.h): which way a
// bend turns, the lane's half-way fraction and tension range (+-3), and the absolute drag: the curve passes under the
// pointer on every pixel, bends into the step with no wall, and holds from the start row outward.
#include "Timeline/AutomationKernel.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include <cmath>
#include <gtest/gtest.h>
#include <string>

using namespace synth::ui;

namespace {
constexpr double kT = (double)synth::kMaxLaneTension;
constexpr double kBaseY = 500.0;

double gammaOf(double t) { return std::exp2(2.0 * t); }

struct Seg {
    double startY;
    double endY;
    double span;
    double sign; // +1 when the end row is below the start row (a falling segment)
    double curveRow(double tension, double xp) const {
        return startY + (endY - startY) * std::pow(xp, gammaOf(tension));
    }
};

Seg makeSeg(bool rising, double span) {
    return rising ? Seg{kBaseY, kBaseY - span, span, -1.0} : Seg{kBaseY, kBaseY + span, span, 1.0};
}
} // namespace

TEST(SegmentBendDrag, UpwardSignFlipsForRisingAndFalling) {
    EXPECT_EQ(upwardBendSign(0.2, 0.8), -1.0f);
    EXPECT_EQ(upwardBendSign(0.8, 0.2), 1.0f);
}

TEST(SegmentBendDrag, MidFractionAndTensionRoundTripAcrossTheWholeRange) {
    EXPECT_NEAR(midFractionForTension(0.0f), 0.5, 1e-12);
    EXPECT_NEAR(minMidFraction(), std::pow(0.5, 64.0), 1e-30);
    EXPECT_NEAR(maxMidFraction(), std::pow(0.5, 1.0 / 64.0), 1e-12);
    for (float t = -2.5f; t <= 2.5f; t += 0.125f)
        EXPECT_NEAR(tensionForMidFraction(midFractionForTension(t)), t, 1e-4f) << t;
}

TEST(SegmentBendDrag, TensionForMidFractionClampsToTheLaneRange) {
    EXPECT_EQ(tensionForMidFraction(1e-30), synth::kMaxLaneTension);
    EXPECT_EQ(tensionForMidFraction(-3.0), synth::kMaxLaneTension);
    EXPECT_EQ(tensionForMidFraction(0.9999), -synth::kMaxLaneTension);
}

TEST(SegmentBendDrag, AFlatSegmentKeepsItsShape) {
    LaneBendDrag drag(50.0, 50.2, 0.3f, false, 0.5, 50.0, 0.0, 200.0);
    const auto bend = drag.moveTo(120.0, false);
    EXPECT_EQ(bend.tension, 0.3f);
    EXPECT_FALSE(bend.hold);
    LaneBendDrag held(50.0, 50.0, 0.3f, true, 0.5, 50.0, 0.0, 200.0);
    EXPECT_TRUE(held.moveTo(10.0, false).hold);
}

// A press never jumps the curve, and a hold keeps the tension it had until it is dragged out of its zone.
TEST(SegmentBendDrag, ThePressLeavesTheCurveAndAHoldKeepsItsTension) {
    const Seg s = makeSeg(false, 120.0);
    const double press = s.curveRow(0.7, 0.5);
    LaneBendDrag drag(s.startY, s.endY, 0.7f, false, 0.5, press, 0.0, 1000.0);
    const auto same = drag.moveTo(press, false);
    EXPECT_NEAR(same.tension, 0.7f, 1e-4f);
    EXPECT_FALSE(same.hold);

    LaneBendDrag held(s.startY, s.endY, 0.4f, true, 0.5, s.startY, 0.0, 1000.0);
    const auto still = held.moveTo(s.startY + 1.0, false);
    EXPECT_TRUE(still.hold) << "inside the 2 px exit";
    EXPECT_EQ(still.tension, 0.4f);
    const auto out = held.moveTo(s.startY + kHoldExitPx + 1.0, false);
    EXPECT_FALSE(out.hold);
}

// The sweep: rising and falling segments of several heights, pressed at several beats with several starting tensions,
// the pointer walked a pixel at a time from the press to past the start row, to past the end row and all the way back.
TEST(SegmentBendDrag, TheCurveFollowsThePointerEveryPixelAndHoldsFromTheStartRowOutward) {
    int checkedRows = 0;
    for (const bool rising : {true, false})
        for (const double span : {40.0, 120.0, 300.0})
            for (const double xp : {0.2, 0.5, 0.8})
                for (const float t0 : {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f}) {
                    const Seg s = makeSeg(rising, span);
                    const double press = s.curveRow(t0, xp);
                    const std::string where = std::string(rising ? "rising " : "falling ") + std::to_string(span) +
                                              "px xp " + std::to_string(xp) + " t0 " + std::to_string(t0);
                    LaneBendDrag drag(s.startY, s.endY, t0, false, xp, press, 0.0, 2000.0);
                    const double zone = std::min(kStepZonePx, span / 4.0);

                    bool expectHold = false;
                    double lastY = press;
                    float prevT = t0;
                    bool prevValid = true;
                    bool prevHold = false;
                    int prevDir = 0;
                    const auto visit = [&](double y) {
                        const auto bend = drag.moveTo(y, false);
                        const double along = (y - s.startY) * s.sign;
                        if (along <= 0.0)
                            expectHold = true;
                        else if (along > kHoldExitPx)
                            expectHold = false;
                        ASSERT_EQ(bend.hold, expectHold) << where << " row " << y << " along " << along;
                        const int dir = y > lastY ? 1 : (y < lastY ? -1 : 0);
                        if (bend.hold) {
                            if (!prevHold)
                                EXPECT_EQ(bend.tension, synth::kMaxLaneTension) << where << ": no jump into the hold";
                            prevValid = false;
                        } else {
                            const bool clamped = std::abs(bend.tension) >= (float)kT - 1e-4f;
                            if (along >= zone && along < span && !clamped) {
                                // (a) the curve at the press beat is where the pointer is.
                                EXPECT_NEAR(s.curveRow(bend.tension, xp), y, 1.0) << where << " row " << y;
                                ++checkedRows;
                            }
                            // (b) every pixel changes the tension, toward the end value less, toward the start more.
                            const bool prevClamped = std::abs(prevT) >= (float)kT - 1e-4f;
                            if (prevValid && !clamped && !prevClamped && dir == prevDir && dir != 0) {
                                const double towardEnd = dir * s.sign;
                                if (towardEnd > 0.0)
                                    EXPECT_LT(bend.tension, prevT) << where << " row " << y;
                                else
                                    EXPECT_GT(bend.tension, prevT) << where << " row " << y;
                            }
                            prevT = bend.tension;
                            prevValid = true;
                        }
                        prevHold = bend.hold;
                        prevDir = dir;
                        lastY = y;
                    };
                    const auto walkTo = [&](double target) {
                        const double dir = target >= lastY ? 1.0 : -1.0;
                        while (std::abs(target - lastY) > 1.0)
                            visit(lastY + dir);
                        visit(target);
                    };
                    walkTo(s.startY - s.sign * 6.0); // out past the start row: a hold
                    walkTo(s.endY + s.sign * 6.0);   // the whole way to past the end row
                    walkTo(s.startY - s.sign * 6.0); // and all the way back
                    walkTo(press);
                }
    EXPECT_GT(checkedRows, 1000) << "the sweep checked real rows";
}

// The curve never passes the end row: past it the tension rests at the limit, and the pointer coming back retraces.
TEST(SegmentBendDrag, PastTheEndRowTheCurveStaysAtItsLimit) {
    const Seg s = makeSeg(false, 100.0);
    LaneBendDrag drag(s.startY, s.endY, 0.0f, false, 0.5, s.curveRow(0.0, 0.5), 0.0, 2000.0);
    EXPECT_EQ(drag.moveTo(s.endY, false).tension, -synth::kMaxLaneTension);
    EXPECT_EQ(drag.moveTo(s.endY + 80.0, false).tension, -synth::kMaxLaneTension);
    const auto back = drag.moveTo(s.curveRow(0.0, 0.5), false);
    EXPECT_NEAR(back.tension, 0.0f, 1e-4f);
}

// The step zone is continuous with the formula at its edge and reaches the maximum at the start row.
TEST(SegmentBendDrag, TheStepZoneIsContinuousAndReachesTheMaximumAtTheStartRow) {
    const Seg s = makeSeg(false, 200.0);
    LaneBendDrag drag(s.startY, s.endY, 0.0f, false, 0.5, s.curveRow(0.0, 0.5), 0.0, 2000.0);
    const double zone = kStepZonePx;
    const float atEdge = drag.moveTo(s.startY + zone + 1e-6, false).tension;
    const float inside = drag.moveTo(s.startY + zone - 1e-6, false).tension;
    EXPECT_NEAR(atEdge, inside, 1e-4f) << "no wall at the edge of the zone";
    EXPECT_NEAR(drag.moveTo(s.startY + 0.01, false).tension, synth::kMaxLaneTension, 0.05f);
    EXPECT_TRUE(drag.moveTo(s.startY, false).hold);
}

// (e) Shift: the pointer's travel counts a tenth.
TEST(SegmentBendDrag, ShiftCountsATenthOfTheTravel) {
    const Seg s = makeSeg(true, 120.0);
    const double press = s.curveRow(0.0, 0.5);
    LaneBendDrag fine(s.startY, s.endY, 0.0f, false, 0.5, press, 0.0, 2000.0);
    LaneBendDrag plain(s.startY, s.endY, 0.0f, false, 0.5, press, 0.0, 2000.0);
    LaneBend a;
    for (int i = 1; i <= 100; ++i)
        a = fine.moveTo(press - i, true);
    const auto b = plain.moveTo(press - 10.0, false);
    EXPECT_NEAR(a.tension, b.tension, 1e-4f);
}
