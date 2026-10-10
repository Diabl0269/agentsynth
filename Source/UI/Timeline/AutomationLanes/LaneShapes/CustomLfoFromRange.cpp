// Concern: the pure math behind "Create custom LFO" -- see CustomLfoFromRange.h.
#include "UI/Timeline/AutomationLanes/LaneShapes/CustomLfoFromRange.h"

#include "Modules/LfoRateDivisions.h"
#include "Timeline/AutomationKernel.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {

using Breakpoint = synth::AutomationLane::Breakpoint;
constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);

constexpr double kLeftLimit = 1e-9;     // beats: "just before" a beat
constexpr double kBeatTolerance = 1e-9; // beats: two beats this close are the same beat
constexpr double kPinGap = 1e-6;        // beats: the lane's value is pinned this far before the range starts
constexpr double kStepThreshold = 1e-6; // normalised: a jump at least this tall is a step
constexpr double kFlatHeight = 1e-4;    // normalised: a range that moves less than this is flat
constexpr double kReadTolerance = 1e-4; // normalised: no more readings once the wave strays less than this
constexpr double kLevelStep = 0.01;     // the LFO's Level knob moves in steps of this
constexpr int kMaxWavePoints = synth::LfoCustomWave::kMaxPoints;

// The lane's value as playback computes it (the audio kernel, a fresh cursor per read).
struct LaneCurve {
    explicit LaneCurve(const synth::AutomationLane& lane)
        : fallback(lane.range.defaultValue) {
        points.reserve(lane.points.size());
        for (const auto& bp : lane.points)
            points.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    }
    double at(double beat) const {
        AutomationCursor cursor{};
        return AutomationKernel::evaluate(points.data(), (int)points.size(), std::max(0.0, beat), fallback, cursor);
    }
    std::vector<synth::TimelineSnapshot::Point> points;
    double fallback;
};

struct Knot {
    double beat;
    double value;
};

// The range's breakpoints, with a step (two knots at one beat) wherever the value jumps there.
std::vector<Knot> breakpointKnots(const synth::AutomationLane& lane, const LaneCurve& curve, double start, double end,
                                  const ValueNormaliser& normalise) {
    std::vector<Knot> knots{{start, curve.at(start)}};
    for (const auto& bp : lane.points) {
        if (bp.beat <= start + kBeatTolerance || bp.beat >= end - kBeatTolerance)
            continue;
        const double before = curve.at(bp.beat - kLeftLimit);
        const double after = curve.at(bp.beat);
        if (std::abs(normalise(before) - normalise(after)) > kStepThreshold)
            knots.push_back({bp.beat, before});
        knots.push_back({bp.beat, after});
    }
    knots.push_back({end, curve.at(end - kLeftLimit)});
    return knots;
}

struct Reading {
    double beat;
    double value;
    double norm;
    // The worst way the curve strays from a straight line to the next reading, and where (normalised, and the beat).
    double splitError = 0.0;
    double splitBeat = 0.0;
};

// How far the curve strays, between this reading and the next, from the straight line the wave would draw there.
void measureSplit(std::vector<Reading>& readings, size_t i, const LaneCurve& curve, const ValueNormaliser& normalise) {
    auto& r = readings[i];
    r.splitError = 0.0;
    if (i + 1 >= readings.size() || readings[i + 1].beat - r.beat < kBeatTolerance)
        return; // the last reading, or a step: nothing is drawn between two readings at one beat
    const auto& next = readings[i + 1];
    for (const double q : {0.25, 0.5, 0.75}) {
        const double beat = r.beat + (next.beat - r.beat) * q;
        const double miss = std::abs(normalise(curve.at(beat)) - (r.norm + (next.norm - r.norm) * q));
        if (miss > r.splitError) {
            r.splitError = miss;
            r.splitBeat = beat;
        }
    }
}

// The wave's points: the range's breakpoints (steps kept as steps) when they fit the budget, else just the two ends,
// then more readings wherever the straight line between neighbours strays furthest from the curve, up to `budget`. A
// straight or held lane needs none; a bent segment or a skewed parameter gets them where they bend.
std::vector<Reading> readCurve(const synth::AutomationLane& lane, const LaneCurve& curve, double start, double end,
                               int budget, const ValueNormaliser& normalise) {
    std::vector<Reading> readings;
    auto knots = breakpointKnots(lane, curve, start, end, normalise);
    if ((int)knots.size() > budget) {
        // Too many to draw one by one: read the curve at evenly spaced beats instead (a dense wiggle has no flat
        // stretch for the refinement below to start from).
        knots.clear();
        for (int i = 0; i < budget; ++i) {
            const double beat = start + (end - start) * (double)i / (double)(budget - 1);
            knots.push_back({beat, i == budget - 1 ? curve.at(end - kLeftLimit) : curve.at(beat)});
        }
    }
    for (const auto& k : knots)
        readings.push_back({k.beat, k.value, normalise(k.value)});
    for (size_t i = 0; i < readings.size(); ++i)
        measureSplit(readings, i, curve, normalise);

    while ((int)readings.size() < budget) {
        size_t worst = readings.size();
        double worstError = kReadTolerance;
        for (size_t i = 0; i < readings.size(); ++i)
            if (readings[i].splitError > worstError) {
                worstError = readings[i].splitError;
                worst = i;
            }
        if (worst == readings.size())
            break;
        const double beat = readings[worst].splitBeat;
        const double value = curve.at(beat);
        readings.insert(readings.begin() + (long)worst + 1, {beat, value, normalise(value)});
        measureSplit(readings, worst, curve, normalise);
        measureSplit(readings, worst + 1, curve, normalise);
    }
    return readings;
}

struct Levels {
    double lowNorm = 0.0;
    double highNorm = 0.0;
    double lowValue = 0.0;
};

// The lowest and highest the curve reaches: every reading and every breakpoint inside the range, so a wave too dense to
// be drawn point by point still gets its true extremes.
Levels levelsOf(const std::vector<Reading>& readings, const synth::AutomationLane& lane, double start, double end,
                const ValueNormaliser& normalise) {
    Levels out{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), 0.0};
    std::vector<Reading> all = readings;
    for (const auto& bp : lane.points)
        if (bp.beat > start && bp.beat < end)
            all.push_back({bp.beat, bp.value, normalise(bp.value)});
    for (const auto& k : all) {
        if (k.norm < out.lowNorm) {
            out.lowNorm = k.norm;
            out.lowValue = k.value;
        }
        out.highNorm = std::max(out.highNorm, k.norm);
    }
    return out;
}

// The wave for `readings`: x measured in cycles from the range start, y in 0..1 of the drawn height. A range shorter
// than the cycle holds its last value to the cycle's end, so the wave always spans [0, 1].
synth::LfoCustomWave buildWave(const std::vector<Reading>& readings, double start, double divisionBeats,
                               const Levels& levels, bool needsTail) {
    const double height = levels.highNorm - levels.lowNorm;
    synth::LfoCustomWave wave;
    for (const auto& k : readings) {
        const double x = std::clamp((k.beat - start) / divisionBeats, 0.0, 1.0);
        const double y = std::clamp((k.norm - levels.lowNorm) / height, 0.0, 1.0);
        wave.points.push_back({(float)x, (float)y, 0.0f});
    }
    if (needsTail)
        wave.points.push_back({1.0f, wave.points.back().y, 0.0f});
    else
        wave.points.back().x = 1.0f;
    wave.sanitise();
    return wave;
}

int divisionFor(double rangeBeats) {
    const int count = synth::lfoRateDivisions().size();
    for (int i = 0; i < count; ++i)
        if ((double)synth::lfoRateDivisionBeats(i) >= rangeBeats - 1e-6)
            return i;
    return count - 1;
}

// The lane keeps its value outside [start, end] and plays flat at `base` inside; the point at `end` takes up the
// curve again, in the shape of the segment it sat in. A pin just before `start` keeps a sloped segment arriving at the
// range from bending towards the new flat value.
void planLaneEdit(CustomLfoPlan& plan, const synth::AutomationLane& lane, const LaneCurve& curve, double start,
                  double end) {
    const double pinBeat = start - kPinGap;
    const Breakpoint* before = nullptr; // the last point that stays on the left
    const Breakpoint* sitsIn = nullptr; // the last point strictly before `end`
    const Breakpoint* atEnd = nullptr;  // a point exactly at `end`
    for (const auto& bp : lane.points) {
        if (bp.beat >= pinBeat - kBeatTolerance && bp.beat <= end + kBeatTolerance)
            plan.removeBeats.push_back(bp.beat);
        else if (bp.beat < pinBeat)
            before = &bp;
        if (std::abs(bp.beat - end) <= kBeatTolerance)
            atEnd = &bp;
        else if (bp.beat < end)
            sitsIn = &bp;
    }

    if (pinBeat >= 0.0 && (before == nullptr || before->curve != kHold))
        plan.addPoints.push_back({pinBeat, curve.at(pinBeat), 0.0f, kHold});
    plan.addPoints.push_back({start, plan.baseValue, 0.0f, kHold});
    if (atEnd != nullptr)
        plan.addPoints.push_back(*atEnd);
    else
        plan.addPoints.push_back({end, curve.at(end), sitsIn != nullptr ? sitsIn->tension : 0.0f,
                                  sitsIn != nullptr ? sitsIn->curve : kLinear});
}

void planAmountLane(CustomLfoPlan& plan, double start, double end) {
    if (start > 0.0)
        plan.amountPoints.push_back({0.0, 0.0, 0.0f, kHold});
    plan.amountPoints.push_back({start, 1.0, 0.0f, kHold});
    plan.amountPoints.push_back({end, 0.0, 0.0f, kHold});
}

} // namespace

CustomLfoPlan planCustomLfoFromRange(const synth::AutomationLane& lane, double startBeat, double endBeat,
                                     const ValueNormaliser& normalise) {
    CustomLfoPlan plan;
    const double length = endBeat - startBeat;
    if (!(startBeat >= 0.0) || !(length > 0.0)) {
        plan.blocker = CustomLfoBlocker::NoRange;
        return plan;
    }
    if (length > kMaxCustomLfoBeats + 1e-9) {
        plan.blocker = CustomLfoBlocker::TooLong;
        return plan;
    }

    plan.divisionIndex = divisionFor(length);
    plan.divisionBeats = (double)synth::lfoRateDivisionBeats(plan.divisionIndex);
    const bool needsTail = length / plan.divisionBeats < 1.0 - 1e-6;
    const LaneCurve curve(lane);

    const int budget = kMaxWavePoints - (needsTail ? 1 : 0);
    const auto readings = readCurve(lane, curve, startBeat, endBeat, budget, normalise);
    const auto levels = levelsOf(readings, lane, startBeat, endBeat, normalise);
    if (!(levels.highNorm - levels.lowNorm >= kFlatHeight)) {
        plan.blocker = CustomLfoBlocker::Flat;
        return plan;
    }
    auto wave = buildWave(readings, startBeat, plan.divisionBeats, levels, needsTail);

    // The Level knob snaps to its step, so the knob is set to the next step up and the wave is drawn that much lower:
    // level * wave is still the drawn height.
    const double height = levels.highNorm - levels.lowNorm;
    plan.level = std::min(1.0, std::ceil(height / kLevelStep - 1e-6) * kLevelStep);
    const float shrink = (float)std::min(1.0, height / plan.level);
    for (auto& point : wave.points)
        point.y *= shrink;
    plan.wave = std::move(wave);
    plan.baseValue = levels.lowValue;
    const double cycles = startBeat / plan.divisionBeats;
    const double fraction = cycles - std::floor(cycles);
    plan.phaseDegrees = fraction < 1e-9 ? 0.0 : (1.0 - fraction) * 360.0;

    planLaneEdit(plan, lane, curve, startBeat, endBeat);
    const auto kept = (long long)lane.points.size() - (long long)plan.removeBeats.size();
    if (kept + (long long)plan.addPoints.size() > synth::TimelineDoc::kMaxBreakpointsPerLane) {
        plan.blocker = CustomLfoBlocker::LaneFull;
        return plan;
    }
    planAmountLane(plan, startBeat, endBeat);
    plan.blocker = CustomLfoBlocker::None;
    return plan;
}

juce::String customLfoMenuText(CustomLfoBlocker blocker) {
    switch (blocker) {
    case CustomLfoBlocker::None:
        return "Create custom LFO";
    case CustomLfoBlocker::NoRange:
        return "Create custom LFO (select a range first)";
    case CustomLfoBlocker::TooLong:
        return "Create custom LFO (range longer than 8 bars)";
    case CustomLfoBlocker::Flat:
        return "Create custom LFO (range is flat)";
    case CustomLfoBlocker::LaneFull:
        return "Create custom LFO (lane has too many points)";
    }
    return "Create custom LFO";
}

} // namespace synth::ui
