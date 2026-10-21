#pragma once

#include <algorithm>
#include <juce_core/juce_core.h>

namespace synth {

/** "C#3"-style name of a MIDI pitch (sharps, 0..127). `middleCOctave` is the octave number MIDI 60 gets: 4 in the
 *  piano roll, 3 in the sequencer's step grid. */
inline juce::String noteNameWithOctave(int pitch, int middleCOctave = 4) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    pitch = std::clamp(pitch, 0, 127);
    return juce::String(kNames[pitch % 12]) + juce::String(pitch / 12 - 5 + middleCOctave);
}

} // namespace synth
