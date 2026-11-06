#pragma once

// ShapeWindowPrefs.h -- what the shape window remembers across windows and launches, one value per kind of shape list
// ("lfo", "filter"): the grid choice and whether the Filter's spectrum shows. They are user settings, not patch data.
// A missing value (or no settings at all, as in a test without them) reads as the default: grid Off, spectrum off.
#include <juce_data_structures/juce_data_structures.h>

namespace synth::ui::shapewindowprefs {

inline juce::String gridKey(const juce::String& kind) { return "shapeWindowGrid_" + kind; }
inline juce::String spectrumKey(const juce::String& kind) { return "shapeWindowSpectrum_" + kind; }
/** Holds "0" while the labelled guide lines are hidden; a missing value shows them. */
inline juce::String guidesKey(const juce::String& kind) { return "shapeWindowGuides_" + kind; }
/** The Draw pen: a shape name ("Sine") while draw mode is on, "off:Sine" when it was left (the shape is kept), else
 *  none. */
inline juce::String penKey(const juce::String& kind) { return "shapeWindowPen_" + kind; }

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

inline bool loadFlag(juce::ApplicationProperties* props, const juce::String& key) {
    return loadText(props, key) == "1";
}

inline void saveFlag(juce::ApplicationProperties* props, const juce::String& key, bool on) {
    saveText(props, key, on ? "1" : juce::String());
}

} // namespace synth::ui::shapewindowprefs
