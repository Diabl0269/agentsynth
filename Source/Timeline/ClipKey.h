#pragma once

// The key a MIDI clip uses: its own (Clip::scale) or, with none, the song key it follows. Highlighting, snapping new
// notes, Generate, the range picker and Fit to key all ask this one question. docs/timeline/song-key.md.

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <optional>

namespace synth {

/** What a clip's key resolves to: `key` is nullopt when there is none (following a song with No key). */
struct ResolvedClipKey {
    std::optional<MusicalScale> key;
    bool following = true; // false: the clip has its own key
};

inline ResolvedClipKey resolveClipKey(const std::optional<MusicalScale>& ownKey,
                                      const std::optional<MusicalScale>& songKey) {
    if (ownKey.has_value())
        return {ownKey, false};
    return {songKey, true};
}

inline ResolvedClipKey resolveClipKey(const Clip& clip, const std::optional<MusicalScale>& songKey) {
    return resolveClipKey(clip.scale, songKey);
}

} // namespace synth
