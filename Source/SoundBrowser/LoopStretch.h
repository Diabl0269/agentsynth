#pragma once

// Fits a sound-browser loop to the song: time-stretch to the song's tempo and, optionally, pitch-shift to the song's
// key, with Signalsmith Stretch (MIT, header-only). Pure functions on buffers: no threads, no files, no UI.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <optional>

#include "Music/MusicalScale.h"

namespace synth::soundbrowser {

/** What to do to a loop: play a file of `fileBpm` at `songBpm`, shifted by `semitones`. */
struct LoopTarget {
    double fileBpm = 0.0; ///< 0 = unknown tempo, so no time-stretch.
    double songBpm = 0.0;
    int semitones = 0;

    /** songBpm / fileBpm (above 1 is faster, so shorter); 1 when either tempo is unknown. */
    double tempoRatio() const noexcept;
    /** True when stretchLoop would change the audio: the ratio is more than 0.5% from 1, or semitones is not 0. */
    bool needsStretch() const noexcept;
};

/** `fileBpm`, twice it or half of it, whichever is closest to `songBpm`; the browser treats half and double time as
 *  near the song's tempo. `fileBpm` when either tempo is not positive. */
double bestStretchBpm(double fileBpm, double songBpm);

/** The smallest shift (-6..+6 semitones) that moves a loop in key `loopKey` to `songKey` (names as parseSoundKey reads
 *  them: "Am", "C# major"). 0 when either key is unknown or both modes are known and differ. */
int semitonesToKey(const juce::String& loopKey, const juce::String& songKey);

/** The target for a loop of `entryBpm` and `entryKey` in a song at `songBpm` in `songKey`; the semitones are 0 unless
 *  `matchKey`. The file's tempo is the closest of its half, itself and double (bestStretchBpm). */
LoopTarget makeLoopTarget(double entryBpm, const juce::String& entryKey, double songBpm, const juce::String& songKey,
                          bool matchKey);

/** The song key as the sound browser names a key ("D minor", "F major"); empty for no key or a scale that is neither
 *  major nor minor in character (blues, whole tone, locrian). */
juce::String soundKeyForScale(const std::optional<synth::MusicalScale>& key);

/** `in` played `tempoRatio` times faster (so 1/ratio as long: round(length / ratio) samples) and `semitones` higher,
 *  same channels and rate. A ratio of 1 with no shift, a ratio that is not positive and an input under about 0.25 s
 *  come back unchanged. Blocking and CPU heavy (about 50 times faster than real time): call it off the message thread
 *  for anything long. */
juce::AudioBuffer<float> stretchLoop(const juce::AudioBuffer<float>& in, double sampleRate, double tempoRatio,
                                     double semitones);

} // namespace synth::soundbrowser
