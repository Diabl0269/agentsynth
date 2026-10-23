#pragma once

// The index's background work: discovering places, walking them for audio files (headers only, never a decode) and
// the JSON cache. Internal to SoundBrowser/; SoundIndex is the public face.

#include "SoundBrowser/SoundEntry.h"
#include <atomic>
#include <functional>

namespace synth::soundbrowser {

/** Every pack folder under `packRoots` (a pack.json with an id; the first root wins a repeated id), then each user
    folder, listed whether or not it exists now (`missing` says). `names` maps a folder's path to the name the user gave
    it; a folder without one is called by its own name. */
std::vector<SoundPlace> discoverPlaces(const std::vector<juce::File>& packRoots,
                                       const std::vector<juce::File>& userFolders,
                                       const std::map<juce::String, juce::String>& names = {});

bool isSoundFile(const juce::File& file);

struct ScanLimits {
    size_t cap = 50000;
    std::atomic<int>* headersRead = nullptr; ///< Counts every file whose header was opened.
    std::function<bool()> shouldExit;
    std::function<void(const std::vector<SoundEntry>&)> progress; ///< Called with the entries so far, now and then.
    std::function<void(size_t, const std::vector<SoundEntry>&)> placeDone; ///< A place has been walked (or skipped).
    std::function<void(const juce::File&)> beforeFile; ///< Called before each file is looked at; tests hold the scan.
    int progressIntervalMs = 200;                      ///< The least time between two `progress` calls.
};

struct ScanResult {
    std::vector<SoundEntry> entries;
    bool truncated = false;
};

/** A file whose size and mtime match its `previous` entry is not opened again. */
ScanResult scanPlaces(const std::vector<SoundPlace>& places, const std::map<juce::String, SoundEntry>& previous,
                      const ScanLimits& limits);

std::vector<SoundEntry> readIndexCache(const juce::File& file, bool& truncated);
void writeIndexCache(const juce::File& file, const std::vector<SoundEntry>& entries, bool truncated);

} // namespace synth::soundbrowser
