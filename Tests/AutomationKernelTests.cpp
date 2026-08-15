// Tests for TL4-1: the audio-thread automation evaluation kernel.
//
// Two separate things are pinned here. First the CONTRACT — endpoint clamping, Hold vs
// tension-shaped Linear, the reserved-curve fallback — because TL4-2's applier and everything after
// it read a lane's value through this function and nothing else. Second the CURSOR, which is pure
// optimisation and therefore pure risk: a cache that drifts from the truth is a wrong parameter
// value with no visible symptom. MonotonicSweepMatchesRandomAccess is the tripwire for that — every
// warm-cursor result is compared bit-for-bit against a cold-cursor (binary-search) evaluation of
// the same beat, so the fast path is not allowed to differ from the slow one by even one ulp.
//
// Everything here builds TimelineSnapshot::Point runs directly rather than going through
// TimelineDoc: the kernel's input is the flattened run, and the degenerate cases it must survive
// (duplicate beats, denormal-scale gaps, out-of-range tension) are ones the doc's validation would
// never let through — which is exactly why the kernel has to defend against them itself.

#include "Timeline/AutomationKernel.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using synth::AutomationCursor;
using synth::AutomationKernel;
using synth::BreakpointCurve;
using synth::TimelineSnapshot;

namespace {

constexpr int kHold = static_cast<int>(BreakpointCurve::Hold);
constexpr int kLinear = static_cast<int>(BreakpointCurve::Linear);
constexpr int kBezier = static_cast<int>(BreakpointCurve::Bezier);

constexpr double kFallback = -999.0;

TimelineSnapshot::Point pt(double beat, double value, float tension = 0.0f, int curve = kLinear) {
    TimelineSnapshot::Point p;
    p.beat = beat;
    p.value = value;
    p.tension = tension;
    p.curve = curve;
    return p;
}

// A cold evaluation: a fresh cursor forces the binary-search path every time.
double evalCold(const std::vector<TimelineSnapshot::Point>& points, double beat, double fallback = kFallback) {
    AutomationCursor cursor;
    return AutomationKernel::evaluate(points.data(), static_cast<int>(points.size()), beat, fallback, cursor);
}

double evalWith(AutomationCursor& cursor, const std::vector<TimelineSnapshot::Point>& points, double beat,
                double fallback = kFallback) {
    return AutomationKernel::evaluate(points.data(), static_cast<int>(points.size()), beat, fallback, cursor);
}

} // namespace

// ============================================================================
// Contract: empty runs and endpoints
// ============================================================================

TEST(AutomationKernelTest, EmptyRunReturnsFallback) {
    AutomationCursor cursor;

    // No points at all, and a null run — both are "this lane has nothing to say".
    EXPECT_DOUBLE_EQ(AutomationKernel::evaluate(nullptr, 0, 12.5, 0.75, cursor), 0.75);

    const std::vector<TimelineSnapshot::Point> points{pt(0.0, 1.0), pt(4.0, 2.0)};
    EXPECT_DOUBLE_EQ(AutomationKernel::evaluate(points.data(), 0, 2.0, 0.75, cursor), 0.75);
    EXPECT_DOUBLE_EQ(AutomationKernel::evaluate(points.data(), -3, 2.0, 0.75, cursor), 0.75);

    // A non-null run with points still ignores the fallback entirely.
    EXPECT_DOUBLE_EQ(evalCold(points, 2.0), 1.5);
}

TEST(AutomationKernelTest, BeforeFirstAndAfterLastClampToEndpoints) {
    const std::vector<TimelineSnapshot::Point> points{pt(4.0, 10.0), pt(8.0, 20.0), pt(12.0, 30.0)};

    // No extrapolation in either direction — a lane is flat outside its own span.
    EXPECT_DOUBLE_EQ(evalCold(points, -1000.0), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 3.999), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 4.0), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 12.0), 30.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 12.001), 30.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 1.0e9), 30.0);

    EXPECT_DOUBLE_EQ(evalCold(points, -std::numeric_limits<double>::infinity()), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(points, std::numeric_limits<double>::infinity()), 30.0);
    EXPECT_DOUBLE_EQ(evalCold(points, std::numeric_limits<double>::quiet_NaN()), 10.0);

    // A single point is the degenerate case of the same rule: constant everywhere.
    const std::vector<TimelineSnapshot::Point> one{pt(6.0, 7.0)};
    EXPECT_DOUBLE_EQ(evalCold(one, 0.0), 7.0);
    EXPECT_DOUBLE_EQ(evalCold(one, 6.0), 7.0);
    EXPECT_DOUBLE_EQ(evalCold(one, 99.0), 7.0);
}

// ============================================================================
// Contract: Linear, Hold, tension, reserved curves
// ============================================================================

TEST(AutomationKernelTest, LinearInterpolationExactMidpoints) {
    const std::vector<TimelineSnapshot::Point> points{pt(0.0, 0.0), pt(4.0, 100.0)};

    EXPECT_DOUBLE_EQ(evalCold(points, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 1.0), 25.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 2.0), 50.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 3.0), 75.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 4.0), 100.0);

    // A segment that neither starts at 0 nor spans a power of two, and a descending one.
    const std::vector<TimelineSnapshot::Point> offset{pt(2.0, 10.0), pt(6.0, 30.0), pt(10.0, -10.0)};
    EXPECT_DOUBLE_EQ(evalCold(offset, 3.0), 15.0);
    EXPECT_DOUBLE_EQ(evalCold(offset, 4.0), 20.0);
    EXPECT_DOUBLE_EQ(evalCold(offset, 5.0), 25.0);
    EXPECT_DOUBLE_EQ(evalCold(offset, 6.0), 30.0);
    EXPECT_DOUBLE_EQ(evalCold(offset, 8.0), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(offset, 9.0), 0.0);
}

TEST(AutomationKernelTest, HoldSegmentsStepAtBoundaries) {
    const std::vector<TimelineSnapshot::Point> points{pt(0.0, 10.0, 0.0f, kHold), pt(2.0, 20.0, 0.0f, kHold),
                                                      pt(4.0, 30.0, 0.0f, kLinear), pt(6.0, 50.0, 0.0f, kLinear)};

    // The left value holds up to but NOT including the next beat.
    EXPECT_DOUBLE_EQ(evalCold(points, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 1.0), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 1.9999999), 10.0);

    // The value exactly AT a breakpoint is that breakpoint's own value, Hold or not.
    EXPECT_DOUBLE_EQ(evalCold(points, 2.0), 20.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 3.5), 20.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 4.0), 30.0);

    // A Hold point's tension is ignored outright, and a Linear segment following a Hold one
    // interpolates normally.
    EXPECT_DOUBLE_EQ(evalCold(points, 5.0), 40.0);
    EXPECT_DOUBLE_EQ(evalCold(points, 6.0), 50.0);

    const std::vector<TimelineSnapshot::Point> tensioned{pt(0.0, 10.0, 1.0f, kHold), pt(2.0, 20.0, -1.0f, kHold),
                                                         pt(4.0, 30.0, 0.0f, kHold)};
    EXPECT_DOUBLE_EQ(evalCold(tensioned, 1.0), 10.0);
    EXPECT_DOUBLE_EQ(evalCold(tensioned, 3.0), 20.0);
    EXPECT_DOUBLE_EQ(evalCold(tensioned, 4.0), 30.0);
}

TEST(AutomationKernelTest, TensionShaping) {
    // gamma = exp2(2 * tension): +1 -> x^4 (slow start), -1 -> x^0.25 (fast start), 0 -> lerp.
    const std::vector<TimelineSnapshot::Point> slow{pt(0.0, 0.0, 1.0f, kLinear), pt(1.0, 100.0, 0.0f, kLinear)};
    const std::vector<TimelineSnapshot::Point> fast{pt(0.0, 0.0, -1.0f, kLinear), pt(1.0, 100.0, 0.0f, kLinear)};

    EXPECT_DOUBLE_EQ(evalCold(slow, 0.5), 100.0 * 0.0625); // 0.5^4
    EXPECT_NEAR(evalCold(fast, 0.5), 100.0 * 0.8408964152537145, 1.0e-12);

    // Half tension is the geometric middle of the two: gamma = exp2(1) = 2.
    const std::vector<TimelineSnapshot::Point> half{pt(0.0, 0.0, 0.5f, kLinear), pt(1.0, 100.0, 0.0f, kLinear)};
    EXPECT_DOUBLE_EQ(evalCold(half, 0.5), 25.0); // 0.5^2

    // Endpoints stay exact for every tension, including at an INTERIOR breakpoint (where the beat
    // opens the next segment rather than closing the previous one).
    const std::vector<TimelineSnapshot::Point> mixed{pt(0.0, 5.0, 1.0f, kLinear), pt(2.0, 55.0, -1.0f, kLinear),
                                                     pt(4.0, -20.0, 0.0f, kLinear)};
    EXPECT_DOUBLE_EQ(evalCold(mixed, 0.0), 5.0);
    EXPECT_DOUBLE_EQ(evalCold(mixed, 2.0), 55.0);
    EXPECT_DOUBLE_EQ(evalCold(mixed, 4.0), -20.0);

    // Monotonicity: shaping bends the ramp, it never makes it turn around.
    for (const auto* run : {&slow, &fast}) {
        double previous = evalCold(*run, 0.0);
        for (int i = 1; i <= 400; ++i) {
            const double beat = static_cast<double>(i) / 400.0;
            const double value = evalCold(*run, beat);
            ASSERT_GE(value, previous) << "beat " << beat;
            previous = value;
        }
    }

    // ...including on a descending segment, where the same gamma has to bend the ramp downwards.
    double previous = evalCold(mixed, 2.0);
    for (int i = 1; i <= 400; ++i) {
        const double beat = 2.0 + 2.0 * static_cast<double>(i) / 400.0;
        const double value = evalCold(mixed, beat);
        ASSERT_LE(value, previous) << "beat " << beat;
        previous = value;
    }

    // Out-of-range tension is clamped rather than fed to std::pow.
    const std::vector<TimelineSnapshot::Point> wild{pt(0.0, 0.0, 40.0f, kLinear), pt(1.0, 100.0, 0.0f, kLinear)};
    EXPECT_DOUBLE_EQ(evalCold(wild, 0.5), evalCold(slow, 0.5));
}

TEST(AutomationKernelTest, ReservedCurveFallsBackToLinear) {
    // Bezier (2) has no evaluator yet, and a future build may write curve numbers this one has
    // never heard of. Both play as Linear — a sane approximation beats a silent or stepped lane.
    const std::vector<TimelineSnapshot::Point> bezier{pt(0.0, 0.0, 0.0f, kBezier), pt(4.0, 100.0, 0.0f, kBezier)};
    const std::vector<TimelineSnapshot::Point> future{pt(0.0, 0.0, 0.0f, 99), pt(4.0, 100.0, 0.0f, 99)};
    const std::vector<TimelineSnapshot::Point> negative{pt(0.0, 0.0, 0.0f, -7), pt(4.0, 100.0, 0.0f, -7)};

    for (double beat = 0.0; beat <= 4.0; beat += 0.25) {
        const double expected = 25.0 * beat;
        EXPECT_DOUBLE_EQ(evalCold(bezier, beat), expected) << "beat " << beat;
        EXPECT_DOUBLE_EQ(evalCold(future, beat), expected) << "beat " << beat;
        EXPECT_DOUBLE_EQ(evalCold(negative, beat), expected) << "beat " << beat;
    }

    // The fallback is Linear *including* tension shaping — a reserved curve is not a plain lerp
    // that quietly drops the shape the file asked for.
    const std::vector<TimelineSnapshot::Point> shapedReserved{pt(0.0, 0.0, 0.5f, kBezier),
                                                              pt(1.0, 100.0, 0.0f, kBezier)};
    const std::vector<TimelineSnapshot::Point> shapedLinear{pt(0.0, 0.0, 0.5f, kLinear), pt(1.0, 100.0, 0.0f, kLinear)};
    EXPECT_DOUBLE_EQ(evalCold(shapedReserved, 0.5), evalCold(shapedLinear, 0.5));
}

// ============================================================================
// The cursor
// ============================================================================

TEST(AutomationKernelTest, MonotonicSweepMatchesRandomAccess) {
    // 1000 points, mixed curves and tensions, non-uniform spacing — then one warm cursor swept
    // forward across 100k beats, every result compared bit-for-bit against a cold evaluation of the
    // same beat. The cursor is an optimisation; the moment it disagrees with the binary search it
    // is a silent wrong value, so "close enough" is not the assertion.
    std::mt19937 rng(0xA07031u);
    std::uniform_real_distribution<double> gap(0.05, 3.0);
    std::uniform_real_distribution<double> value(-50.0, 50.0);
    std::uniform_real_distribution<float> tension(-1.0f, 1.0f);
    std::uniform_int_distribution<int> curve(0, 2);

    std::vector<TimelineSnapshot::Point> points;
    points.reserve(1000);
    double beat = 0.0;
    for (int i = 0; i < 1000; ++i) {
        points.push_back(pt(beat, value(rng), tension(rng), curve(rng)));
        beat += gap(rng);
    }

    const double firstBeat = points.front().beat;
    const double lastBeat = points.back().beat;
    const double span = lastBeat - firstBeat;

    AutomationCursor cursor;
    constexpr int kSteps = 100000;
    int mismatches = 0;
    for (int i = 0; i <= kSteps; ++i) {
        // Deliberately starts before the run and ends past it, so the clamping branches are swept
        // through with a warm cursor too.
        const double t = firstBeat - 1.0 + (span + 2.0) * static_cast<double>(i) / static_cast<double>(kSteps);
        const double warm = evalWith(cursor, points, t);
        const double cold = evalCold(points, t);
        if (warm != cold) {
            if (++mismatches == 1)
                ADD_FAILURE() << "cursor drifted at step " << i << " beat " << t << ": warm " << warm << " vs cold "
                              << cold;
        }
    }
    EXPECT_EQ(mismatches, 0);

    // The same sweep landing exactly ON every breakpoint (the boundary the cursor has to step
    // across) must return that breakpoint's own value.
    AutomationCursor exact;
    for (std::size_t i = 0; i < points.size(); ++i)
        ASSERT_DOUBLE_EQ(evalWith(exact, points, points[i].beat), points[i].value) << "breakpoint " << i;
}

TEST(AutomationKernelTest, BackwardSeekReSearches) {
    const std::vector<TimelineSnapshot::Point> points{pt(0.0, 0.0),   pt(4.0, 40.0),  pt(8.0, 80.0),
                                                      pt(12.0, 20.0), pt(16.0, 60.0), pt(20.0, 0.0)};

    AutomationCursor cursor;

    // Walk the cursor deep into the run...
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 2.0), 20.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 6.0), 60.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 14.0), 40.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 18.0), 30.0);

    // ...then jump back. The cached segment is now behind the beat, so this has to re-search
    // rather than shape the wrong pair of points.
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 2.0), 20.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 18.0), 30.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 10.0), 50.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, -5.0), 0.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, points, 6.0), 60.0);

    // A forward jump larger than kMaxLinearAdvance segments gives up on walking and takes the same
    // search path — a loop wrap or a playhead drag must not walk half the run.
    std::vector<TimelineSnapshot::Point> longRun;
    longRun.reserve(64);
    for (int i = 0; i < 64; ++i)
        longRun.push_back(pt(static_cast<double>(i), static_cast<double>(i) * 10.0));

    AutomationCursor jumpy;
    EXPECT_DOUBLE_EQ(evalWith(jumpy, longRun, 0.5), 5.0);
    EXPECT_DOUBLE_EQ(evalWith(jumpy, longRun, 50.5), 505.0); // 50 segments forward in one call
    EXPECT_DOUBLE_EQ(evalWith(jumpy, longRun, 51.25), 512.5);
    EXPECT_DOUBLE_EQ(evalWith(jumpy, longRun, 2.5), 25.0); // and all the way back
    EXPECT_DOUBLE_EQ(evalWith(jumpy, longRun, 62.75), 627.5);
}

TEST(AutomationKernelTest, SnapshotSwapResetsViaIdentityCheck) {
    // Same beats, different values: a warm cursor pointing at run A must not be believed when run B
    // is passed in. The caller is expected to reset the cursor on a snapshot swap; this check is
    // what makes forgetting it a lost cache rather than a wrong parameter value.
    const std::vector<TimelineSnapshot::Point> runA{pt(0.0, 0.0), pt(4.0, 100.0), pt(8.0, 0.0)};
    const std::vector<TimelineSnapshot::Point> runB{pt(0.0, 500.0), pt(4.0, 600.0), pt(8.0, 500.0)};

    AutomationCursor cursor;
    EXPECT_DOUBLE_EQ(evalWith(cursor, runA, 2.0), 50.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, runB, 2.0), 550.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, runA, 2.0), 50.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, runB, 6.0), 550.0);
    EXPECT_DOUBLE_EQ(evalWith(cursor, runA, 6.0), 50.0);

    // A run that shrank under a cursor holding an index past its new end must also be caught —
    // the cached index is re-bounds-checked against the run actually passed in, not trusted.
    AutomationCursor deep;
    EXPECT_DOUBLE_EQ(evalWith(deep, runA, 6.0), 50.0);
    EXPECT_DOUBLE_EQ(AutomationKernel::evaluate(runA.data(), 2, 2.0, kFallback, deep), 50.0);
    EXPECT_DOUBLE_EQ(AutomationKernel::evaluate(runA.data(), 1, 2.0, kFallback, deep), 0.0);
}

// ============================================================================
// Degenerate input
// ============================================================================

TEST(AutomationKernelTest, DegenerateSegments) {
    // TimelineDoc keeps breakpoint beats unique, so none of this can arrive through the model —
    // but the kernel reads a flat array and a divide-by-zero here would be a NaN written straight
    // into a live parameter, so it defends itself.
    const std::vector<TimelineSnapshot::Point> duplicated{pt(0.0, 10.0), pt(4.0, 20.0), pt(4.0, 70.0), pt(8.0, 80.0)};

    // At (and after) a duplicated beat the RIGHT point wins: the search picks the rightmost point
    // whose beat is <= the query, so the zero-length segment is never the active one.
    EXPECT_DOUBLE_EQ(evalCold(duplicated, 2.0), 15.0);
    EXPECT_DOUBLE_EQ(evalCold(duplicated, 4.0), 70.0);
    EXPECT_DOUBLE_EQ(evalCold(duplicated, 6.0), 75.0);
    EXPECT_DOUBLE_EQ(evalCold(duplicated, 8.0), 80.0);

    // Three at the same beat, and duplicates at either end of the run.
    const std::vector<TimelineSnapshot::Point> triple{pt(0.0, 1.0), pt(2.0, 2.0), pt(2.0, 3.0), pt(2.0, 4.0),
                                                      pt(6.0, 8.0)};
    EXPECT_DOUBLE_EQ(evalCold(triple, 2.0), 4.0);
    EXPECT_DOUBLE_EQ(evalCold(triple, 4.0), 6.0);

    const std::vector<TimelineSnapshot::Point> duplicatedEnds{pt(1.0, 5.0), pt(1.0, 6.0), pt(3.0, 9.0), pt(3.0, 11.0)};
    EXPECT_TRUE(std::isfinite(evalCold(duplicatedEnds, 1.0)));
    EXPECT_DOUBLE_EQ(evalCold(duplicatedEnds, 3.0), 11.0);
    EXPECT_DOUBLE_EQ(evalCold(duplicatedEnds, 0.0), 5.0);

    // Every point on the same beat: the run has no span at all.
    const std::vector<TimelineSnapshot::Point> allSame{pt(2.0, 1.0), pt(2.0, 2.0), pt(2.0, 3.0)};
    EXPECT_DOUBLE_EQ(evalCold(allSame, 2.0), 3.0);
    EXPECT_DOUBLE_EQ(evalCold(allSame, 0.0), 1.0);

    // A denormal-scale gap: 1/length overflows to infinity, and inf * 0 would be NaN.
    const double tiny = std::numeric_limits<double>::denorm_min();
    const std::vector<TimelineSnapshot::Point> denormal{pt(0.0, 3.0), pt(tiny, 9.0), pt(1.0, 12.0)};
    EXPECT_TRUE(std::isfinite(evalCold(denormal, 0.0)));
    EXPECT_TRUE(std::isfinite(evalCold(denormal, tiny)));
    EXPECT_TRUE(std::isfinite(evalCold(denormal, 0.5)));

    const std::vector<TimelineSnapshot::Point> subnormalSpan{pt(0.0, 3.0, 1.0f), pt(tiny * 4.0, 9.0, -1.0f),
                                                             pt(tiny * 8.0, 12.0)};
    for (double beat : {0.0, tiny, tiny * 2.0, tiny * 6.0, tiny * 8.0, 1.0})
        EXPECT_TRUE(std::isfinite(evalCold(subnormalSpan, beat))) << "beat " << beat;
}

TEST(AutomationKernelTest, FuzzNeverProducesNonFiniteValues) {
    // 10k evaluations over randomly shaped runs — duplicate beats, microscopic and enormous gaps,
    // out-of-range and non-finite tension, unknown curve numbers, NaN/inf query beats. Every result
    // must be a finite number: a NaN escaping here lands directly in an audio parameter.
    std::mt19937 rng(0x51EEDu);
    std::uniform_int_distribution<int> pointCount(1, 24);
    std::uniform_int_distribution<int> gapKind(0, 4);
    std::uniform_real_distribution<double> value(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> curve(-5, 40);
    std::uniform_int_distribution<int> tensionKind(0, 5);
    std::uniform_real_distribution<double> tensionValue(-4.0f, 4.0f);
    std::uniform_int_distribution<int> beatKind(0, 6);
    std::uniform_real_distribution<double> beatPick(-5.0, 25.0);

    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double tiny = std::numeric_limits<double>::denorm_min();

    int evaluations = 0;
    while (evaluations < 10000) {
        const int n = pointCount(rng);
        std::vector<TimelineSnapshot::Point> points;
        points.reserve(static_cast<std::size_t>(n));

        double beat = 0.0;
        for (int i = 0; i < n; ++i) {
            float tension = 0.0f;
            switch (tensionKind(rng)) {
            case 0:
                tension = 0.0f;
                break;
            case 1:
                tension = static_cast<float>(tensionValue(rng));
                break;
            case 2:
                tension = std::numeric_limits<float>::quiet_NaN();
                break;
            case 3:
                tension = std::numeric_limits<float>::infinity();
                break;
            case 4:
                tension = -std::numeric_limits<float>::infinity();
                break;
            default:
                tension = 1.0f;
                break;
            }

            points.push_back(pt(beat, value(rng), tension, curve(rng)));

            switch (gapKind(rng)) {
            case 0:
                break; // duplicate beat
            case 1:
                beat += tiny;
                break;
            case 2:
                beat += 1.0e-9;
                break;
            case 3:
                beat += 1.0;
                break;
            default:
                beat += 1.0e6;
                break;
            }
        }

        AutomationCursor warm;
        for (int q = 0; q < 8 && evaluations < 10000; ++q, ++evaluations) {
            double query = 0.0;
            switch (beatKind(rng)) {
            case 0:
                query = nan;
                break;
            case 1:
                query = inf;
                break;
            case 2:
                query = -inf;
                break;
            case 3:
                query = points.front().beat;
                break;
            case 4:
                query = points.back().beat;
                break;
            case 5:
                query = tiny;
                break;
            default:
                query = beatPick(rng);
                break;
            }

            const double warmValue = evalWith(warm, points, query);
            const double coldValue = evalCold(points, query);
            ASSERT_TRUE(std::isfinite(warmValue)) << "warm eval " << evaluations << " beat " << query;
            ASSERT_TRUE(std::isfinite(coldValue)) << "cold eval " << evaluations << " beat " << query;
            ASSERT_EQ(warmValue, coldValue) << "eval " << evaluations << " beat " << query;
        }
    }

    EXPECT_EQ(evaluations, 10000);
}

// ============================================================================
// Performance tripwire
// ============================================================================

TEST(AutomationKernelTest, PerfSmoke) {
    // Not a benchmark — a tripwire for the cursor silently degrading into a per-call scan of the
    // whole run. A 4k-point run swept by 1M monotonic evaluations measures 15-23 ms in an
    // unoptimised local test build, so the budget is ~10x the observed time: an accidental O(n)
    // per call over 4000 points is three orders of magnitude over it and cannot hide, while a
    // loaded CI box has an order of magnitude of slack and cannot flake.
    constexpr int kNumPoints = 4000;
    constexpr int kNumEvaluations = 1000000;
    constexpr double kBudgetMs = 250.0;

    std::mt19937 rng(0xBEA75u);
    std::uniform_real_distribution<double> gap(0.1, 2.0);
    std::uniform_real_distribution<double> value(-1.0, 1.0);
    std::uniform_real_distribution<float> tension(-1.0f, 1.0f);
    std::uniform_int_distribution<int> curve(0, 1);

    std::vector<TimelineSnapshot::Point> points;
    points.reserve(kNumPoints);
    double beat = 0.0;
    for (int i = 0; i < kNumPoints; ++i) {
        points.push_back(pt(beat, value(rng), tension(rng), curve(rng)));
        beat += gap(rng);
    }

    const double span = points.back().beat - points.front().beat;
    const double step = span / static_cast<double>(kNumEvaluations);

    AutomationCursor cursor;
    double accumulator = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kNumEvaluations; ++i)
        accumulator +=
            AutomationKernel::evaluate(points.data(), kNumPoints, points.front().beat + step * i, 0.0, cursor);
    const auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(std::isfinite(accumulator)); // also stops the loop being optimised away
    EXPECT_LT(elapsedMs, kBudgetMs) << kNumEvaluations << " evaluations over " << kNumPoints << " points took "
                                    << elapsedMs << " ms";
    std::cout << "[ PERF     ] AutomationKernel: " << kNumEvaluations << " evaluations over " << kNumPoints
              << " points in " << elapsedMs << " ms\n";
}
