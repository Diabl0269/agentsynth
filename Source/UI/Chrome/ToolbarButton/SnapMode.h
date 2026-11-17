#pragma once

// Which directions a curve editor's Snap works in. Pure: the picker (SnapModeButton), the Filter grid and the curve
// window map it to their grids, and the tests check the mapping without a component.
#include <juce_core/juce_core.h>

namespace synth::ui {

enum class SnapMode { TimeAndLevel, TimeOnly, LevelOnly };

inline constexpr SnapMode kSnapModes[] = {SnapMode::TimeAndLevel, SnapMode::TimeOnly, SnapMode::LevelOnly};

/** Whether points land on the lines across (time, left to right). */
inline constexpr bool snapsTime(SnapMode mode) noexcept { return mode != SnapMode::LevelOnly; }
/** Whether points land on the lines up and down (level). */
inline constexpr bool snapsLevel(SnapMode mode) noexcept { return mode != SnapMode::TimeOnly; }

/** The text saved in the settings: "timeAndLevel", "timeOnly" or "levelOnly". */
inline juce::String snapModeKey(SnapMode mode) {
    switch (mode) {
    case SnapMode::TimeOnly:
        return "timeOnly";
    case SnapMode::LevelOnly:
        return "levelOnly";
    case SnapMode::TimeAndLevel:
        break;
    }
    return "timeAndLevel";
}

/** The mode a saved key names; an unknown or missing key reads as TimeAndLevel. */
inline SnapMode snapModeFromKey(const juce::String& key) {
    for (const auto mode : kSnapModes)
        if (snapModeKey(mode) == key)
            return mode;
    return SnapMode::TimeAndLevel;
}

/** The name the menu, tooltip and screen reader use: "Time and level", "Time only", "Level only". */
inline juce::String snapModeName(SnapMode mode) {
    switch (mode) {
    case SnapMode::TimeOnly:
        return "Time only";
    case SnapMode::LevelOnly:
        return "Level only";
    case SnapMode::TimeAndLevel:
        break;
    }
    return "Time and level";
}

} // namespace synth::ui
