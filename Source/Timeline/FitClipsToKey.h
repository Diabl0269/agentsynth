#pragma once

// Fit to key over many clips: the clip menu's "Fit all clips to key" (each clip to its own resolved key) and the Song
// key picker's "Fit project to key" (every clip that follows the song key). Neither writes an undo step: the caller
// wraps the call. docs/timeline/song-key.md.

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <optional>
#include <vector>

namespace synth {

/** What a project fit did, for the message the user reads. */
struct FitProjectResult {
    int clipsFollowing = 0; // MIDI clips with notes that follow the song key and may change
    int clipsMoved = 0;     // of those, the clips in which a note moved
    int skippedOwnKey =
        0; // MIDI clips with notes left alone because they, or a clip they are linked to, have their own key
    int audioClips = 0; // audio clips, never touched
    bool moved() const noexcept { return clipsMoved > 0; }
};

/** Moves the notes of each MIDI clip in `ids` to its own key, or to `songKey` when it has none; audio clips and clips
 * with no key are skipped. True if any note moved. */
bool fitClipsToTheirKeys(TimelineDoc& doc, const std::vector<ClipId>& ids, const std::optional<MusicalScale>& songKey);

/** Moves every note of every MIDI clip that follows the song key to `songKey`. Clips on tracks `skipTrack` accepts
 * (drum kits) are left alone and not counted; so is any linked group that contains a clip with its own key or on such a
 * track, since linked clips share their notes. */
FitProjectResult fitProjectToSongKey(TimelineDoc& doc, const MusicalScale& songKey,
                                     const std::function<bool(const Track&)>& skipTrack = {});

/** The sentence the user reads after a project fit, naming `keyName`: "Fitted 5 clips to D Dorian. Skipped 2 with their
 * own key. Audio clips were left alone." A clause with a zero count is left out; nothing moved says so. */
juce::String describeProjectFit(const FitProjectResult& result, const juce::String& keyName);

} // namespace synth
