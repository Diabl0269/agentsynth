// TimelineClipLaneKitRows.cpp -- a Drum Kit track's clips draw the kit's shown pads as their rows: a pad switched off
// on the card has no row in the clip's note preview, and the rows below it close up. docs/timeline/drum-machine.md.

#include "TimelineClipLaneArea.h"
#include <algorithm>

namespace synth::ui {

void TimelineClipLaneArea::refreshKitRows() {
    std::vector<std::pair<synth::TrackId, KitRows>> fresh;
    if (doc_ != nullptr && kitRowsProvider)
        for (const auto& track : doc_->getTracks())
            if (KitRows rows; kitRowsProvider(track.id, rows))
                fresh.emplace_back(track.id, std::move(rows));
    if (fresh == kitRows_)
        return;
    kitRows_ = std::move(fresh);
    repaint();
}

// The row (0-based) of `pitch` among the rows a clip on `track` shows, with the row count; row -1 means no row (a
// hidden pad's hit). Without a kit the pitch keeps the 0..127 scale and `rowCount` is 0.
TimelineClipLaneArea::NoteRows TimelineClipLaneArea::noteRowsFor(const synth::Track& track,
                                                                 const synth::Clip& clip) const {
    NoteRows out;
    for (const auto& [id, rows] : kitRows_)
        if (id == track.id) {
            for (const auto& lane : visibleDrumLanes(&clip, false, &rows))
                out.pitches.push_back(lane.pitch);
            break;
        }
    return out;
}

int TimelineClipLaneArea::NoteRows::rowOf(int pitch) const {
    const auto it = std::find(pitches.begin(), pitches.end(), pitch);
    return it == pitches.end() ? -1 : (int)(it - pitches.begin());
}

} // namespace synth::ui
