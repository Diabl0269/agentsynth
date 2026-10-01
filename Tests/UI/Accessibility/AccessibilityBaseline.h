#pragma once

// AccessibilityBaseline.h -- today's accessibility gap counts per audited surface, a ratchet
// (see docs/development/accessibility.md). A count above its entry fails the test (a new control
// shipped without a name or tooltip); a count below it passes with a NOTE line asking to lower the
// entry (some controls exist only on some machines), and the change that fixed the gap lowers it. An entry holds the
// highest count across the CI platforms (macOS, Windows, Linux). NEVER raise an entry: fix the control instead. A new
// surface may only be added with its real counts.

#include <cstring>

namespace synth::test {

struct AccessibilityBaselineEntry {
    const char* surface;
    int maxMissingName;
    int maxMissingTooltip;
};

// clang-format off
inline constexpr AccessibilityBaselineEntry kAccessibilityBaseline[] = {
    {"MainComponent", 11, 6},
    {"PianoRoll", 0, 0},
    {"ModuleCards", 200, 246},
    {"ExportAudioDialog", 0, 0},
    {"Settings/Audio", 0, 0}, // Windows adds the audio driver-type drop-down; it is named from its caption too
    {"Settings/AI", 0, 0},
    {"Settings/Keyboard Shortcuts", 0, 0},
    {"Settings/Preferences", 0, 0},
    {"Settings/Appearance", 0, 0},
    {"Settings/Feedback", 0, 0},
    {"DualIOPerModulePopup", 0, 0},
    {"SignInDialog", 0, 0},
    {"MacroPortConfigDialog", 0, 0},
    {"MacroAutoPortPromptDialog", 0, 0},
    {"EQWindow", 0, 0},
    {"ModuleViews", 0, 0},
    {"WelcomeScreen", 0, 0},
    {"ColourPickerPopup", 0, 0},
    {"ModuleLibraryHelpPopup", 0, 0},
};
// clang-format on

inline const AccessibilityBaselineEntry* findAccessibilityBaseline(const char* surface) {
    for (const auto& e : kAccessibilityBaseline)
        if (std::strcmp(e.surface, surface) == 0)
            return &e;
    return nullptr;
}

} // namespace synth::test
