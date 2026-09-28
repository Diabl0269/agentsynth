#include "AutomationShapes.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kPi = kTwoPi * 0.5;

// Smallest period this generator treats as real — guards every per-cycle division against a
// near-zero value that slipped past the header's "periodBeats <= 0" fallback as a tiny positive
// float (or was clamped down to that by this file's own coarsening math).
constexpr double kMinPeriodBeats = 1.0 / 960.0;

// The gap a Saw's instantaneous reset is offset by: small enough to read as instant at any zoom
// this app's timeline reaches, large enough that the two points either side of a reset are never
// collapsed by TimelineDoc's "a point at the same beat replaces" insert rule.
constexpr double kSawResetEpsilonBeats = 1.0 / 960.0;

// low/high arrive in GESTURE order (press value, drag value), not sorted — every shape's math
// below wants lo <= hi.
void sortedRange(double a, double b, double& lo, double& hi) {
    lo = std::min(a, b);
    hi = std::max(a, b);
}

// Position within one cycle, wrapped to [0, 1) — 0 at `startBeat` when phase is 0, shifted earlier
// by `phase` radians otherwise (PI is exactly half a cycle: the "start at the other extreme" case
// every kind below uses it for).
double cyclePosition(double beat, double startBeat, double period, double phase) {
    const double u = (beat - startBeat) / period + phase / kTwoPi;
    return u - std::floor(u);
}

double sineValue(double u, double lo, double hi) {
    const double mid = (lo + hi) * 0.5;
    const double amp = (hi - lo) * 0.5;
    return mid - amp * std::cos(kTwoPi * u); // u=0 -> lo, u=0.25 -> mid, u=0.5 -> hi
}

double triangleValue(double u, double lo, double hi) {
    const double ramp = (u < 0.5) ? (2.0 * u) : (2.0 * (1.0 - u)); // 0 -> 1 -> 0 over one cycle
    return lo + (hi - lo) * ramp;
}

// Appends {beat, value, curve}, EXCEPT when the previous point already sits at (within floating
// noise of) `beat` — the cap-driven coarsening below can compute an effective period for which the
// last natural cycle point and the forced end-of-span point land on the same double. Overwriting
// rather than appending keeps every kind's "beats are unique and strictly increasing" contract
// exact rather than "usually, modulo one ULP".
void pushOrReplace(std::vector<AutomationLane::Breakpoint>& points, double beat, double value, int curve) {
    if (!points.empty() && std::abs(points.back().beat - beat) < 1e-9)
        points.back().value = value;
    else
        points.push_back({beat, value, 0.0f, curve});
}

// Sine: dense uniform sampling (Linear points approximating the curve — AutomationKernel has no
// sine evaluator). `kSamplesPerPeriod` matches a real period exactly whenever the cap below doesn't
// engage, which is what makes a quarter-period landmark (mid/lo/hi) fall exactly ON a sample rather
// than needing interpolation to check.
std::vector<AutomationLane::Breakpoint> generateSine(double startBeat, double endBeat, double period, double lo,
                                                     double hi, double phase) {
    constexpr int kSamplesPerPeriod = 16;
    const double span = endBeat - startBeat;
    const double idealStep = period / (double)kSamplesPerPeriod;
    const long idealCount = (long)std::floor(span / idealStep + 1e-9) + 1;
    const int cap = TimelineDoc::kMaxBreakpointsPerLane;
    const int count = (int)std::clamp<long>(idealCount, 2, cap);
    const double step = span / (double)(count - 1);

    std::vector<AutomationLane::Breakpoint> points;
    points.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        const double beat = (i == count - 1) ? endBeat : startBeat + step * (double)i;
        const double u = cyclePosition(beat, startBeat, period, phase);
        points.push_back({beat, sineValue(u, lo, hi), 0.0f, (int)BreakpointCurve::Linear});
    }
    return points;
}

// Triangle: one point per peak/trough — AutomationKernel's own Linear interpolation IS the ramp
// between them, so (unlike Sine) no interior resampling is needed. `half` is coarsened the same
// way Sine's step is when a tiny period would otherwise place more kinks than the cap allows.
std::vector<AutomationLane::Breakpoint> generateTriangle(double startBeat, double endBeat, double period, double lo,
                                                         double hi, double phase) {
    const double span = endBeat - startBeat;
    const int cap = TimelineDoc::kMaxBreakpointsPerLane;
    const long idealKinks = (long)std::floor(span / (period * 0.5) + 1e-9);
    // +2 headroom: the starting point plus the forced endpoint, on top of every interior kink.
    const double half = (idealKinks + 2 > cap) ? (span / (double)(cap - 2)) : (period * 0.5);

    std::vector<AutomationLane::Breakpoint> points;
    const bool startsLow = std::cos(phase) >= 0.0;
    points.push_back({startBeat, startsLow ? lo : hi, 0.0f, (int)BreakpointCurve::Linear});
    bool low = startsLow;
    for (long k = 1; (double)k * half < span && (int)points.size() < cap - 1; ++k) {
        low = !low;
        points.push_back({startBeat + half * (double)k, low ? lo : hi, 0.0f, (int)BreakpointCurve::Linear});
    }
    const double u = cyclePosition(endBeat, startBeat, half * 2.0, startsLow ? 0.0 : kPi);
    pushOrReplace(points, endBeat, triangleValue(u, lo, hi), (int)BreakpointCurve::Linear);
    return points;
}

// Square: Hold points at the same peak/trough beats Triangle uses, so the two share one mental
// model (period honoured, coarsened identically) even though Square holds flat between them instead
// of ramping. The forced endpoint repeats the CURRENT level rather than drawing a fresh one, so
// stopping the drag mid-plateau never inserts a level change that was never drawn.
std::vector<AutomationLane::Breakpoint> generateSquare(double startBeat, double endBeat, double period, double lo,
                                                       double hi, double phase) {
    const double span = endBeat - startBeat;
    const int cap = TimelineDoc::kMaxBreakpointsPerLane;
    const long idealKinks = (long)std::floor(span / (period * 0.5) + 1e-9);
    const double half = (idealKinks + 2 > cap) ? (span / (double)(cap - 2)) : (period * 0.5);

    std::vector<AutomationLane::Breakpoint> points;
    const bool startsLow = std::cos(phase) >= 0.0;
    bool low = startsLow;
    points.push_back({startBeat, low ? lo : hi, 0.0f, (int)BreakpointCurve::Hold});
    for (long k = 1; (double)k * half < span && (int)points.size() < cap - 1; ++k) {
        low = !low;
        points.push_back({startBeat + half * (double)k, low ? lo : hi, 0.0f, (int)BreakpointCurve::Hold});
    }
    pushOrReplace(points, endBeat, low ? lo : hi, (int)BreakpointCurve::Hold);
    return points;
}

// Saw (Up ascends low->high then resets; Down mirrors it): a Linear ramp per cycle plus an
// instantaneous reset (see kSawResetEpsilonBeats). `phase` shifts where in an ALREADY-RUNNING ramp
// the span starts — the very first point may therefore sit mid-ramp rather than at an extreme,
// which is why (unlike Triangle/Square) the reset beats are derived from the continuous position
// function instead of being placed at flat multiples of the period from startBeat.
std::vector<AutomationLane::Breakpoint> generateSaw(double startBeat, double endBeat, double period, double lo,
                                                    double hi, double phase, bool ascending) {
    const double span = endBeat - startBeat;
    const int cap = TimelineDoc::kMaxBreakpointsPerLane;
    // Two points per cycle (pre-reset peak/trough + reset) plus the leading and forced-end points.
    const long idealCycles = (long)std::floor(span / period + 1e-9) + 1;
    const double usePeriod =
        (idealCycles * 2 + 2 > cap) ? std::max(kMinPeriodBeats, (span * 2.0) / (double)(cap - 2)) : period;

    const auto valueAt = [&](double beat) {
        const double u = cyclePosition(beat, startBeat, usePeriod, phase);
        return ascending ? (lo + (hi - lo) * u) : (hi - (hi - lo) * u);
    };

    std::vector<AutomationLane::Breakpoint> points;
    points.push_back({startBeat, valueAt(startBeat), 0.0f, (int)BreakpointCurve::Linear});

    double u0 = phase / kTwoPi;
    u0 -= std::floor(u0);
    // The first reset is wherever `u` next wraps back to 0 — always a FULL cycle after
    // `startBeat` when u0 is 0 (a fresh start), less than that when phase already put the ramp
    // partway through its cycle.
    double boundary = startBeat + (1.0 - u0) * usePeriod;
    while (boundary < endBeat && (int)points.size() < cap - 2) {
        const double preBoundary = boundary - kSawResetEpsilonBeats;
        if (preBoundary > points.back().beat)
            points.push_back({preBoundary, ascending ? hi : lo, 0.0f, (int)BreakpointCurve::Linear});
        points.push_back({boundary, ascending ? lo : hi, 0.0f, (int)BreakpointCurve::Linear});
        boundary += usePeriod;
    }
    // The forced endpoint reads as "the ramp's value on approach to endBeat", never as "just after
    // an instantaneous reset AT endBeat" — cyclePosition's wrap makes those the same computation
    // whenever endBeat lands exactly on a period boundary (u wraps to 0 either way), so that one
    // case is corrected to the top of the ramp explicitly rather than showing a reset that was
    // never drawn.
    double endU = cyclePosition(endBeat, startBeat, usePeriod, phase);
    if (endU == 0.0 && endBeat > startBeat)
        endU = 1.0;
    const double endValue = ascending ? (lo + (hi - lo) * endU) : (hi - (hi - lo) * endU);
    pushOrReplace(points, endBeat, endValue, (int)BreakpointCurve::Linear);
    return points;
}

// Random: sample-and-hold, one value per period. `mix64` (splitmix64's finalizer) gives each
// (seed, cycle index) pair a well-avalanched, order-independent value — the SAME point for the
// same seed and index regardless of what span/period generated it, which is what "deterministic"
// has to mean for this to be testable at all.
std::uint64_t mix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

double unitRandom(std::uint32_t seed, long index) noexcept {
    const std::uint64_t h = mix64((std::uint64_t)seed ^ mix64((std::uint64_t)index));
    return (double)(h >> 11) * (1.0 / 9007199254740992.0); // top 53 bits -> a double in [0, 1)
}

std::vector<AutomationLane::Breakpoint> generateRandom(double startBeat, double endBeat, double period, double lo,
                                                       double hi, std::uint32_t seed) {
    const double span = endBeat - startBeat;
    const int cap = TimelineDoc::kMaxBreakpointsPerLane;
    const double usePeriod = ((long)std::floor(span / period + 1e-9) + 2 > cap) ? (span / (double)(cap - 1)) : period;

    std::vector<AutomationLane::Breakpoint> points;
    double lastValue = lo + (hi - lo) * unitRandom(seed, 0);
    points.push_back({startBeat, lastValue, 0.0f, (int)BreakpointCurve::Hold});
    for (long n = 1;; ++n) {
        const double beat = startBeat + usePeriod * (double)n;
        if (beat >= endBeat || (int)points.size() >= cap - 1)
            break;
        lastValue = lo + (hi - lo) * unitRandom(seed, n);
        points.push_back({beat, lastValue, 0.0f, (int)BreakpointCurve::Hold});
    }
    pushOrReplace(points, endBeat, lastValue, (int)BreakpointCurve::Hold);
    return points;
}

} // namespace

std::vector<AutomationLane::Breakpoint> generateAutomationShape(ShapeKind kind, double startBeat, double endBeat,
                                                                double periodBeats, double lowValue, double highValue,
                                                                double phase, std::uint32_t randomSeed) {
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat) || !(endBeat > startBeat))
        return {};
    if (!std::isfinite(lowValue) || !std::isfinite(highValue))
        return {};
    if (!std::isfinite(phase))
        phase = 0.0;

    double lo = 0.0, hi = 0.0;
    sortedRange(lowValue, highValue, lo, hi);

    const double span = endBeat - startBeat;
    double period = periodBeats;
    if (!std::isfinite(period) || !(period > 0.0))
        period = span; // no usable period -> draw exactly one cycle across the whole span
    period = std::max(period, kMinPeriodBeats);

    std::vector<AutomationLane::Breakpoint> points;
    switch (kind) {
    case ShapeKind::Sine:
        points = generateSine(startBeat, endBeat, period, lo, hi, phase);
        break;
    case ShapeKind::Triangle:
        points = generateTriangle(startBeat, endBeat, period, lo, hi, phase);
        break;
    case ShapeKind::Square:
        points = generateSquare(startBeat, endBeat, period, lo, hi, phase);
        break;
    case ShapeKind::SawUp:
        points = generateSaw(startBeat, endBeat, period, lo, hi, phase, true);
        break;
    case ShapeKind::SawDown:
        points = generateSaw(startBeat, endBeat, period, lo, hi, phase, false);
        break;
    case ShapeKind::Random:
        points = generateRandom(startBeat, endBeat, period, lo, hi, randomSeed);
        break;
    }

    for (auto& p : points)
        p.value = std::clamp(p.value, lo, hi);
    return points;
}

} // namespace synth
