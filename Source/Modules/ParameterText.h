// Concern: how a percent, a millisecond time and a phase angle read as text -- on a knob's value
// box and to a screen reader -- and how typed text reads back.
#pragma once

#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** "50 %". */
inline juce::String percentToText(float v) { return juce::String(juce::roundToInt(v)) + " %"; }

/** Reads "50 %", "50%" and a bare "50" back as a percent. */
inline float percentFromText(const juce::String& text) {
    return text.trim().upToFirstOccurrenceOf("%", false, false).getFloatValue();
}

/** Below 1000 ms in milliseconds ("0 ms", "7.5 ms", "120 ms"), from 1000 ms in seconds ("1.50 s"). */
inline juce::String millisecondsToText(float ms) {
    if (ms >= 1000.0f)
        return juce::String(ms / 1000.0f, 2) + " s";
    return juce::String(ms, ms < 10.0f ? 1 : 0) + " ms";
}

/** Reads "120 ms", "1.5 s" and a bare "120" (milliseconds) back as milliseconds. */
inline float millisecondsFromText(const juce::String& text) {
    const auto t = text.trim();
    if (t.endsWithIgnoreCase("ms"))
        return t.dropLastCharacters(2).getFloatValue();
    if (t.endsWithIgnoreCase("s"))
        return t.dropLastCharacters(1).getFloatValue() * 1000.0f;
    return t.getFloatValue();
}

/** "90°". */
inline juce::String degreesToText(float v) {
    return juce::String(juce::roundToInt(v)) + juce::String(juce::CharPointer_UTF8("\xc2\xb0"));
}

/** Reads "90°", "90 deg" and a bare "90" back as degrees. */
inline float degreesFromText(const juce::String& text) { return text.trim().getFloatValue(); }

/** Parameter attributes that format and parse a percent, keeping the host-facing unit label. */
inline juce::AudioParameterFloatAttributes percentAttributes() {
    return juce::AudioParameterFloatAttributes()
        .withLabel("%")
        .withStringFromValueFunction([](float v, int) { return percentToText(v); })
        .withValueFromStringFunction([](const juce::String& t) { return percentFromText(t); });
}

/** Parameter attributes that format and parse a time held in milliseconds. */
inline juce::AudioParameterFloatAttributes millisecondsAttributes() {
    return juce::AudioParameterFloatAttributes()
        .withLabel("ms")
        .withStringFromValueFunction([](float v, int) { return millisecondsToText(v); })
        .withValueFromStringFunction([](const juce::String& t) { return millisecondsFromText(t); });
}

/** Parameter attributes that format and parse an angle in degrees. */
inline juce::AudioParameterFloatAttributes degreesAttributes() {
    return juce::AudioParameterFloatAttributes()
        .withLabel("deg")
        .withStringFromValueFunction([](float v, int) { return degreesToText(v); })
        .withValueFromStringFunction([](const juce::String& t) { return degreesFromText(t); });
}

/** "0.0 st", "-12.0 st": one decimal, and a value that rounds to zero never prints as "-0.0". */
inline juce::String semitonesToText(float v) {
    const float r = std::round(v * 10.0f) / 10.0f;
    return juce::String(r == 0.0f ? 0.0f : r, 1) + " st";
}

/** Reads "-12.0 st", "-12st" and a bare "-12" back as semitones. */
inline float semitonesFromText(const juce::String& text) {
    return text.trim().upToFirstOccurrenceOf("st", false, true).getFloatValue();
}

/** Parameter attributes that format and parse a pitch shift in semitones. */
inline juce::AudioParameterFloatAttributes semitonesAttributes() {
    return juce::AudioParameterFloatAttributes()
        .withLabel("st")
        .withStringFromValueFunction([](float v, int) { return semitonesToText(v); })
        .withValueFromStringFunction([](const juce::String& t) { return semitonesFromText(t); });
}

/** A 0..N multiplier shown as a percent: 1.0 reads "100%", 0.5 reads "50%". */
inline juce::AudioParameterFloatAttributes multiplierPercentAttributes() {
    return juce::AudioParameterFloatAttributes()
        .withLabel("%")
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })
        .withValueFromStringFunction([](const juce::String& t) { return percentFromText(t) / 100.0f; });
}

} // namespace synth
