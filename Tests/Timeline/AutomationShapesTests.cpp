// AutomationShapesTests.cpp
//
// synth::generateAutomationShape — the Timeline Shape tool's pure waveform generator. Pins the
// contract from AutomationShapes.h: point counts, extremes within range, unique sorted beats, the
// forced endBeat-exactly landmark, period honoured (checked by evaluating the SAME
// AutomationKernel::evaluate the audio thread and the curve canvas read a lane through, at
// quarter-period beats), the kMaxBreakpointsPerLane cap under a huge-span/tiny-period stress case,
// and the zero/negative-span and zero/negative-period edge cases.

#include "Timeline/AutomationKernel.h"
#include "Timeline/AutomationShapes.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using synth::AutomationCursor;
using synth::AutomationKernel;
using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::generateAutomationShape;
using synth::ShapeKind;
using synth::TimelineDoc;
using synth::TimelineSnapshot;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Evaluates `points` through the SAME kernel the audio thread and the curve canvas use, so a
// "period honoured" assertion is checked against playback rather than against this test's own
// re-derivation of the waveform formula.
double evaluateViaKernel(const std::vector<AutomationLane::Breakpoint>& points, double beat, double fallback) {
    std::vector<TimelineSnapshot::Point> pts;
    pts.reserve(points.size());
    for (const auto& bp : points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    AutomationCursor cursor{};
    return AutomationKernel::evaluate(pts.data(), (int)pts.size(), beat, fallback, cursor);
}

bool beatsAreUniqueAndSorted(const std::vector<AutomationLane::Breakpoint>& points) {
    for (size_t i = 1; i < points.size(); ++i)
        if (!(points[i].beat > points[i - 1].beat))
            return false;
    return true;
}

bool allValuesWithinRange(const std::vector<AutomationLane::Breakpoint>& points, double lo, double hi) {
    for (const auto& p : points)
        if (p.value < lo - 1e-9 || p.value > hi + 1e-9)
            return false;
    return true;
}

bool noBezierCurves(const std::vector<AutomationLane::Breakpoint>& points) {
    for (const auto& p : points)
        if (p.curve == (int)BreakpointCurve::Bezier)
            return false;
    return true;
}

} // namespace

// ============================================================================
// Degenerate spans / periods, and low/high ordering
// ============================================================================

TEST(AutomationShapesTest, EmptyWhenEndNotAfterStart) {
    EXPECT_TRUE(generateAutomationShape(ShapeKind::Sine, 4.0, 4.0, 1.0, 0.0, 1.0).empty());
    EXPECT_TRUE(generateAutomationShape(ShapeKind::Sine, 4.0, 2.0, 1.0, 0.0, 1.0).empty());
}

TEST(AutomationShapesTest, EmptyOnNonFiniteBounds) {
    const double nan = std::nan("");
    EXPECT_TRUE(generateAutomationShape(ShapeKind::Square, nan, 4.0, 1.0, 0.0, 1.0).empty());
    EXPECT_TRUE(generateAutomationShape(ShapeKind::Square, 0.0, nan, 1.0, 0.0, 1.0).empty());
    EXPECT_TRUE(generateAutomationShape(ShapeKind::Square, 0.0, 4.0, 1.0, nan, 1.0).empty());
}

TEST(AutomationShapesTest, ZeroOrNegativePeriodFallsBackToOneCycleAcrossTheSpan) {
    for (double period : {0.0, -1.0, -100.0}) {
        const auto points = generateAutomationShape(ShapeKind::Triangle, 0.0, 8.0, period, 0.0, 10.0);
        ASSERT_GE(points.size(), 2u);
        EXPECT_TRUE(beatsAreUniqueAndSorted(points));
        EXPECT_DOUBLE_EQ(points.front().beat, 0.0);
        EXPECT_DOUBLE_EQ(points.back().beat, 8.0);
        EXPECT_TRUE(allValuesWithinRange(points, 0.0, 10.0));
    }
}

TEST(AutomationShapesTest, LowHighOrderDoesNotMatter) {
    const auto a = generateAutomationShape(ShapeKind::Sine, 0.0, 4.0, 4.0, 20.0, 80.0);
    const auto b = generateAutomationShape(ShapeKind::Sine, 0.0, 4.0, 4.0, 80.0, 20.0);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_NEAR(a[i].beat, b[i].beat, 1e-9);
        EXPECT_NEAR(a[i].value, b[i].value, 1e-9);
    }
}

// ============================================================================
// Sine
// ============================================================================

TEST(AutomationShapesTest, SineQuarterPeriodsMatchExpectedValuesViaKernel) {
    const auto points = generateAutomationShape(ShapeKind::Sine, 0.0, 4.0, 4.0, 0.0, 100.0);
    ASSERT_EQ(points.size(), 17u); // 16 samples/period + the inclusive endpoint
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(allValuesWithinRange(points, 0.0, 100.0));
    EXPECT_TRUE(noBezierCurves(points));
    EXPECT_DOUBLE_EQ(points.back().beat, 4.0);

    EXPECT_NEAR(evaluateViaKernel(points, 0.0, -1.0), 0.0, 1e-6);   // start: lo
    EXPECT_NEAR(evaluateViaKernel(points, 1.0, -1.0), 50.0, 1e-6);  // quarter period: mid
    EXPECT_NEAR(evaluateViaKernel(points, 2.0, -1.0), 100.0, 1e-6); // half period: hi
    EXPECT_NEAR(evaluateViaKernel(points, 3.0, -1.0), 50.0, 1e-6);  // three-quarter period: mid
    EXPECT_NEAR(evaluateViaKernel(points, 4.0, -1.0), 0.0, 1e-6);   // full cycle: back to lo
}

TEST(AutomationShapesTest, SinePhasePiStartsAtHigh) {
    const auto points = generateAutomationShape(ShapeKind::Sine, 0.0, 4.0, 4.0, 0.0, 100.0, kPi);
    EXPECT_NEAR(evaluateViaKernel(points, 0.0, -1.0), 100.0, 1e-6);
    EXPECT_NEAR(evaluateViaKernel(points, 2.0, -1.0), 0.0, 1e-6);
}

TEST(AutomationShapesTest, SineCoarsensAndCapsForHugeSpanTinyPeriod) {
    const auto points = generateAutomationShape(ShapeKind::Sine, 0.0, 100000.0, 0.01, 0.0, 1.0);
    EXPECT_LE((int)points.size(), TimelineDoc::kMaxBreakpointsPerLane);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(allValuesWithinRange(points, 0.0, 1.0));
    EXPECT_DOUBLE_EQ(points.back().beat, 100000.0);
}

// ============================================================================
// Triangle
// ============================================================================

TEST(AutomationShapesTest, TrianglePeaksAndTroughsMatchExpectedValuesViaKernel) {
    const auto points = generateAutomationShape(ShapeKind::Triangle, 0.0, 8.0, 4.0, 0.0, 10.0);
    // Two full periods -> kinks at 0, 2, 4, 6, plus the forced endpoint at 8.
    ASSERT_EQ(points.size(), 5u);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(noBezierCurves(points));
    for (const auto& p : points)
        EXPECT_EQ(p.curve, (int)BreakpointCurve::Linear);

    EXPECT_NEAR(evaluateViaKernel(points, 0.0, -1.0), 0.0, 1e-9);
    EXPECT_NEAR(evaluateViaKernel(points, 1.0, -1.0), 5.0, 1e-9);  // quarter period: half-way up the first ramp
    EXPECT_NEAR(evaluateViaKernel(points, 2.0, -1.0), 10.0, 1e-9); // peak
    EXPECT_NEAR(evaluateViaKernel(points, 4.0, -1.0), 0.0, 1e-9);  // trough
    EXPECT_NEAR(evaluateViaKernel(points, 8.0, -1.0), 0.0, 1e-9);
}

TEST(AutomationShapesTest, TrianglePhasePiStartsAtHigh) {
    const auto points = generateAutomationShape(ShapeKind::Triangle, 0.0, 4.0, 4.0, 0.0, 10.0, kPi);
    EXPECT_NEAR(evaluateViaKernel(points, 0.0, -1.0), 10.0, 1e-9);
    EXPECT_NEAR(evaluateViaKernel(points, 2.0, -1.0), 0.0, 1e-9);
    EXPECT_NEAR(evaluateViaKernel(points, 4.0, -1.0), 10.0, 1e-9);
}

TEST(AutomationShapesTest, TriangleCapsForHugeSpanTinyPeriod) {
    const auto points = generateAutomationShape(ShapeKind::Triangle, 0.0, 100000.0, 0.01, 0.0, 1.0);
    EXPECT_LE((int)points.size(), TimelineDoc::kMaxBreakpointsPerLane);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(allValuesWithinRange(points, 0.0, 1.0));
    EXPECT_DOUBLE_EQ(points.back().beat, 100000.0);
}

// ============================================================================
// Square
// ============================================================================

TEST(AutomationShapesTest, SquareHoldsFlatBetweenTransitions) {
    const auto points = generateAutomationShape(ShapeKind::Square, 0.0, 8.0, 4.0, 0.0, 1.0);
    ASSERT_EQ(points.size(), 5u); // 0, 2, 4, 6, and the forced endpoint at 8
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    for (const auto& p : points)
        EXPECT_EQ(p.curve, (int)BreakpointCurve::Hold);

    EXPECT_NEAR(evaluateViaKernel(points, 0.5, -1.0), 0.0, 1e-9);
    EXPECT_NEAR(evaluateViaKernel(points, 2.5, -1.0), 1.0, 1e-9);
    EXPECT_NEAR(evaluateViaKernel(points, 4.5, -1.0), 0.0, 1e-9);
    EXPECT_NEAR(evaluateViaKernel(points, 6.5, -1.0), 1.0, 1e-9);
    EXPECT_DOUBLE_EQ(points.back().beat, 8.0);
    EXPECT_NEAR(points.back().value, 1.0, 1e-9)
        << "forced endpoint repeats the level in force at endBeat, no new transition drawn";
}

TEST(AutomationShapesTest, SquareCapsForHugeSpanTinyPeriod) {
    const auto points = generateAutomationShape(ShapeKind::Square, 0.0, 100000.0, 0.01, 0.0, 1.0);
    EXPECT_LE((int)points.size(), TimelineDoc::kMaxBreakpointsPerLane);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_DOUBLE_EQ(points.back().beat, 100000.0);
}

// ============================================================================
// Saw
// ============================================================================

TEST(AutomationShapesTest, SawUpOneFullPeriodIsOneCleanRamp) {
    const auto points = generateAutomationShape(ShapeKind::SawUp, 0.0, 4.0, 4.0, 0.0, 10.0);
    ASSERT_EQ(points.size(), 2u) << "no reset boundary is crossed within exactly one period";
    EXPECT_NEAR(points.front().value, 0.0, 1e-9);
    EXPECT_NEAR(points.back().value, 10.0, 1e-9);
    EXPECT_DOUBLE_EQ(points.back().beat, 4.0);
}

TEST(AutomationShapesTest, SawUpRampsThenResetsInstantly) {
    const auto points = generateAutomationShape(ShapeKind::SawUp, 0.0, 6.0, 4.0, 0.0, 10.0);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(noBezierCurves(points));
    EXPECT_TRUE(allValuesWithinRange(points, 0.0, 10.0));
    ASSERT_GE(points.size(), 4u); // start, pre-reset peak, reset, forced end
    EXPECT_NEAR(points.front().value, 0.0, 1e-9) << "starts at low";
    EXPECT_DOUBLE_EQ(points.front().beat, 0.0);
    EXPECT_DOUBLE_EQ(points.back().beat, 6.0);

    // The point just before the reset sits at the high extreme; the very next point drops straight
    // back to low, less than the reset epsilon away in beats.
    bool foundResetPair = false;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i].value > 9.9 && points[i + 1].value < 0.1) {
            EXPECT_LT(points[i + 1].beat - points[i].beat, 0.01);
            foundResetPair = true;
        }
    }
    EXPECT_TRUE(foundResetPair) << "expected exactly one reset inside a 1.5-period span";
}

TEST(AutomationShapesTest, SawDownIsTheMirrorOfSawUp) {
    const auto up = generateAutomationShape(ShapeKind::SawUp, 0.0, 4.0, 4.0, 0.0, 10.0);
    const auto down = generateAutomationShape(ShapeKind::SawDown, 0.0, 4.0, 4.0, 0.0, 10.0);
    ASSERT_EQ(up.size(), down.size());
    for (size_t i = 0; i < up.size(); ++i) {
        EXPECT_NEAR(up[i].beat, down[i].beat, 1e-9);
        EXPECT_NEAR(up[i].value, 10.0 - down[i].value, 1e-6);
    }
}

TEST(AutomationShapesTest, SawCapsForHugeSpanTinyPeriod) {
    const auto points = generateAutomationShape(ShapeKind::SawUp, 0.0, 100000.0, 0.01, 0.0, 1.0);
    EXPECT_LE((int)points.size(), TimelineDoc::kMaxBreakpointsPerLane);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(allValuesWithinRange(points, 0.0, 1.0));
    EXPECT_DOUBLE_EQ(points.back().beat, 100000.0);
}

// ============================================================================
// Random
// ============================================================================

TEST(AutomationShapesTest, RandomIsDeterministicGivenTheSameSeed) {
    const auto a = generateAutomationShape(ShapeKind::Random, 0.0, 16.0, 4.0, 0.0, 1.0, 0.0, 42);
    const auto b = generateAutomationShape(ShapeKind::Random, 0.0, 16.0, 4.0, 0.0, 1.0, 0.0, 42);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_DOUBLE_EQ(a[i].beat, b[i].beat);
        EXPECT_DOUBLE_EQ(a[i].value, b[i].value);
    }
}

TEST(AutomationShapesTest, RandomDiffersWithADifferentSeed) {
    const auto a = generateAutomationShape(ShapeKind::Random, 0.0, 16.0, 4.0, 0.0, 1.0, 0.0, 1);
    const auto b = generateAutomationShape(ShapeKind::Random, 0.0, 16.0, 4.0, 0.0, 1.0, 0.0, 2);
    ASSERT_EQ(a.size(), b.size());
    bool anyDiffer = false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i].value - b[i].value) > 1e-9)
            anyDiffer = true;
    EXPECT_TRUE(anyDiffer);
}

TEST(AutomationShapesTest, RandomIsSampleAndHoldWithHoldCurve) {
    const auto points = generateAutomationShape(ShapeKind::Random, 0.0, 16.0, 4.0, 0.0, 1.0, 0.0, 7);
    ASSERT_EQ(points.size(), 5u); // one sample every 4 beats across 16 beats, plus the forced endpoint
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_TRUE(allValuesWithinRange(points, 0.0, 1.0));
    for (const auto& p : points)
        EXPECT_EQ(p.curve, (int)BreakpointCurve::Hold);
    EXPECT_DOUBLE_EQ(points.back().beat, 16.0);
    EXPECT_NEAR(points.back().value, points[3].value, 1e-9)
        << "the forced endpoint holds the last sample rather than drawing a new one";
}

TEST(AutomationShapesTest, RandomCapsForHugeSpanTinyPeriod) {
    const auto points = generateAutomationShape(ShapeKind::Random, 0.0, 100000.0, 0.01, 0.0, 1.0, 0.0, 3);
    EXPECT_LE((int)points.size(), TimelineDoc::kMaxBreakpointsPerLane);
    EXPECT_TRUE(beatsAreUniqueAndSorted(points));
    EXPECT_DOUBLE_EQ(points.back().beat, 100000.0);
}
