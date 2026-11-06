#pragma once

#include <juce_core/juce_core.h>

namespace synth {

// The letter a section variation shows: 0 is "A", 25 is "Z".
inline juce::String variationLetter(int letter) {
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + letter));
}

} // namespace synth
