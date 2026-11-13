#pragma once

#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

namespace synth::sequencer_detail {

/** A finite number from a number, bool or numeric string; false for anything else (NaN and Inf included). */
inline bool readNumber(const juce::var& v, double& out) {
    double value = 0.0;
    if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()) {
        value = (double)v;
    } else if (v.isString()) {
        const auto text = v.toString().trim();
        const auto first = text.isEmpty() ? juce::juce_wchar(0) : text[0];
        const bool numeric = juce::CharacterFunctions::isDigit(first) || first == '-' || first == '.' || first == '+';
        if (!numeric)
            return false;
        value = text.getDoubleValue();
    } else {
        return false;
    }
    if (!std::isfinite(value))
        return false;
    out = value;
    return true;
}

inline bool readInt(const juce::var& v, int lo, int hi, int& out) {
    double value = 0.0;
    if (!readNumber(v, value))
        return false;
    out = (int)std::lround(std::clamp(value, (double)lo, (double)hi));
    return true;
}

inline bool readFloat(const juce::var& v, float lo, float hi, float& out) {
    double value = 0.0;
    if (!readNumber(v, value))
        return false;
    out = (float)std::clamp(value, (double)lo, (double)hi);
    return true;
}

} // namespace synth::sequencer_detail
