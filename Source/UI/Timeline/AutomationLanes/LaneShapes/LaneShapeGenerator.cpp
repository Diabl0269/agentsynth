#include "LaneShapeGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kBeatEpsilon = 1e-9;
constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);
constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);

// One breakpoint of a cycle: where in the cycle (0..1, or an absolute beat offset back from the end for
// the saw's top) and what it holds.
struct CyclePoint {
    double fraction = 0.0;
    double value = 0.0;
    int curve = kLinear;
    double beatsBeforeEnd = 0.0; // > 0: placed this far before the cycle's end instead of at `fraction`
};

std::vector<CyclePoint> cyclePointsFor(DrawShape shape, double cycleBeats, double lo, double hi) {
    std::vector<CyclePoint> points;
    switch (shape) {
    case DrawShape::Sine: {
        // Starts at the middle going up: phase 0 of sin().
        const double mid = (lo + hi) * 0.5;
        const double amp = (hi - lo) * 0.5;
        for (int k = 0; k < kSinePointsPerCycle; ++k) {
            const double f = (double)k / (double)kSinePointsPerCycle;
            points.push_back({f, mid + amp * std::sin(kTwoPi * f), kLinear, 0.0});
        }
        break;
    }
    case DrawShape::Triangle:
        points.push_back({0.0, lo, kLinear, 0.0});
        points.push_back({0.5, hi, kLinear, 0.0});
        break;
    case DrawShape::Saw:
        // Beats are unique on a lane, so the drop can't be two points on one beat: the top point holds
        // its value for the last sliver of the cycle and the next cycle's start is the bottom.
        points.push_back({0.0, lo, kLinear, 0.0});
        points.push_back({1.0, hi, kHold, sawDropBeats(cycleBeats)});
        break;
    case DrawShape::Square:
        points.push_back({0.0, hi, kHold, 0.0});
        points.push_back({0.5, lo, kHold, 0.0});
        break;
    case DrawShape::Free:
    case DrawShape::Line:
        break;
    }
    return points;
}

// The shape's value approaching `phase` from the left (phase 1 = the end of a whole cycle), which is
// what the closing point at the span's end must hold so the last stretch reads as the shape.
double valueBefore(DrawShape shape, double phase, double lo, double hi) {
    switch (shape) {
    case DrawShape::Sine:
        return (lo + hi) * 0.5 + (hi - lo) * 0.5 * std::sin(kTwoPi * phase);
    case DrawShape::Triangle:
        return phase <= 0.5 ? lo + (hi - lo) * 2.0 * phase : hi - (hi - lo) * 2.0 * (phase - 0.5);
    case DrawShape::Saw:
        return lo + (hi - lo) * phase;
    case DrawShape::Square:
        return phase <= 0.5 ? hi : lo;
    case DrawShape::Free:
    case DrawShape::Line:
        break;
    }
    return lo;
}

bool validSpan(DrawShape shape, double startBeat, double endBeat, double cycleBeats) {
    return isPeriodicShape(shape) && std::isfinite(startBeat) && std::isfinite(endBeat) && std::isfinite(cycleBeats) &&
           cycleBeats > 0.0 && endBeat - startBeat > kBeatEpsilon;
}

} // namespace

double sawDropBeats(double cycleBeats) noexcept { return std::min(cycleBeats * 0.01, 1.0 / 960.0); }

long long estimateShapePointCount(DrawShape shape, double startBeat, double endBeat, double cycleBeats) {
    if (!validSpan(shape, startBeat, endBeat, cycleBeats))
        return 0;
    const double cycles = std::ceil((endBeat - startBeat) / cycleBeats - kBeatEpsilon);
    if (!std::isfinite(cycles) || cycles > 1e12)
        return std::numeric_limits<long long>::max();
    const long long perCycle = shape == DrawShape::Sine ? kSinePointsPerCycle : 2;
    return (long long)cycles * perCycle + 1;
}

std::vector<synth::AutomationLane::Breakpoint> generateShapePoints(DrawShape shape, double startBeat, double endBeat,
                                                                   double cycleBeats, double lo, double hi) {
    std::vector<synth::AutomationLane::Breakpoint> out;
    if (!validSpan(shape, startBeat, endBeat, cycleBeats))
        return out;

    const auto cycle = cyclePointsFor(shape, cycleBeats, lo, hi);
    for (long long k = 0;; ++k) {
        const double cycleStart = startBeat + (double)k * cycleBeats;
        if (cycleStart >= endBeat - kBeatEpsilon)
            break;
        for (const auto& p : cycle) {
            const double beat = p.beatsBeforeEnd > 0.0 ? cycleStart + cycleBeats - p.beatsBeforeEnd
                                                       : cycleStart + p.fraction * cycleBeats;
            if (beat >= endBeat - kBeatEpsilon)
                break;
            out.push_back({beat, p.value, 0.0f, p.curve});
        }
    }

    double phase = std::fmod((endBeat - startBeat) / cycleBeats, 1.0);
    if (phase < kBeatEpsilon)
        phase = 1.0; // a whole number of cycles: the end is the close of the last one
    out.push_back({endBeat, valueBefore(shape, phase, lo, hi), 0.0f, kLinear});
    return out;
}

} // namespace synth::ui
