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

/** "C", "C#", ... for a MIDI note number, ignoring the octave. */
juce::String noteNameOf(int midiNote);

} // namespace synth::soundbrowser
