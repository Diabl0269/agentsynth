// ControllerLaneEdits — the pure edit maths behind the piano roll's velocity / CC lane gestures
// (declared in ControllerLaneEdits.h). Headless and allocation-light; the strip calls these on every
// drag step for its preview and once more on mouse-up for the commit, so preview and commit can never
// disagree.

#include "ControllerLaneEdits.h"

#include "Timeline/AutomationRecorder.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui::lanes {

juce::String laneName(int laneId) {
    switch (laneId) {
    case kVelocityLane:
        return "Velocity";
    case 1:
        return "CC1 Mod Wheel";
    case 2:
        return "CC2 Breath";
    case 7:
        return "CC7 Volume";
    case 10:
        return "CC10 Pan";
    case 11:
        return "CC11 Expression";
    case 64:
        return "CC64 Sustain";
    default:
        return "CC" + juce::String(laneId);
    }
}

// 64..69 are the on/off pedals (sustain, portamento, sostenuto, soft, legato, hold 2): a ramp between
// two of their values means nothing to a receiver, so a new point holds until the next one.
int defaultCurveFor(int ccNumber) noexcept {
    return (ccNumber >= 64 && ccNumber <= 69) ? static_cast<int>(synth::BreakpointCurve::Hold)
                                              : static_cast<int>(synth::BreakpointCurve::Linear);
}

// A zero-width span (a click, or a purely vertical drag) sets every note starting exactly there to
// valueB — the value under the pointer NOW, which is what a vertical scrub on one bar means.
std::vector<std::pair<synth::NoteId, int>> velocityLine(const synth::Clip& clip,
                                                        const std::vector<synth::NoteId>& restrictTo, double beatA,
                                                        double valueA, double beatB, double valueB) {
    const double lo = std::min(beatA, beatB);
    const double hi = std::max(beatA, beatB);
    const std::set<std::int64_t> allowed = [&] {
        std::set<std::int64_t> ids;
        for (const auto& id : restrictTo)
            ids.insert(id.value);
        return ids;
    }();

    std::vector<std::pair<synth::NoteId, int>> out;
    for (const auto& note : clip.notes) {
        if (note.startBeat >= clip.lengthBeats)
            break; // sorted by start: everything after is past the clip end too
        if (note.startBeat < lo || note.startBeat > hi)
            continue;
        if (!allowed.empty() && allowed.count(note.id.value) == 0)
            continue;
        const double t = (hi > lo) ? (note.startBeat - beatA) / (beatB - beatA) : 1.0;
        const double value = valueA + (valueB - valueA) * t;
        out.emplace_back(note.id, juce::jlimit(1, 127, (int)std::lround(value)));
    }
    return out;
}

std::vector<synth::ControllerPoint> replaceSpan(const std::vector<synth::ControllerPoint>& existing, double from,
                                                double to, const std::vector<synth::ControllerPoint>& inserted) {
    const double lo = std::min(from, to);
    const double hi = std::max(from, to);
    std::vector<synth::ControllerPoint> out;
    out.reserve(existing.size() + inserted.size());
    for (const auto& point : existing)
        if (point.beat < lo || point.beat > hi)
            out.push_back(point);
    out.insert(out.end(), inserted.begin(), inserted.end());
    // Stable, and `inserted` came last: on a same-beat collision the inserted point sorts after the
    // survivor, and the dedupe below keeps the later one.
    std::stable_sort(out.begin(), out.end(),
                     [](const synth::ControllerPoint& a, const synth::ControllerPoint& b) { return a.beat < b.beat; });
    std::vector<synth::ControllerPoint> deduped;
    deduped.reserve(out.size());
    for (const auto& point : out) {
        if (!deduped.empty() && deduped.back().beat == point.beat)
            deduped.back() = point;
        else
            deduped.push_back(point);
    }
    return deduped;
}

// Reuses the automation recorder's RDP thinning rather than re-implementing it. The tolerance is the
// recorder's own epsilon fraction scaled to the CC range, floored at ONE PIXEL of the lane: a stroke
// drawn with a mouse is quantised to whole pixels (about 1.5 CC steps on this strip), and a tolerance
// finer than that would keep every pixel wobble of a straight stroke as its own point.
std::vector<synth::ControllerPoint> thinStroke(const std::vector<synth::ControllerPoint>& raw, int curve,
                                               double pixelValue) {
    std::vector<synth::AutomationRecorder::CapturedPoint> captured;
    captured.reserve(raw.size());
    for (const auto& point : raw)
        captured.push_back({point.beat, point.value});
    const double epsilon = std::max(synth::AutomationRecorder::kThinningEpsilonFraction * 127.0,
                                    std::isfinite(pixelValue) ? pixelValue : 0.0);
    const auto thinned = synth::AutomationRecorder::thinPoints(captured, epsilon);
    std::vector<synth::ControllerPoint> out;
    out.reserve(thinned.size());
    for (const auto& point : thinned)
        out.push_back({point.beat, juce::jlimit(0.0, 127.0, point.value), curve});
    return out;
}

} // namespace synth::ui::lanes
