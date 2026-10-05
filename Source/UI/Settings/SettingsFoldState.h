#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Remembers which sections of a Settings tab the user folded, across Settings windows and launches.
// Stored as one user-setting value per tab: the folded sections' stable names joined by commas (never
// display text, so rewording a heading cannot repoint a saved fold). A missing value means nothing is
// folded, which is also how a first run looks.
namespace synth::ui::settingsfold {

inline juce::StringArray load(juce::ApplicationProperties* props, const juce::String& key) {
    if (props == nullptr || props->getUserSettings() == nullptr)
        return {};
    return juce::StringArray::fromTokens(props->getUserSettings()->getValue(key), ",", "");
}

// Writes straight away, so the fold survives a crash as well as a normal close.
inline void save(juce::ApplicationProperties* props, const juce::String& key, const juce::StringArray& folded) {
    if (props == nullptr || props->getUserSettings() == nullptr)
        return;
    if (folded.isEmpty())
        props->getUserSettings()->removeValue(key);
    else
        props->getUserSettings()->setValue(key, folded.joinIntoString(","));
    props->saveIfNeeded();
}

} // namespace synth::ui::settingsfold
