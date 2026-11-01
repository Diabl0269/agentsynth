// Concern: how a frequency parameter reads as text -- on a knob's value box and to a screen reader
// -- how typed text reads back, and how a frequency knob's travel maps to hertz.
#pragma once

#include <cmath>
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

/** A range where each equal step of knob travel covers the same number of octaves. `start` must be above 0.
 *  Values snap to `interval` hertz and the two ends are exact. Read a parameter's position only through the
 *  range: code that copies its start, end and skew into a plain range loses this curve. */
inline juce::NormalisableRange<float> logFrequencyRange(float start, float end, float interval = 0.01f) {
    juce::NormalisableRange<float> range(
        start, end,
        [](float lo, float hi, float position) {
            if (position <= 0.0f)
                return lo;
            return position >= 1.0f ? hi : lo * std::pow(hi / lo, position);
        },
        [](float lo, float hi, float hz) { return std::log(juce::jlimit(lo, hi, hz) / lo) / std::log(hi / lo); },
        [start, end, interval](float, float, float hz) {
            return juce::jlimit(start, end, start + interval * std::round((hz - start) / interval));
        });
    range.interval = interval;
    return range;
}

} // namespace synth
