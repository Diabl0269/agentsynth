#pragma once

// Search and folder browsing over one immutable snapshot. The matcher follows the app's one search rule (every
// whitespace-separated word must occur, any order, ignoring case); it is a Core copy because Core cannot use the UI's.

#include "SoundBrowser/SoundEntry.h"

namespace synth::soundbrowser {

/** -1 = no match, else lower is better (0 starts with the word, 1 at a word boundary, 2 inside a word). */
int soundSearchScore(const juce::String& text, const juce::String& query);

/** True when `entry` passes every active field of `filter`. */
bool soundPassesFilter(const SoundEntry& entry, const SoundFilter& filter);

/** A key name as pitch class 0..11 (C = 0, sharps and flats alike) and mode (0 unknown, 1 major, 2 minor), or
 *  pitchClass -1 when the text is no key. Reads "A minor", "Am", "C#", "Db major", "Ebm". */
struct SoundKeyId {
    int pitchClass = -1;
    int mode = 0;
};
SoundKeyId parseSoundKey(const juce::String& text);

/** Entries passing `filter` whose name, folder, place or tags match `query`; name matches first, then by name. */
std::vector<SoundEntry> searchSnapshot(const SoundSnapshot& snapshot, const juce::String& query,
                                       const SoundFilter& filter);

/** Immediate subfolders and sounds of `relativeFolder` in a place that pass `filter`; a folder with no passing sound
 *  at any depth is left out and the counts are of passing sounds. */
SoundListing listFolder(const SoundSnapshot& snapshot, const juce::String& placeId, const juce::String& relativeFolder,
                        const SoundFilter& filter = {});

} // namespace synth::soundbrowser
