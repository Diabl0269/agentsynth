// ClipKeyboardNav.cpp
//
// Pure clip-to-clip navigation over a TimelineDoc; see ClipKeyboardNav.h.

#include "ClipKeyboardNav.h"

#include <cmath>

namespace synth::ui::clipnav {

synth::ClipId firstClipFrom(const synth::Track& track, double beat) {
    if (track.clips.empty())
        return {};
    for (const auto& clip : track.clips) {
        if (clip.startBeat >= beat)
            return clip.id;
    }
    return track.clips.front().id;
}

synth::ClipId adjacentOnTrack(const synth::Track& track, synth::ClipId id, int direction) {
    // Clips stay sorted by (startBeat, id), so the vector order IS the left-to-right order.
    for (std::size_t i = 0; i < track.clips.size(); ++i) {
        if (track.clips[i].id != id)
            continue;
        const auto next = direction < 0 ? (long long)i - 1 : (long long)i + 1;
        if (next < 0 || next >= (long long)track.clips.size())
            return {};
        return track.clips[(std::size_t)next].id;
    }
    return {};
}

synth::ClipId nearestOnAdjacentTrack(const synth::TimelineDoc& doc, synth::ClipId id, int direction) {
    const auto* clip = doc.getClip(id);
    const auto* own = doc.getTrackForClip(id);
    if (clip == nullptr || own == nullptr || direction == 0)
        return {};

    const auto& tracks = doc.getTracks();
    long long row = -1;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == own->id)
            row = (long long)i;
    }
    if (row < 0)
        return {};

    const long long step = direction < 0 ? -1 : 1;
    for (long long r = row + step; r >= 0 && r < (long long)tracks.size(); r += step) {
        const auto& track = tracks[(std::size_t)r];
        if (track.clips.empty())
            continue;
        const synth::Clip* best = nullptr;
        double bestDistance = 0.0;
        for (const auto& candidate : track.clips) {
            const double distance = std::abs(candidate.startBeat - clip->startBeat);
            // Sorted by start, so on a tie the earlier clip is seen first and kept.
            if (best == nullptr || distance < bestDistance - 1e-9) {
                best = &candidate;
                bestDistance = distance;
            }
        }
        return best->id;
    }
    return {};
}

} // namespace synth::ui::clipnav
