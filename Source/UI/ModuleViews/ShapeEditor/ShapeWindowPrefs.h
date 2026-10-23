#pragma once

// ShapeWindowPrefs.h -- what the shape window remembers across windows and launches, one value per kind of shape list
// ("lfo", "filter"): the grid choice. It is a user setting, not patch data.
// A missing value (or no settings at all, as in a test without them) reads as the default: grid Off.
#include <juce_data_structures/juce_data_structures.h>

namespace synth::ui::shapewindowprefs {

inline juce::String gridKey(const juce::String& kind) { return "shapeWindowGrid_" + kind; }

inline juce::String loadText(juce::ApplicationProperties* props, const juce::String& key) {
    if (props == nullptr || props->getUserSettings() == nullptr)
        return {};
    return props->getUserSettings()->getValue(key);
}

/** Writes straight away, so the choice survives a crash as well as a normal close. An empty value removes the key. */
inline void saveText(juce::ApplicationProperties* props, const juce::String& key, const juce::String& value) {
    if (props == nullptr || props->getUserSettings() == nullptr)
        return;
    if (value.isEmpty())
        props->getUserSettings()->removeValue(key);
    else
        props->getUserSettings()->setValue(key, value);
    props->saveIfNeeded();
}

} // namespace synth::ui::shapewindowprefs
