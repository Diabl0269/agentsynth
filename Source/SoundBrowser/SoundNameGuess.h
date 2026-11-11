#pragma once

// Tags, tempo, key and loop-or-one-shot guessed from a file's name and folder, for files that carry no metadata.

#include <juce_core/juce_core.h>

namespace synth::soundbrowser {

struct SoundGuess {
    juce::StringArray tags;
    double tempo = 0.0; ///< 60 to 200; 0 when the name says nothing.
    juce::String key;   ///< "A minor"; empty when the name says nothing.
    bool loopHint = false;
};

/** `folder` is the path inside the place ('/'-separated), `fileName` has no extension. */
SoundGuess guessFromName(const juce::String& folder, const juce::String& fileName);

/** Shortest sound that can be a loop, in seconds. */
inline constexpr double kMinLoopSeconds = 1.5;
/** Fewest beats a loop spans at its tempo. */
inline constexpr double kMinLoopBeats = 2.0;

/** Whether a sound of this length can really be a loop at this tempo (0 = unknown). Names and folders over-promise
    ("Loops/Synth/Pad [128] 19" is a 0.3 s slice), so a name-guessed loop must be at least kMinLoopSeconds long and,
    when the tempo is known, at least kMinLoopBeats beats. Matching whole bars was rejected: real loops are 5 to 10
    percent off a whole bar because of their tails. */
bool loopLengthFits(double lengthSeconds, double tempo);

/** "C", "C#", ... for a MIDI note number, ignoring the octave. */
juce::String noteNameOf(int midiNote);

} // namespace synth::soundbrowser
