#pragma once

// The song layout a project.generate answer may carry ("song": named patterns of notes plus sections that
// play them), expanded into ordinary timelineOps before the edit is previewed. This mirrors the hosted
// service's expansion (synth-platform: packages/capabilities/src/project-generate/song.ts, `expandSong` with
// `tempoAndMarkers` on, and `parseNotes`); a change there must be ported here. Where the hosted schema would
// reject an answer and ask the model again, this port never throws: it skips what cannot be read and says
// so in `warnings`.

#include <juce_core/juce_core.h>
#include <vector>

namespace synth::projectgenerate {

constexpr int kBeatsPerBar = 4;
constexpr int kSongMaxPatterns = 64;
constexpr int kSongMaxSections = 32;
constexpr int kSongMaxTotalBars = 256;
constexpr int kSongNotesMaxChars = 20000;
constexpr int kSongPatternMaxBars = 8;
constexpr int kSongSectionMaxBars = 64;
constexpr int kSongMaxPlaysPerSection = 16;
constexpr int kSongMinBpm = 40;
constexpr int kSongMaxBpm = 220;
constexpr int kSongMaxTranspose = 24;
constexpr double kSongMinVelocityScale = 0.25;
constexpr double kSongMaxVelocityScale = 1.5;
constexpr int kSongIdMaxChars = 40;
constexpr int kTimelineOpsMaxOps = 64;
constexpr int kTimelineOpsMaxBeat = 100000;
constexpr int kTimelineOpsMaxTracks = 256;
constexpr int kTimelineOpsMaxNameChars = 128;
constexpr int kTimelineOpsMaxClipsPerOp = 4096;
constexpr int kTimelineOpsMaxNotesPerClip = 16384;

/** One note of a pattern: beats relative to the pattern start, MIDI pitch 0..127, velocity 1..127. */
struct SongNote {
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    int pitch = 0;
    int velocity = 100;
};

/** Parses `start:length:pitch[:velocity]` tokens (velocity defaults to 100). Returns false and fills `error` with
 * the first problem; `notes` then holds what parsed before it. */
bool parseSongNotes(const juce::String& text, int bars, const juce::String& patternId, std::vector<SongNote>& notes,
                    juce::String& error);

/** `envelope` without "song", its notes expanded into setTempo, addMarker and placeClips timelineOps (after the
 * envelope's own ops). An envelope without "song" comes back unchanged. */
juce::var expandSong(const juce::var& envelope, juce::StringArray* warnings = nullptr);

} // namespace synth::projectgenerate
