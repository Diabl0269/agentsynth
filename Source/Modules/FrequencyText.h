// Concern: how a frequency parameter reads as text -- on a knob's value box and to a screen reader
// -- and how typed text reads back.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** "440 Hz", "55.5 Hz", "1.2 kHz", "12.5 kHz". */
inline juce::String frequencyToText(float hz) {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, 1) + " kHz";
    if (hz >= 100.0f)
        return juce::String(juce::roundToInt(hz)) + " Hz";
    return juce::String(hz, 1) + " Hz";
}

/** Reads "1.2 kHz", "1.2k", "440 Hz" and a bare "440" back as hertz. */
inline float frequencyFromText(const juce::String& text) {
    const auto trimmed = text.trim();
    const float number = trimmed.getFloatValue();
    return trimmed.containsChar('k') || trimmed.containsChar('K') ? number * 1000.0f : number;
}

/** Parameter attributes that format and parse a frequency in hertz. */
inline juce::AudioParameterFloatAttributes frequencyAttributes() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return frequencyToText(v); })
        .withValueFromStringFunction([](const juce::String& t) { return frequencyFromText(t); });
}

} // namespace synth
