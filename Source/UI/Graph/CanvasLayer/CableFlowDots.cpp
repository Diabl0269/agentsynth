// CableFlowDots.cpp
//
// On an idle canvas the flow dots are all a frame draws, 30 times a second, so their cost is the frame's cost. Placing
// a dot with juce::Path::getPointAlongPath flattens the whole curve every time (and measuring its length flattens it
// again), and a colour set per cable builds a platform colour per cable. Here each cable's curve is measured once, into
// points at equal distances along it, and a dot is placed between two of them; a cable's ends moving gives it a new
// track. A frame's dots are filled one colour at a time: the cables of one kind share a colour.

#include "CableFlowDots.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace synth::ui {

namespace {
// More tracks than any canvas shows means cables that moved away (a drag leaves one per frame): start over.
constexpr size_t kMaxTracks = 4096;
} // namespace

bool CableFlowDots::Key::operator==(const Key& o) const noexcept {
    return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2;
}

size_t CableFlowDots::KeyHash::operator()(const Key& k) const noexcept {
    juce::uint64 h = 1469598103934665603ull;
    for (const float f : {k.x1, k.y1, k.x2, k.y2}) {
        juce::uint32 bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        h = (h ^ bits) * 1099511628211ull;
    }
    return (size_t)h;
}

// The curve is flattened once, with the tolerance getPointAlongPath measures with, and kept as its points and the
// distance along it to each; a dot is placed by finding its segment and stepping into it, as getPointAlongPath does, so
// it lands on the same point without flattening the curve again.
const CableFlowDots::Track& CableFlowDots::track(juce::Point<float> p1, juce::Point<float> p2) {
    const Key key{p1.x, p1.y, p2.x, p2.y};
    if (const auto it = tracks_.find(key); it != tracks_.end())
        return it->second;
    if (tracks_.size() >= kMaxTracks)
        tracks_.clear();
    ++tracksMeasured_;

    Track track;
    const juce::Path wire = build_(p1, p2);
    juce::PathFlatteningIterator it(wire, {}, juce::Path::defaultToleranceForMeasurement);
    float along = 0.0f;
    while (it.next()) {
        const juce::Point<float> a((float)it.x1, (float)it.y1), b((float)it.x2, (float)it.y2);
        if (track.points.empty()) {
            track.points.push_back(a);
            track.along.push_back(0.0f);
        }
        along += a.getDistanceFrom(b);
        track.points.push_back(b);
        track.along.push_back(along);
    }
    if (track.points.empty()) { // a cable with no length
        track.points.push_back(p1);
        track.along.push_back(0.0f);
    }
    return tracks_.emplace(key, std::move(track)).first->second;
}

juce::Point<float> CableFlowDots::pointAt(const Track& track, float distance) noexcept {
    const auto end = std::lower_bound(track.along.begin(), track.along.end(), distance);
    if (end == track.along.begin())
        return track.points.front();
    if (end == track.along.end())
        return track.points.back();
    const auto i = (size_t)(end - track.along.begin());
    const float segment = track.along[i] - track.along[i - 1];
    const float into = segment > 0.0f ? (distance - track.along[i - 1]) / segment : 0.0f;
    return track.points[i - 1] + (track.points[i] - track.points[i - 1]) * into;
}

void CableFlowDots::addDots(juce::Path& out, juce::Point<float> p1, juce::Point<float> p2, float phase,
                            juce::Rectangle<float> keepOut) {
    const auto& track = this->track(p1, p2);
    const float length = track.along.back();
    constexpr float r = kDotDiameter * 0.5f;
    for (int d = 0; d < kDotsPerCable; ++d) {
        const float t = std::fmod(phase + (float)d / (float)kDotsPerCable, 1.0f);
        const auto pt = pointAt(track, t * length);
        if (!keepOut.contains(pt))
            out.addEllipse(pt.x - r, pt.y - r, kDotDiameter, kDotDiameter);
    }
}

void CableFlowDots::batch(juce::Colour colour, juce::Point<float> p1, juce::Point<float> p2, float phase,
                          juce::Rectangle<float> keepOut) {
    const auto argb = colour.getARGB();
    auto it = std::find_if(batches_.begin(), batches_.end(), [argb](const auto& b) { return b.first == argb; });
    if (it == batches_.end())
        it = batches_.insert(batches_.end(), {argb, juce::Path()});
    addDots(it->second, p1, p2, phase, keepOut);
}

void CableFlowDots::fill(juce::Graphics& g) {
    lastFillCount_ = 0;
    for (auto& [argb, dots] : batches_) {
        if (dots.isEmpty())
            continue;
        g.setColour(juce::Colour(argb));
        g.fillPath(dots);
        ++lastFillCount_;
    }
    batches_.clear();
}

} // namespace synth::ui
