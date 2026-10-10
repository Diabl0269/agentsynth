#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** Plugin-host state for choice parameters (ModuleBase::getStateInformation). A choice's normalised value
 *  shifts when choices are appended to its list, so the choice NAME is saved too and preferred on load.
 */
inline const juce::String kChoiceNameSuffix{"__name"};

/** Saves `c`'s current choice name next to its normalised value. */
inline void saveChoiceName(juce::ValueTree& state, const juce::AudioParameterChoice& c) {
    state.setProperty(c.paramID + kChoiceNameSuffix, c.getCurrentChoiceName(), nullptr);
}

/** Loads `c` from `state`: by saved name when present and still a choice, else from the normalised value
 *  decoded over `legacyCount` choices (the count when a blob without a name was written).
 */
inline void loadChoice(const juce::ValueTree& state, juce::AudioParameterChoice& c, int legacyCount) {
    const auto nameKey = c.paramID + kChoiceNameSuffix;
    const int byName = state.hasProperty(nameKey) ? c.choices.indexOf(state.getProperty(nameKey).toString()) : -1;
    if (byName >= 0)
        c = byName;
    else if (state.hasProperty(c.paramID))
        c = legacyCount <= 1 ? 0 : juce::roundToInt((float)state.getProperty(c.paramID) * (float)(legacyCount - 1));
}

} // namespace synth
