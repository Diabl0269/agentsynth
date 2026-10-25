#pragma once

// Fit to key: move notes to the nearest pitch of a key, ties going down. It runs only when chosen (piano roll chip or
// shortcut, clip right-click menu); setting a key never calls it. docs/timeline/scale-assist.md.

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <vector>

namespace synth {

/** The notes to fit: `selected` when it is not empty, otherwise every note of the clip. */
inline std::vector<NoteId> fitToKeyTargets(const Clip& clip, const std::vector<NoteId>& selected) {
    if (!selected.empty())
        return selected;
    std::vector<NoteId> all;
    all.reserve(clip.notes.size());
    for (const auto& note : clip.notes)
        all.push_back(note.id);
    return all;
}

/** Moves each note in `ids` to the nearest pitch of `scale`; true if any moved. No undo step: wrap the call. */
inline bool fitNotesToKey(TimelineDoc& doc, const std::vector<NoteId>& ids, MusicalScale scale) {
    bool moved = false;
    for (const auto id : ids) {
        const auto* note = doc.getNote(id);
        if (note == nullptr)
            continue;
        const int snapped = scale.snapPitch(note->pitch);
        if (snapped != note->pitch)
            moved = doc.moveNote(id, note->startBeat, snapped) || moved;
    }
    return moved;
}

} // namespace synth
