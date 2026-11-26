// SegmentBendDragTests.cpp -- the pure maths of dragging the segment between two dots (SegmentBendDrag.h): which way a
// bend turns, the lane's half-way fraction and tension range (+-3), and the absolute drag: the curve passes under the
// pointer on every pixel, bends into the step with no wall, holds from the start row outward and jumps from the end
// row outward, every pixel moving the drawn line by no more than its share.
#include "Timeline/AutomationKernel.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include <cmath>
#include <gtest/gtest.h>
#include <string>
#include <vector>

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
    LaneBendDrag drag(50.0, 50.2, 0.3f, LaneBendKind::Curve, 0.5, 50.0, 0.0, 200.0);
    const auto bend = drag.moveTo(120.0, false);
    EXPECT_EQ(bend.tension, 0.3f);
    EXPECT_FALSE(bend.hold);
    LaneBendDrag held(50.0, 50.0, 0.3f, LaneBendKind::Hold, 0.5, 50.0, 0.0, 200.0);
    EXPECT_TRUE(held.moveTo(10.0, false).hold);
}

// A press never jumps the curve, and a hold keeps the tension it had until it is dragged out of its zone.
TEST(SegmentBendDrag, ThePressLeavesTheCurveAndAHoldKeepsItsTension) {
    const Seg s = makeSeg(false, 120.0);
    const double press = s.curveRow(0.7, 0.5);
    LaneBendDrag drag(s.startY, s.endY, 0.7f, LaneBendKind::Curve, 0.5, press, 0.0, 1000.0);
    const auto same = drag.moveTo(press, false);
    EXPECT_NEAR(same.tension, 0.7f, 1e-4f);
    EXPECT_FALSE(same.hold);

    LaneBendDrag held(s.startY, s.endY, 0.4f, LaneBendKind::Hold, 0.5, s.startY, 0.0, 1000.0);
    const auto still = held.moveTo(s.startY + 1.0, false);
    EXPECT_TRUE(still.hold) << "inside the 2 px exit";
    EXPECT_EQ(still.tension, 0.4f);
    const auto out = held.moveTo(s.startY + kEndRowSlackPx + kHoldExitPx + 1.0, false);
    EXPECT_FALSE(out.hold);
}

// The sweep: rising and falling segments of several heights, pressed at several beats with several starting tensions,
// the pointer walked a pixel at a time from the press to past the start row, to past the end row and all the way back:
// a Hold from the start row outward, a Jump from the end row outward (each left only kHoldExitPx back), and between
// the zones the curve under the pointer.
TEST(SegmentBendDrag, TheCurveFollowsThePointerEveryPixelAndStepsFromEitherEndRowOutward) {
    int checkedRows = 0;
    for (const bool rising : {true, false})
        for (const double span : {40.0, 120.0, 300.0})
            for (const double xp : {0.2, 0.5, 0.8})
                for (const float t0 : {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f}) {
                    const Seg s = makeSeg(rising, span);
                    const double press = s.curveRow(t0, xp);
                    const std::string where = std::string(rising ? "rising " : "falling ") + std::to_string(span) +
                                              "px xp " + std::to_string(xp) + " t0 " + std::to_string(t0);
                    LaneBendDrag drag(s.startY, s.endY, t0, LaneBendKind::Curve, xp, press, 0.0, 2000.0);
                    const double zone = std::min(kStepZonePx, span / 4.0);

                    LaneBendKind expect = LaneBendKind::Curve;
                    double lastY = press;
                    float prevT = t0;
                    bool prevValid = true;
                    int prevDir = 0;
                    const auto visit = [&](double y) {
                        const auto bend = drag.moveTo(y, false);
                        const double along = (y - s.startY) * s.sign;
                        const double toward = (y - lastY) * s.sign;
                        if (along <= 0.0 || (along <= kEndRowSlackPx && toward < 0.0))
                            expect = LaneBendKind::Hold;
                        else if (along >= span || (along >= span - kEndRowSlackPx && toward > 0.0))
                            expect = LaneBendKind::Jump;
                        else if ((expect == LaneBendKind::Hold && along > kEndRowSlackPx + kHoldExitPx) ||
                                 (expect == LaneBendKind::Jump && along < span - kEndRowSlackPx - kHoldExitPx))
                            expect = LaneBendKind::Curve;
                        ASSERT_EQ(bend.hold, expect == LaneBendKind::Hold)
                            << where << " row " << y << " along " << along;
                        ASSERT_EQ(bend.jump, expect == LaneBendKind::Jump)
                            << where << " row " << y << " along " << along;
                        const int dir = y > lastY ? 1 : (y < lastY ? -1 : 0);
                        if (bend.hold || bend.jump) {
                            EXPECT_EQ(bend.tension, bend.hold ? synth::kMaxLaneTension : -synth::kMaxLaneTension)
                                << where << ": a step carries the limit its curve arrived at";
                            prevValid = false;
                        } else {
                            const bool clamped = std::abs(bend.tension) >= (float)kT - 1e-4f;
                            if (along >= zone && along <= span - zone && !clamped) {
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
                    walkTo(s.endY + s.sign * 6.0);   // the whole way to past the end row: a jump
                    walkTo(s.startY - s.sign * 6.0); // and all the way back
                    walkTo(press);
                }
    EXPECT_GT(checkedRows, 1000) << "the sweep checked real rows";
}

// Regression test for FRO2292: past the end row the drag only reached tension -3 (x^(1/64)), which on a log lane drew
// a drop part-way, a flat and a second drop. At the end row the segment is a Jump; coming back retraces the curve.
TEST(SegmentBendDrag, AtTheEndRowTheSegmentIsAJump) {
    const Seg s = makeSeg(false, 100.0);
    LaneBendDrag drag(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, s.curveRow(0.0, 0.5), 0.0, 2000.0);
    EXPECT_FALSE(drag.moveTo(s.endY - 1.5, false).jump) << "a pixel and a half short is still the curve";
    EXPECT_TRUE(drag.moveTo(s.endY, false).jump);
    EXPECT_TRUE(drag.moveTo(s.endY + 80.0, false).jump);
    const auto back = drag.moveTo(s.curveRow(0.0, 0.5), false);
    EXPECT_FALSE(back.jump);
    EXPECT_NEAR(back.tension, 0.0f, 1e-4f);
}

// The end row and the press offset are fractional and pointer rows whole: a pointer on an end row may sit just under a
// pixel short of it.
TEST(SegmentBendDrag, APointerUnderAPixelShortOfAnEndRowIsOnIt) {
    const Seg s = makeSeg(false, 100.0);
    LaneBendDrag drag(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, s.curveRow(0.0, 0.5), 0.0, 2000.0);
    EXPECT_TRUE(drag.moveTo(s.endY - 0.95, false).jump);
    EXPECT_FALSE(drag.moveTo(s.endY - 3.5, false).jump);
    EXPECT_TRUE(drag.moveTo(s.startY + 0.95, false).hold);
    EXPECT_FALSE(drag.moveTo(s.startY + 3.5, false).hold);
    // Pressed within the slack, a move away from the row keeps the curve.
    LaneBendDrag near(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, s.curveRow(0.0, 0.5), 0.0, 2000.0);
    near.moveTo(s.startY + 0.3, false);
    EXPECT_TRUE(near.moveTo(s.startY + 0.3, false).hold) << "standing still on the row keeps its step";
    const double onTheRow = s.curveRow(2.0, 0.5); // a steep curve passes its press beat a sliver under the start row
    LaneBendDrag pressedNear(s.startY, s.endY, 2.0f, LaneBendKind::Curve, 0.5, onTheRow, 0.0, 2000.0);
    EXPECT_FALSE(pressedNear.moveTo(onTheRow + 0.8, false).hold) << "a move away from the row";
}

// The step zone is continuous with the formula at its edge and reaches the maximum at the start row.
TEST(SegmentBendDrag, TheStepZoneIsContinuousAndReachesTheMaximumAtTheStartRow) {
    const Seg s = makeSeg(false, 200.0);
    LaneBendDrag drag(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, s.curveRow(0.0, 0.5), 0.0, 2000.0);
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
    LaneBendDrag fine(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, press, 0.0, 2000.0);
    LaneBendDrag plain(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, press, 0.0, 2000.0);
    LaneBend a;
    for (int i = 1; i <= 100; ++i)
        a = fine.moveTo(press - i, true);
    const auto b = plain.moveTo(press - 10.0, false);
    EXPECT_NEAR(a.tension, b.tension, 1e-4f);
}

namespace {
// The segment as the audio kernel plays it, sampled at the middle of each of 32 beats: 0 is the start value, 1 the end.
std::vector<double> playedShape(const LaneBend& bend) {
    const int curve = bend.hold   ? static_cast<int>(synth::BreakpointCurve::Hold)
                      : bend.jump ? static_cast<int>(synth::BreakpointCurve::Jump)
                                  : static_cast<int>(synth::BreakpointCurve::Linear);
    const synth::TimelineSnapshot::Point pts[] = {{0.0, 0.0, bend.tension, curve}, {32.0, 1.0, 0.0f, 1}};
    std::vector<double> out;
    synth::AutomationCursor cursor{};
    for (int k = 0; k < 32; ++k)
        out.push_back(synth::AutomationKernel::evaluate(pts, 2, k + 0.5, 0.0, cursor));
    return out;
}

// How far the played shape moved between two pointer rows: the mean over the 32 samples, as a fraction of the span.
double shapeMove(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k)
        sum += std::abs(a[k] - b[k]);
    return sum / (double)a.size();
}
} // namespace

// dead-zone-flip, per pixel: from any press to past either end row, one pixel of pointer travel moves the played shape
// by no more than one pixel's share of the step zone (1 / (zone - kEndRowSlackPx - kHoldExitPx) of the span), and the
// pixel that turns the curve into the Hold or the Jump moves it by no more than the limit's sliver (1/65 of the span):
// no blink there. Regression test for FRO2292: the bottom never became a step, so the last pixel could not reach the
// end shape.
TEST(SegmentBendDrag, EveryPixelToEitherEndRowMovesTheShapeByAtMostItsShareAndEntersTheStepSmoothly) {
    for (const bool rising : {true, false})
        for (const double span : {40.0, 120.0, 300.0})
            for (const double xp : {0.1, 0.5, 0.9})
                for (const float t0 : {-2.0f, 0.0f, 2.0f})
                    for (const bool toStart : {true, false}) {
                        const Seg s = makeSeg(rising, span);
                        const double press = s.curveRow(t0, xp);
                        const std::string where = std::string(rising ? "rising " : "falling ") + std::to_string(span) +
                                                  "px xp " + std::to_string(xp) + " t0 " + std::to_string(t0) +
                                                  (toStart ? " to the start" : " to the end");
                        LaneBendDrag drag(s.startY, s.endY, t0, LaneBendKind::Curve, xp, press, 0.0, 2000.0);
                        const double zone = std::min(kStepZonePx, span / 4.0);
                        const double share = 1.0 / (zone - kEndRowSlackPx - kHoldExitPx) + 1e-9;
                        const double target = toStart ? s.startY : s.endY;
                        const double dir = target >= press ? 1.0 : -1.0;
                        auto before = playedShape(drag.moveTo(press, false));
                        bool stepped = false;
                        for (double y = press + dir; dir > 0 ? y <= target + 4.0 * dir : y >= target + 4.0 * dir;
                             y += dir) {
                            const auto bend = drag.moveTo(y, false);
                            const auto now = playedShape(bend);
                            const double moved = shapeMove(before, now);
                            const bool entering = (bend.hold || bend.jump) && !stepped;
                            const double along = (y - s.startY) * s.sign;
                            // Far from the ends the curve follows the pointer at xp, which may move it faster than
                            // the zone's share on a short span; the zones and the step entry are what this guards.
                            const bool inZone = along < zone + 1.0 || along > span - zone - 1.0;
                            if (entering)
                                EXPECT_LE(moved, 1.0 / 65.0 + 1e-6) << where << " row " << y << ": the step blinked";
                            else if (inZone)
                                EXPECT_LE(moved, share) << where << " row " << y;
                            stepped = stepped || bend.hold || bend.jump;
                            before = now;
                        }
                        EXPECT_TRUE(stepped) << where << ": the end row makes the step";
                        const auto last = drag.moveTo(target, false);
                        EXPECT_EQ(last.hold, toStart) << where;
                        EXPECT_EQ(last.jump, !toStart) << where;
                    }
}

// A hand resting on an end row trembles: 30 events of +-2 px around the row keep the one shape it reached (the step's
// hysteresis), and +-3 px only ever shows that end's step or the curve at its limit beside it, never the other end's
// step and never a shape in between.
TEST(SegmentBendDrag, JitterOnAnEndRowKeepsItsShapeAndNeverShowsTheOtherEnd) {
    for (const bool rising : {true, false})
        for (const bool atStart : {true, false})
            for (const double amp : {2.0, 3.0}) {
                const Seg s = makeSeg(rising, 120.0);
                const double press = s.curveRow(0.0, 0.5);
                LaneBendDrag drag(s.startY, s.endY, 0.0f, LaneBendKind::Curve, 0.5, press, 0.0, 2000.0);
                const double row = atStart ? s.startY : s.endY;
                const double dir = row >= press ? 1.0 : -1.0;
                for (double y = press; dir > 0 ? y < row : y > row; y += dir)
                    drag.moveTo(y, false);
                const auto onRow = drag.moveTo(row, false);
                ASSERT_TRUE(atStart ? onRow.hold : onRow.jump);
                int flips = 0;
                bool wasStep = true;
                for (int i = 0; i < 30; ++i) {
                    const double y = row + (i % 2 == 0 ? amp : -amp) * (i % 4 < 2 ? 1.0 : 0.5);
                    const auto bend = drag.moveTo(y, false);
                    const std::string where = std::string(rising ? "rising" : "falling") +
                                              (atStart ? " start" : " end") + " amp " + std::to_string(amp) +
                                              " event " + std::to_string(i);
                    EXPECT_FALSE(atStart ? bend.jump : bend.hold) << where << ": the other end's step";
                    // The curve beside the step is within the limit's sliver plus one pixel's share of it.
                    const double share = 1.0 / (kStepZonePx - kEndRowSlackPx - kHoldExitPx);
                    EXPECT_LE(shapeMove(playedShape(bend), playedShape(onRow)), 1.0 / 65.0 + share + 1e-6)
                        << where << ": a shape far from the step";
                    const bool step = atStart ? bend.hold : bend.jump;
                    flips += step != wasStep ? 1 : 0;
                    wasStep = step;
                }
                if (amp <= kHoldExitPx)
                    EXPECT_EQ(flips, 0) << (rising ? "rising" : "falling") << (atStart ? " start" : " end");
            }
}
