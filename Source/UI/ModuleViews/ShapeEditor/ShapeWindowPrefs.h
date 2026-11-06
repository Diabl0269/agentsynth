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

/** The frequency rows (the Filter's grid): the kind ("Octaves"), the step ("1/3"), the level step ("6", "Off"), "0"
 *  while a Snap pill is off, the own key ("" follows the song key) and the Root box's entry id. A missing kind reads
 *  the old one-row gridKey once, so a choice made before the rows existed carries over. */
inline juce::String gridKindKey(const juce::String& kind) { return "shapeWindowGridKind_" + kind; }
inline juce::String gridStepsKey(const juce::String& kind) { return "shapeWindowGridSteps_" + kind; }
inline juce::String gridLevelKey(const juce::String& kind) { return "shapeWindowGridLevel_" + kind; }
inline juce::String snapAcrossKey(const juce::String& kind) { return "shapeWindowSnapAcross_" + kind; }
inline juce::String snapLevelKey(const juce::String& kind) { return "shapeWindowSnapLevel_" + kind; }
inline juce::String gridOwnKeyKey(const juce::String& kind) { return "shapeWindowGridKey_" + kind; }
inline juce::String gridRootKey(const juce::String& kind) { return "shapeWindowGridRoot_" + kind; }

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
