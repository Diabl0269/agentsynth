// ClipColourGlide.h -- the clip lane's colour glide: each clip's body colour (its track's colour, dimmed when the track
// or the clip is muted) eases from the one drawn to the one an undo or redo restores. Keyed by clip id; the lane syncs
// it at the end of every refresh from the doc (TimelineClipLaneRestoreGlide.cpp) and paints through glide.drawn().
#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"
#include "UI/Timeline/TrackColour.h"
#include <map>

namespace synth::ui {

struct ClipColourGlide {
    explicit ClipColourGlide(juce::Component& owner)
        : glide(owner) {}

    RestoredColourGlide glide;
    /** The colour each clip rested at at the last sync, to tell which clips changed colour. */
    std::map<std::int64_t, juce::Colour> seen;

    /** Reads every clip's resting colour from `doc`: a clip whose colour changed eases (restore) or lands (any other
     *  change); a clip that is gone is forgotten. */
    void sync(const synth::TimelineDoc& doc, bool restored) {
        std::map<std::int64_t, juce::Colour> now;
        const auto& tracks = doc.getTracks();
        for (int row = 0; row < (int)tracks.size(); ++row)
            for (const auto& clip : tracks[(size_t)row].clips)
                now[clip.id.value] =
                    resolveTrackColour(tracks[(size_t)row].colourArgb, row, tracks[(size_t)row].muted || clip.muted);
        for (const auto& [id, colour] : now)
            if (const auto was = seen.find(id); was != seen.end())
                glide.note(id, was->second, colour, restored);
        for (const auto& [id, colour] : seen)
            if (now.find(id) == now.end())
                glide.forget(id);
        seen = std::move(now);
    }
};

} // namespace synth::ui
