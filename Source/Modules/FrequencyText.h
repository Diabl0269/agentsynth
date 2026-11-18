// Concern: how a frequency parameter reads as text -- on a knob's value box and to a screen reader
// -- and how typed text reads back.
#pragma once

#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** "440 Hz", "55.5 Hz", "0.25 Hz", "1.2 kHz", "12.5 kHz"; a negative shift keeps its sign ("-440 Hz"). */
inline juce::String frequencyToText(float hz) {
    const juce::String sign = hz < 0.0f ? "-" : "";
    const float a = std::abs(hz);
    if (a >= 1000.0f)
        return sign + juce::String(a / 1000.0f, 1) + " kHz";
    if (a >= 100.0f)
        return sign + juce::String(juce::roundToInt(a)) + " Hz";
    return sign + juce::String(a, a < 10.0f ? 2 : 1) + " Hz";
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
