// CustomLfoFromRangeTests.cpp -- the pure math behind a lane's "Create custom LFO": which LFO rate fits a range,
// what wave, level and phase offset reproduce the drawn curve (base + CV), when the item is blocked, and how the lane
// is flattened inside the range without touching a beat outside it.

#include "Modules/LfoRateDivisions.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/CustomLfoFromRange.h"
#include <cmath>
#include <gtest/gtest.h>

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::ui::CustomLfoBlocker;
using synth::ui::CustomLfoPlan;
using synth::ui::planCustomLfoFromRange;

namespace {

constexpr int kHold = static_cast<int>(BreakpointCurve::Hold);
constexpr int kLinear = static_cast<int>(BreakpointCurve::Linear);
constexpr int kBezier = static_cast<int>(BreakpointCurve::Bezier);

AutomationLane makeLane(std::vector<AutomationLane::Breakpoint> points, float lo = 0.0f, float hi = 100.0f) {
    AutomationLane lane;
    lane.range = {lo, hi, lo};
    lane.points = std::move(points);
    return lane;
}

synth::ui::ValueNormaliser straight() {
    return [](double v) { return v / 100.0; };
}

// A skewed parameter range, like a cutoff or a rate knob.
synth::ui::ValueNormaliser skewed() {
    return
        [range = juce::NormalisableRange<double>(0.0, 1000.0, 0.0, 0.3)](double v) { return range.convertTo0to1(v); };
}

double frac(double x) { return x - std::floor(x); }

// The base plus the LFO's CV at `beat`, as the engine would add them, against the lane's own value there.
double worstMiss(const CustomLfoPlan& plan, const AutomationLane& lane, double start, double end,
                 const synth::ui::ValueNormaliser& normalise) {
    double worst = 0.0;
    const int n = 400;
    for (int i = 0; i < n; ++i) {
        const double beat = start + (end - start) * ((double)i + 0.37) / (double)n;
        const double phase = frac(beat / plan.divisionBeats + plan.phaseDegrees / 360.0);
        const double played = normalise(plan.baseValue) + plan.level * plan.wave.evaluate((float)phase);
        EXPECT_NEAR(plan.level * 100.0, std::round(plan.level * 100.0), 1e-6) << "a whole Level step";
        worst = std::max(worst, std::abs(played - normalise(synth::ui::laneValueAt(lane, beat))));
    }
    return worst;
}

// Applies the plan's lane edit to a copy of `lane`, through the doc the app edits with.
AutomationLane flattened(const AutomationLane& lane, const CustomLfoPlan& plan) {
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Midi, "T");
    const auto id = doc.addLane(track, "node", "p", lane.range);
    for (const auto& bp : lane.points)
        doc.addBreakpoint(id, bp.beat, bp.value, bp.tension, bp.curve);
    EXPECT_TRUE(doc.editBreakpoints(id, plan.removeBeats, plan.addPoints));
    return *doc.getLane(id);
}

void expectOutsideUnchanged(const AutomationLane& before, double start, double end, const CustomLfoPlan& plan) {
    const auto after = flattened(before, plan);
    for (double beat = 0.0; beat < 20.0; beat += 0.0731) {
        if (beat > start - 1e-5 && beat < end)
            continue;
        EXPECT_NEAR(synth::ui::laneValueAt(after, beat), synth::ui::laneValueAt(before, beat), 1e-6) << "beat " << beat;
    }
    EXPECT_NEAR(synth::ui::laneValueAt(after, end), synth::ui::laneValueAt(before, end), 1e-6) << "at the range end";
    for (double beat = start; beat < end; beat += 0.0731)
        EXPECT_NEAR(synth::ui::laneValueAt(after, beat), plan.baseValue, 1e-6) << "flat inside, beat " << beat;
}

AutomationLane ramp() { return makeLane({{0.0, 0.0, 0.0f, kLinear}, {4.0, 100.0, 0.0f, kLinear}}); }

} // namespace

TEST(CustomLfoFromRangeTest, OneBarPicksTheOneBarRateAndASimpleRampIsTwoPoints) {
    const auto plan = planCustomLfoFromRange(ramp(), 0.0, 4.0, straight());
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(synth::lfoRateDivisions()[plan.divisionIndex], "1/1");
    ASSERT_EQ(plan.wave.points.size(), 2u);
    EXPECT_FLOAT_EQ(plan.wave.points[0].x, 0.0f);
    EXPECT_FLOAT_EQ(plan.wave.points[1].x, 1.0f);
    EXPECT_NEAR(plan.level, 1.0, 1e-9);
    EXPECT_NEAR(plan.baseValue, 0.0, 1e-9);
    EXPECT_FLOAT_EQ(plan.wave.points[1].y, 1.0f);
    EXPECT_NEAR(worstMiss(plan, ramp(), 0.0, 4.0, straight()), 0.0, 1e-3);
}

TEST(CustomLfoFromRangeTest, ThreeBeatsUseAOneBarCycleAndHoldTheLastValueToItsEnd) {
    const auto lane = makeLane({{0.0, 0.0, 0.0f, kLinear}, {3.0, 60.0, 0.0f, kLinear}});
    const auto plan = planCustomLfoFromRange(lane, 0.0, 3.0, straight());
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(synth::lfoRateDivisions()[plan.divisionIndex], "1/1");
    ASSERT_EQ(plan.wave.points.size(), 3u);
    EXPECT_FLOAT_EQ(plan.wave.points[1].x, 0.75f) << "the drawn part fills three quarters of the cycle";
    EXPECT_FLOAT_EQ(plan.wave.points[2].x, 1.0f);
    EXPECT_FLOAT_EQ(plan.wave.points[2].y, plan.wave.points[1].y) << "held";
    EXPECT_NEAR(worstMiss(plan, lane, 0.0, 3.0, straight()), 0.0, 1e-3);
}

TEST(CustomLfoFromRangeTest, LongerRangesPickTheLongerRatesAndNineBarsIsRefused) {
    const auto lane = makeLane({{0.0, 0.0, 0.0f, kLinear}, {40.0, 100.0, 0.0f, kLinear}});
    EXPECT_EQ(synth::lfoRateDivisions()[planCustomLfoFromRange(lane, 0.0, 8.0, straight()).divisionIndex], "2/1");
    EXPECT_EQ(synth::lfoRateDivisions()[planCustomLfoFromRange(lane, 0.0, 5.0, straight()).divisionIndex], "2/1");
    EXPECT_EQ(synth::lfoRateDivisions()[planCustomLfoFromRange(lane, 0.0, 16.0, straight()).divisionIndex], "4/1");
    const auto eight = planCustomLfoFromRange(lane, 0.0, 32.0, straight());
    ASSERT_TRUE(eight.ok());
    EXPECT_EQ(synth::lfoRateDivisions()[eight.divisionIndex], "8/1");
    EXPECT_EQ(planCustomLfoFromRange(lane, 0.0, 36.0, straight()).blocker, CustomLfoBlocker::TooLong);
}

TEST(CustomLfoFromRangeTest, AShortRangeUsesTheSmallestRateThatHoldsIt) {
    const auto lane = makeLane({{0.0, 0.0, 0.0f, kLinear}, {4.0, 100.0, 0.0f, kLinear}});
    const auto plan = planCustomLfoFromRange(lane, 1.0, 1.5, straight());
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(synth::lfoRateDivisions()[plan.divisionIndex], "1/8");
    EXPECT_NEAR(plan.divisionBeats, 0.5, 1e-9);
}

TEST(CustomLfoFromRangeTest, AFlatOrEmptyRangeIsRefused) {
    const auto flatLane = makeLane({{0.0, 40.0, 0.0f, kLinear}, {8.0, 40.0, 0.0f, kLinear}});
    EXPECT_EQ(planCustomLfoFromRange(flatLane, 1.0, 3.0, straight()).blocker, CustomLfoBlocker::Flat);
    EXPECT_EQ(planCustomLfoFromRange(makeLane({}), 1.0, 3.0, straight()).blocker, CustomLfoBlocker::Flat);
    const auto nearlyFlat = makeLane({{0.0, 40.0, 0.0f, kLinear}, {8.0, 40.005, 0.0f, kLinear}});
    EXPECT_EQ(planCustomLfoFromRange(nearlyFlat, 0.0, 8.0, straight()).blocker, CustomLfoBlocker::Flat);
    EXPECT_EQ(planCustomLfoFromRange(ramp(), 2.0, 2.0, straight()).blocker, CustomLfoBlocker::NoRange);
}

TEST(CustomLfoFromRangeTest, BasePlusTheLfoReproducesALinearLane) {
    const auto lane = makeLane({{0.0, 10.0, 0.0f, kLinear},
                                {1.0, 80.0, 0.0f, kLinear},
                                {2.5, 30.0, 0.0f, kLinear},
                                {4.0, 55.0, 0.0f, kLinear},
                                {6.0, 5.0, 0.0f, kLinear}});
    const auto plan = planCustomLfoFromRange(lane, 0.5, 4.5, straight());
    ASSERT_TRUE(plan.ok());
    EXPECT_NEAR(worstMiss(plan, lane, 0.5, 4.5, straight()), 0.0, 1e-3);
    EXPECT_NEAR(plan.level, 0.5, 1e-9) << "from 30 at beat 2.5 up to 80 at beat 1";
}

TEST(CustomLfoFromRangeTest, HoldStepsBecomeZeroLengthStepsInTheWave) {
    const auto lane = makeLane(
        {{0.0, 20.0, 0.0f, kHold}, {1.0, 70.0, 0.0f, kHold}, {2.0, 40.0, 0.0f, kHold}, {3.0, 90.0, 0.0f, kHold}});
    const auto plan = planCustomLfoFromRange(lane, 0.0, 4.0, straight());
    ASSERT_TRUE(plan.ok());
    int stepsAt = 0;
    for (size_t i = 1; i < plan.wave.points.size(); ++i)
        if (plan.wave.points[i].x == plan.wave.points[i - 1].x)
            ++stepsAt;
    EXPECT_EQ(stepsAt, 3) << "one two-point step at each of beats 1, 2 and 3";
    EXPECT_NEAR(worstMiss(plan, lane, 0.0, 4.0, straight()), 0.0, 1e-3);
}

TEST(CustomLfoFromRangeTest, BezierSegmentsWithTensionAreSampledAndStillReproduce) {
    const auto lane = makeLane({{0.0, 0.0, 0.5f, kBezier}, {2.0, 100.0, 0.5f, kLinear}, {4.0, 20.0, 0.0f, kLinear}});
    const auto plan = planCustomLfoFromRange(lane, 0.0, 4.0, straight());
    ASSERT_TRUE(plan.ok());
    EXPECT_GT(plan.wave.points.size(), 8u) << "a bent segment gets extra readings where it bends";
    EXPECT_LE(plan.wave.points.size(), 64u);
    EXPECT_NEAR(worstMiss(plan, lane, 0.0, 4.0, straight()), 0.0, 1e-3);
}

TEST(CustomLfoFromRangeTest, ASkewedParameterRangeStillReproducesLinearAndHoldLanes) {
    const auto linearLane = makeLane(
        {{0.0, 200.0, 0.0f, kLinear}, {2.0, 800.0, 0.0f, kLinear}, {4.0, 300.0, 0.0f, kLinear}}, 0.0f, 1000.0f);
    const auto linearPlan = planCustomLfoFromRange(linearLane, 0.0, 4.0, skewed());
    ASSERT_TRUE(linearPlan.ok());
    EXPECT_GT(linearPlan.wave.points.size(), 6u) << "a straight segment bends once the range is skewed";
    EXPECT_NEAR(worstMiss(linearPlan, linearLane, 0.0, 4.0, skewed()), 0.0, 1e-3);

    const auto holdLane =
        makeLane({{0.0, 200.0, 0.0f, kHold}, {1.0, 800.0, 0.0f, kHold}, {3.0, 300.0, 0.0f, kHold}}, 0.0f, 1000.0f);
    const auto holdPlan = planCustomLfoFromRange(holdLane, 0.0, 4.0, skewed());
    ASSERT_TRUE(holdPlan.ok());
    EXPECT_LT(holdPlan.wave.points.size(), 10u) << "steps stay steps whatever the skew";
    EXPECT_NEAR(worstMiss(holdPlan, holdLane, 0.0, 4.0, skewed()), 0.0, 1e-3);
    EXPECT_NEAR(holdPlan.level, std::ceil((skewed()(800.0) - skewed()(200.0)) * 100.0) / 100.0, 1e-9);
}

TEST(CustomLfoFromRangeTest, MoreThanSixtyFourPointsAreSampledDownToSixtyFour) {
    std::vector<AutomationLane::Breakpoint> points;
    for (int i = 0; i <= 100; ++i)
        points.push_back({i * 0.04, i % 2 == 0 ? 10.0 : 90.0, 0.0f, kLinear});
    const auto lane = makeLane(points);
    const auto full = planCustomLfoFromRange(lane, 0.0, 4.0, straight());
    ASSERT_TRUE(full.ok());
    EXPECT_EQ(full.wave.points.size(), 64u);
    const auto partial = planCustomLfoFromRange(lane, 0.0, 3.0, straight());
    ASSERT_TRUE(partial.ok());
    EXPECT_EQ(partial.wave.points.size(), 64u) << "63 readings and the point that holds the cycle's tail";
    EXPECT_FLOAT_EQ(partial.wave.points[62].x, 0.75f);
    EXPECT_FLOAT_EQ(partial.wave.points[63].x, 1.0f);
}

TEST(CustomLfoFromRangeTest, ThePhaseOffsetStartsTheCycleAtTheRangeStart) {
    const auto lane = makeLane({{0.0, 0.0, 0.0f, kLinear}, {20.0, 100.0, 0.0f, kLinear}});
    const auto six = planCustomLfoFromRange(lane, 6.0, 10.0, straight());
    ASSERT_TRUE(six.ok());
    EXPECT_NEAR(six.divisionBeats, 4.0, 1e-9);
    EXPECT_NEAR(six.phaseDegrees, 180.0, 1e-6);
    EXPECT_NEAR(worstMiss(six, lane, 6.0, 10.0, straight()), 0.0, 1e-3);
    EXPECT_NEAR(planCustomLfoFromRange(lane, 8.0, 12.0, straight()).phaseDegrees, 0.0, 1e-9) << "on a cycle line";
    EXPECT_NEAR(planCustomLfoFromRange(lane, 5.0, 9.0, straight()).phaseDegrees, 270.0, 1e-6);
}

TEST(CustomLfoFromRangeTest, TheAmountLaneIsOneInsideTheRangeAndZeroOutside) {
    const auto plan = planCustomLfoFromRange(ramp(), 1.0, 3.0, straight());
    ASSERT_EQ(plan.amountPoints.size(), 3u);
    EXPECT_DOUBLE_EQ(plan.amountPoints[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(plan.amountPoints[0].value, 0.0);
    EXPECT_DOUBLE_EQ(plan.amountPoints[1].beat, 1.0);
    EXPECT_DOUBLE_EQ(plan.amountPoints[1].value, 1.0);
    EXPECT_DOUBLE_EQ(plan.amountPoints[2].beat, 3.0);
    EXPECT_DOUBLE_EQ(plan.amountPoints[2].value, 0.0);
    for (const auto& p : plan.amountPoints)
        EXPECT_EQ(p.curve, kHold);

    const auto fromZero = planCustomLfoFromRange(ramp(), 0.0, 2.0, straight());
    ASSERT_EQ(fromZero.amountPoints.size(), 2u) << "no zero point before a range that starts at beat 0";
    EXPECT_DOUBLE_EQ(fromZero.amountPoints[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(fromZero.amountPoints[0].value, 1.0);
}

TEST(CustomLfoFromRangeTest, FlatteningLeavesEveryBeatOutsideTheRangeAsItWasForLinearLanes) {
    const auto lane = makeLane({{0.0, 0.0, 0.0f, kLinear},
                                {2.0, 25.0, 0.0f, kLinear},
                                {4.0, 100.0, 0.0f, kLinear},
                                {7.0, 40.0, 0.0f, kLinear}});
    for (const auto& span :
         {std::pair{1.0, 3.0}, std::pair{2.0, 4.0}, std::pair{0.0, 5.0}, std::pair{4.5, 6.0}, std::pair{3.3, 9.0}}) {
        const auto plan = planCustomLfoFromRange(lane, span.first, span.second, straight());
        ASSERT_TRUE(plan.ok()) << span.first;
        expectOutsideUnchanged(lane, span.first, span.second, plan);
    }
}

TEST(CustomLfoFromRangeTest, FlatteningLeavesEveryBeatOutsideTheRangeAsItWasForHoldLanes) {
    const auto lane = makeLane({{0.0, 10.0, 0.0f, kHold}, {2.0, 60.0, 0.0f, kHold}, {5.0, 30.0, 0.0f, kHold}});
    for (const auto& span : {std::pair{1.0, 3.0}, std::pair{2.0, 6.0}, std::pair{3.0, 6.0}, std::pair{4.0, 8.0}}) {
        const auto plan = planCustomLfoFromRange(lane, span.first, span.second, straight());
        ASSERT_TRUE(plan.ok()) << span.first;
        expectOutsideUnchanged(lane, span.first, span.second, plan);
    }
}

TEST(CustomLfoFromRangeTest, ARangeAtTheStartOfASlopeDoesNotBendTheSlopeBeforeIt) {
    const auto lane = makeLane({{0.0, 0.0, 0.0f, kLinear}, {8.0, 80.0, 0.0f, kLinear}});
    const auto plan = planCustomLfoFromRange(lane, 3.0, 6.0, straight());
    ASSERT_TRUE(plan.ok());
    const auto after = flattened(lane, plan);
    EXPECT_NEAR(synth::ui::laneValueAt(after, 1.5), 15.0, 1e-6);
    EXPECT_NEAR(synth::ui::laneValueAt(after, 2.999), 29.99, 1e-3);
    EXPECT_NEAR(synth::ui::laneValueAt(after, 7.0), 70.0, 1e-6);
}

TEST(CustomLfoFromRangeTest, AFullLaneIsRefusedRatherThanLeftHalfEdited) {
    std::vector<AutomationLane::Breakpoint> points;
    for (int i = 0; i < synth::TimelineDoc::kMaxBreakpointsPerLane; ++i)
        points.push_back({100.0 + i * 0.001, (double)(i % 100), 0.0f, kLinear});
    points.front().beat = 0.0;
    points.front().value = 0.0;
    const auto lane = makeLane(points);
    // The range lies on a long slope with no point in it, so nothing is removed to make room for the 3 new ones.
    const auto plan = planCustomLfoFromRange(lane, 50.0, 52.0, straight());
    EXPECT_EQ(plan.blocker, CustomLfoBlocker::LaneFull);
}
