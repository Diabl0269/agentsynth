#pragma once

#include <juce_core/juce_core.h>

// The "Show button labels" preference (docs/layout/settings-preferences.md): whether the top bar and the canvas
// toolbar print a caption under each button's icon. Stored as a bool in the user settings, on when absent.
namespace synth::ui {

inline constexpr const char* kShowToolbarLabelsKey = "showToolbarLabels";

// True when the captions are wanted. `settings` may be null (a headless caller): then the default, on.
inline bool showToolbarLabels(const juce::PropertiesFile* settings) {
    return settings == nullptr || settings->getBoolValue(kShowToolbarLabelsKey, true);
}

} // namespace synth::ui
