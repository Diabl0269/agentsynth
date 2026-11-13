#pragma once

// Fits a sound-browser loop to the song: time-stretch to the song's tempo and, optionally, pitch-shift to the song's
// key, with Signalsmith Stretch (MIT, header-only). Pure functions on buffers: no threads, no files, no UI.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <optional>

#include "Music/MusicalScale.h"

namespace synth::soundbrowser {

/** Why a loop is or is not shifted to the song's key. */
enum class KeyMatch {
    Off,       ///< Match key is off, or the sound is not a loop.
    NoSongKey, ///< The song has no key, so nothing to match.
    NoLoopKey, ///< The loop's key is unknown, so it plays as recorded.
    InKey,     ///< Already in the song's key (or its relative): no shift.
    Shifted    ///< `semitones` is not 0.
};

/** What to do to a loop: play a file of `fileBpm` at `songBpm`, shifted by `semitones`. */
struct LoopTarget {
    double fileBpm = 0.0; ///< 0 = unknown tempo, so no time-stretch.
    double songBpm = 0.0;
    int semitones = 0;
    KeyMatch keyMatch = KeyMatch::Off;
    juce::String targetKey; ///< The key the loop lands in ("D minor"); empty unless InKey or Shifted.

    /** songBpm / fileBpm (above 1 is faster, so shorter); 1 when either tempo is unknown. */
    double tempoRatio() const noexcept;
    /** True when stretchLoop would change the audio: the ratio is more than 0.5% from 1, or semitones is not 0. */
    bool needsStretch() const noexcept;
};

/** `fileBpm`, twice it or half of it, whichever is closest to `songBpm`; the browser treats half and double time as
 *  near the song's tempo. `fileBpm` when either tempo is not positive. */
double bestStretchBpm(double fileBpm, double songBpm);

/** How a loop in key `loopKey` meets `songKey` (names as parseSoundKey reads them: "Am", "C# major"): the smallest
 *  shift (-6..+6 semitones) onto the song's key, or onto its relative key when both modes are known and differ (a
 *  major loop in D minor goes to F major). Never Off: `keyMatch` is NoSongKey, NoLoopKey, InKey or Shifted. */
struct KeyPlan {
    int semitones = 0;
    KeyMatch keyMatch = KeyMatch::NoSongKey;
    juce::String targetKey;
};
KeyPlan planKeyMatch(const juce::String& loopKey, const juce::String& songKey);

/** The semitones of planKeyMatch: 0 when either key is unknown or the loop is already in key. */
int semitonesToKey(const juce::String& loopKey, const juce::String& songKey);

/** The target for a loop of `entryBpm` and `entryKey` in a song at `songBpm` in `songKey`; the key fields follow
 *  planKeyMatch when `matchKey`, else the semitones are 0 and keyMatch is Off. The file's tempo is the closest of its
 *  half, itself and double (bestStretchBpm). */
LoopTarget makeLoopTarget(double entryBpm, const juce::String& entryKey, double songBpm, const juce::String& songKey,
                          bool matchKey);

/** The song key as the sound browser names a key ("D minor", "F major"), by the scale's third: a minor third (blues,
 *  locrian) is minor, a major third (whole tone, phrygian dominant) is major. Empty for no key or a scale with no third
 *  or both. */
juce::String soundKeyForScale(const std::optional<synth::MusicalScale>& key);

/** `in` played `tempoRatio` times faster (so 1/ratio as long: round(length / ratio) samples) and `semitones` higher,
 *  same channels and rate. A ratio of 1 with no shift, a ratio that is not positive and an input under about 0.25 s
 *  come back unchanged. Blocking and CPU heavy (about 50 times faster than real time): call it off the message thread
 *  for anything long. */
juce::AudioBuffer<float> stretchLoop(const juce::AudioBuffer<float>& in, double sampleRate, double tempoRatio,
                                     double semitones);

} // namespace synth::soundbrowser
