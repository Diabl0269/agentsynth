#include "Timeline/SongKeyNotes.h"

namespace synth {

// Length-weighted rather than counted, so a held pedal note outweighs a run of grace notes, which is what the key
// profiles detectKey() correlates against were measured on.
std::array<double, 12> songPitchClassWeights(const TimelineDoc& doc,
                                             const std::function<bool(const Track&)>& skipTrack) {
    std::array<double, 12> weights{};
    for (const auto& track : doc.getTracks()) {
        if (track.kind != TrackKind::Midi || track.muted || (skipTrack && skipTrack(track)))
            continue;
        for (const auto& clip : track.clips) {
            if (clip.muted || clip.assetRef.isNotEmpty())
                continue;
            for (const auto& note : clip.notes)
                if (!note.muted && note.lengthBeats > 0.0)
                    weights[(size_t)(((note.pitch % 12) + 12) % 12)] += note.lengthBeats;
        }
    }
    return weights;
}

} // namespace synth
