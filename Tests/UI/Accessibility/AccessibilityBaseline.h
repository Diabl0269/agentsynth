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
    {"ModuleCards", 202, 248},
    {"ExportAudioDialog", 6, 10},
    {"Settings/Audio", 5, 5}, // Windows adds the audio driver-type drop-down; macOS/Linux read 4/4
    {"Settings/AI", 4, 0},
    {"Settings/Keyboard Shortcuts", 20, 0},
    {"Settings/Preferences", 5, 0},
    {"Settings/Appearance", 7, 11},
    {"Settings/Feedback", 2, 2},
};
// clang-format on

inline const AccessibilityBaselineEntry* findAccessibilityBaseline(const char* surface) {
    for (const auto& e : kAccessibilityBaseline)
        if (std::strcmp(e.surface, surface) == 0)
            return &e;
    return nullptr;
}

} // namespace synth::test
