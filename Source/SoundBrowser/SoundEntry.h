#pragma once

// The plain data the sound index hands out: one entry per audio file or instrument, the places they live in, and
// the shapes a search or a folder listing returns. Pure Core types: no UI, no threads.

#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
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

enum class SoundLength { Any, Under1, OneToFour, Over4 };

/** What a search or a folder listing keeps. Within the kinds and within the tags any one may match; every other
 *  field that is set must match as well. An unknown tempo (0) or key (empty) never passes an active tempo or key. */
struct SoundFilter {
    std::set<SoundKind> kinds;
    juce::StringArray tags;                         ///< Drums, Bass, Keys, Vocals, FX, Strings.
    juce::String placeId;                           ///< Empty for every place.
    std::optional<std::pair<double, double>> tempo; ///< Inclusive BPM range.
    std::optional<double> nearTempo;                ///< The song's BPM: within 10 BPM of it, or of half or double it.
    juce::String key;                               ///< "A minor", "C major", "Am"; empty for any.
    SoundLength length = SoundLength::Any;

    /** True when at least one field narrows the list. */
    bool isActive() const {
        return !kinds.empty() || !tags.isEmpty() || placeId.isNotEmpty() || tempo.has_value() ||
               nearTempo.has_value() || key.isNotEmpty() || length != SoundLength::Any;
    }
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
