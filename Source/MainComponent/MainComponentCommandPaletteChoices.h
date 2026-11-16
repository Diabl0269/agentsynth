#pragma once

// A dropdown setting the command palette can set in its own bar: its name, the choices, which one is current and what
// picking one does. Plain data, so the palette's header needs nothing from the UI.

#include <functional>
#include <juce_core/juce_core.h>

struct ChoiceSetting {
    juce::String key; // "settingChoice:<name>", the recents list stores it
    juce::String name;
    juce::String keywords;
    juce::StringArray choices;
    int current = -1;         // index into `choices`; -1 when the stored value is none of them
    juce::String currentText; // what the row shows at its right (the choice, or the stored value when custom)
    std::function<void(int)> apply;
    size_t entry = 0; // the palette entry that opens this setting's choices
};
