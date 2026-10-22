#pragma once

// The plain data the sound index hands out: one entry per audio file or instrument, the places they live in, and
// the shapes a search or a folder listing returns. Pure Core types: no UI, no threads.

#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace synth::soundbrowser {

enum class SoundKind { OneShot, Loop, Instrument };

struct SoundEntry {
    juce::String path;      ///< Absolute path of the audio file or .sfz.
    juce::String placeId;   ///< The pack id, or "folder:<path>" for a user folder.
    juce::String placeName; ///< What the place is called in the UI.
    juce::String folder;    ///< Folder inside the place, '/'-separated; empty at the root.
    juce::String name;      ///< File name without its extension.
    juce::int64 size = 0;   ///< Bytes.
    juce::int64 mtime = 0;  ///< Modification time in milliseconds.
    double lengthSeconds = 0.0;
    double sampleRate = 0.0;
    SoundKind kind = SoundKind::OneShot;
    juce::StringArray tags; ///< Drums, Bass, Keys, Vocals, FX, Strings.
    double tempo = 0.0;     ///< BPM; 0 when unknown.
    juce::String key;       ///< "A minor", "C"; empty when unknown.
};

struct SoundPlace {
    juce::String id, name;
    juce::File root;
    bool isPack = false;
    int soundCount = 0;
};

/** What the index holds at one moment; immutable once published. */
struct SoundSnapshot {
    std::vector<SoundPlace> places;
    std::vector<SoundEntry> entries;
    std::map<juce::String, size_t> byPath; ///< Index into `entries`.
    bool truncated = false;                ///< The entry cap was hit; later files were not listed.
    bool scanning = false;                 ///< A scan is still running behind this snapshot.
};

struct SoundFilter {
    std::optional<SoundKind> kind;
    juce::String tag;     ///< One of the tags; empty for any.
    juce::String placeId; ///< Empty for every place.
};

struct SoundFolder {
    juce::String name;
    int soundCount = 0; ///< Sounds at any depth below it.
};

struct SoundListing {
    std::vector<SoundFolder> folders;
    std::vector<SoundEntry> entries;
};

} // namespace synth::soundbrowser
