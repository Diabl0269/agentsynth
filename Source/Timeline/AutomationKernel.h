#pragma once

#include "TimelineSnapshot.h"
#include <cmath>
#include <type_traits>
#include <utility>

namespace synth {

// TL4-1: the audio thread's evaluator for ONE automation lane.
//
// Input is a lane's contiguous breakpoint run — `snapshot.points.data() + lane.firstPoint`,
// `lane.numPoints` — sorted by beat, exactly as TimelineSnapshot::buildFrom flattened it. Output is
// the lane's value at a beat, in the lane's own (denormalised) units.
//
// Audio-thread rules, all load-bearing: header-only and `noexcept`, no allocation, no locks, no
// juce::String / juce::var / exceptions, and no branch on anything the message thread can mutate.
// The kernel never reads the doc and never touches the snapshot beyond the run it is handed, so it
// composes with the snapshot exchange's borrowed-for-this-block-only contract without adding rules
// of its own. TL4-2's applier is the consumer: it walks a snapshot's lanes, calls this once per
// lane per block, and pushes the result at the bound parameter.
//
// Semantics (contract — pinned by Tests/AutomationKernelTests.cpp):
//   - numPoints == 0 (or a null run) -> `fallbackValue`. Callers pass the lane's range default.
//   - beat before the first point -> the first point's value; at or after the last -> the last
//     point's value. There is no extrapolation, ever: a lane is flat outside its own span.
//     A NaN beat takes the "before the first point" branch rather than producing a NaN.
//   - A segment's shape comes from its LEFT point (BreakpointCurve's own definition: a breakpoint
//     describes the interpolation from itself to the next one).
//   - Hold (curve 0) -> the left value for the whole segment. The value exactly AT a breakpoint's
//     beat is always that breakpoint's own value, because the beat lands at the start of the
//     segment the breakpoint opens.
//   - Linear (curve 1) -> lerp shaped by tension: value = a + (b - a) * x^gamma, where
//     x = (beat - aBeat) / (bBeat - aBeat) and gamma = exp2(2 * tension). tension -1 -> gamma 0.25
//     (fast start), 0 -> gamma 1 (a plain lerp), +1 -> gamma 4 (slow start). Tension is clamped to
//     [-1, 1] (TimelineDoc clamps on the way in; the kernel re-clamps so a hand-built run can't
//     reach std::pow with a wild exponent). Endpoints stay exact for every tension: 0^g == 0 and
//     1^g == 1.
//   - Any OTHER curve value — Bezier (2), or a number from a future build — evaluates as Linear.
//     Forward compatibility is deliberate: an old build opening a newer file plays a sane
//     approximation instead of a silent or discontinuous lane.
//
// Cost: one std::pow per call inside a tension-shaped segment, a plain lerp when tension == 0
// (branch on the precomputed gamma), and nothing else. gamma is computed ONCE per segment when the
// segment is cached, never per call.
struct AutomationCursor {
    // The run this cursor's cached segment belongs to. evaluate() compares it against the `points`
    // it is handed and re-searches on a mismatch, so a cursor left over from a PREVIOUS snapshot
    // can never be believed against a new array — it degrades to one binary search instead of
    // reading the wrong segment. The caller should still reset the cursor when it swaps snapshots
    // (a fresh `AutomationCursor{}` is the reset); this check is the safety net that makes
    // forgetting a performance bug rather than a correctness one. Pointer identity only — it is a
    // guard, not a proof of content — so `numPoints` is checked alongside it and every cached index
    // is re-bounds-checked against the run actually passed in.
    const void* pointsIdentity = nullptr;
    int numPoints = 0;

    // Caches the active segment so monotonic evaluation is O(1) per call. Value-init = "unknown".
    int segmentIndex = -1; // index of the LEFT point of the cached segment
    double segStartBeat = 0.0;
    double segEndBeat = 0.0;
    double segStartValue = 0.0;
    double segEndValue = 0.0;
    double invLength = 0.0; // 1 / (segEndBeat - segStartBeat), 0 for a degenerate segment
    double gamma = 1.0;     // precomputed tension exponent for the segment
    int curve = 0;
};

static_assert(std::is_trivially_copyable_v<AutomationCursor>,
              "AutomationCursor is audio-thread state — trivially copyable POD only");
static_assert(std::is_standard_layout_v<AutomationCursor>, "AutomationCursor must stay a plain struct");

struct AutomationKernel {
    // How many segments the monotonic fast path will step through before giving up and doing the
    // binary search instead. A playing transport advances a fraction of a segment per block, so it
    // takes the 1-step path; a big forward jump (a loop wrap, a seek) hits the cap and pays one
    // O(log n) search rather than walking half the run. Keeps the worst case O(log n) whatever the
    // caller does with the beat.
    static constexpr int kMaxLinearAdvance = 8;

    // Evaluates the lane at `beat`. See the file-level comment for the full contract.
    static double evaluate(const TimelineSnapshot::Point* points, int numPoints, double beat, double fallbackValue,
                           AutomationCursor& cursor) noexcept {
        if (points == nullptr || numPoints <= 0)
            return fallbackValue;

        const TimelineSnapshot::Point& first = points[0];
        const TimelineSnapshot::Point& last = points[numPoints - 1];

        // `!(beat >= ...)` rather than `beat < ...` so a NaN beat lands here instead of falling
        // into the search with an unordered comparison.
        if (!(beat >= first.beat))
            return first.value;
        if (beat >= last.beat)
            return last.value;

        // From here first.beat <= beat < last.beat, so numPoints >= 2 and whichever segment we
        // select is guaranteed to have a right-hand point.
        if (cursorMatches(cursor, points, numPoints) && beat >= cursor.segStartBeat) {
            if (beat < cursor.segEndBeat)
                return evaluateCached(cursor, beat); // hot path: same segment as last call

            // Monotonic advance: step forward from the cached segment, bounded by kMaxLinearAdvance.
            int index = cursor.segmentIndex;
            for (int steps = 0; steps < kMaxLinearAdvance; ++steps) {
                ++index;
                if (index + 1 >= numPoints)
                    break; // unreachable for a sorted run (beat < last.beat), bounded anyway
                if (beat < points[index + 1].beat) {
                    cacheSegment(cursor, points, numPoints, index);
                    return evaluateCached(cursor, beat);
                }
            }
        }

        // Cold cursor, a beat BEHIND the cached segment, a different run, or a jump too far to
        // walk: one binary search, then re-cache.
        cacheSegment(cursor, points, numPoints, upperBound(points, numPoints, beat) - 1);
        return evaluateCached(cursor, beat);
    }

private:
    static bool cursorMatches(const AutomationCursor& cursor, const TimelineSnapshot::Point* points,
                              int numPoints) noexcept {
        return cursor.pointsIdentity == static_cast<const void*>(points) && cursor.numPoints == numPoints &&
               cursor.segmentIndex >= 0 && cursor.segmentIndex + 1 < numPoints;
    }

    // First index whose beat is strictly greater than `beat`. Called only when
    // points[0].beat <= beat < points[numPoints - 1].beat, so the result is in [1, numPoints - 1]
    // and `result - 1` is always a valid left-hand point — including across duplicate beats, where
    // it picks the RIGHTMOST point at that beat and therefore never selects a zero-length segment.
    static int upperBound(const TimelineSnapshot::Point* points, int numPoints, double beat) noexcept {
        int lo = 0;
        int hi = numPoints;
        while (lo < hi) {
            const int mid = lo + ((hi - lo) >> 1);
            if (points[mid].beat > beat)
                hi = mid;
            else
                lo = mid + 1;
        }
        return lo;
    }

    static void cacheSegment(AutomationCursor& cursor, const TimelineSnapshot::Point* points, int numPoints,
                             int index) noexcept {
        const TimelineSnapshot::Point& a = points[index];
        const TimelineSnapshot::Point& b = points[index + 1];

        cursor.pointsIdentity = static_cast<const void*>(points);
        cursor.numPoints = numPoints;
        cursor.segmentIndex = index;
        cursor.segStartBeat = a.beat;
        cursor.segEndBeat = b.beat;
        cursor.segStartValue = a.value;
        cursor.segEndValue = b.value;
        cursor.curve = a.curve;

        // A zero-length segment cannot happen through the search above, and TimelineDoc keeps
        // breakpoint beats unique anyway — but a denormal-scale gap would make 1/length infinite,
        // and inf * 0 is NaN. Anything that doesn't reciprocate to a finite number is treated as
        // degenerate (invLength 0 -> x pinned at 0 -> the left value).
        const double length = b.beat - a.beat;
        const double inverse = 1.0 / length;
        cursor.invLength = (length > 0.0 && std::isfinite(inverse)) ? inverse : 0.0;

        cursor.gamma = (a.curve == static_cast<int>(BreakpointCurve::Hold)) ? 1.0 : tensionToGamma(a.tension);
    }

    static double evaluateCached(const AutomationCursor& cursor, double beat) noexcept {
        if (cursor.curve == static_cast<int>(BreakpointCurve::Hold))
            return cursor.segStartValue;

        double x = (beat - cursor.segStartBeat) * cursor.invLength;
        if (!(x > 0.0)) // NaN, negative or zero
            x = 0.0;
        else if (x > 1.0)
            x = 1.0;

        const double shaped = (cursor.gamma == 1.0) ? x : std::pow(x, cursor.gamma);
        return cursor.segStartValue + (cursor.segEndValue - cursor.segStartValue) * shaped;
    }

    static double tensionToGamma(float tension) noexcept {
        double t = static_cast<double>(tension);
        if (!(t == t)) // NaN -> unshaped
            return 1.0;
        if (t < -1.0)
            t = -1.0;
        else if (t > 1.0)
            t = 1.0;
        if (t == 0.0)
            return 1.0; // exp2(0) is exactly 1.0 — skip the call and keep the lerp branch cheap
        return std::exp2(2.0 * t);
    }
};

static_assert(noexcept(AutomationKernel::evaluate(std::declval<const TimelineSnapshot::Point*>(), 0, 0.0, 0.0,
                                                  std::declval<AutomationCursor&>())),
              "AutomationKernel::evaluate runs on the audio thread — it must stay noexcept");

} // namespace synth
