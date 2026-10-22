#pragma once

// Search and folder browsing over one immutable snapshot. The matcher follows the app's one search rule (every
// whitespace-separated word must occur, any order, ignoring case); it is a Core copy because Core cannot use the UI's.

#include "SoundBrowser/SoundEntry.h"

namespace synth::soundbrowser {

/** -1 = no match, else lower is better (0 starts with the word, 1 at a word boundary, 2 inside a word). */
int soundSearchScore(const juce::String& text, const juce::String& query);

/** Entries passing `filter` whose name, folder, place or tags match `query`; name matches first, then by name. */
std::vector<SoundEntry> searchSnapshot(const SoundSnapshot& snapshot, const juce::String& query,
                                       const SoundFilter& filter);

/** Immediate subfolders and sounds of `relativeFolder` in a place. */
SoundListing listFolder(const SoundSnapshot& snapshot, const juce::String& placeId, const juce::String& relativeFolder);

} // namespace synth::soundbrowser
