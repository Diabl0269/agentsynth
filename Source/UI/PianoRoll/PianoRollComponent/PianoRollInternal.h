#pragma once

// Private to the PianoRollComponent translation units (PianoRollComponent.cpp,
// PianoRollScaleAssist.cpp, PianoRollPainting.cpp, PianoRollEditTools.cpp, PianoRollAudition.cpp,
// PianoRollClipboardAndKeys.cpp, PianoRollMouse.cpp, PianoRollZoom.cpp, PianoRollVelocity.cpp). Not a CMake source file
// — each unit that needs one of these constants/helpers includes this header directly and brings the names into scope
// with `using namespace synth::ui::detail;`, so every call site keeps its unqualified spelling.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::detail {

// The velocity-scrub chord: Ctrl on macOS (where Cmd is a separate flag), Ctrl+Alt elsewhere (where
// isCommandDown() IS isCtrlDown(), so plain Ctrl is the unsnapped-move chord). One expression, no
// platform #ifdef.
inline bool isVelocityScrubChord(const juce::ModifierKeys& mods) noexcept {
    return (mods.isCtrlDown() && !mods.isCommandDown()) || (mods.isCtrlDown() && mods.isAltDown());
}

// Wheel tuning. kZoomWheelSensitivity mirrors TimelinePanelComponent::mouseWheelMove's own constant
// (exponential in deltaY, so equal-and-opposite gestures cancel exactly) and
// kScrollPixelsPerWheelUnit mirrors its scroll constant — both duplicated rather than shared
// because they are one-line cosmetic tunings private to a different translation unit.
inline constexpr double kZoomWheelSensitivity = 2.0;
inline constexpr double kScrollPixelsPerWheelUnit = 200.0;
inline constexpr double kPitchScrollSemitonesPerWheelUnit = 3.0;

// Edge-auto-scroll tuning (see EdgeAutoScroll.h). Horizontal mirrors
// TimelineClipLaneArea's kEdgeAutoScrollMaxPxPerTick exactly — the roll's own drag should feel
// identical. Vertical is ROWS, not pixels (visiblePitches_ can collapse rows to less than
// pixelsPerSemitone_ apart in screen terms — a row is the unit that means something), and ~1 row
// per tick at kEdgeScrollHz is a brisk-but-followable walk up/down the keyboard. That maximum is
// only reached at full zone penetration (right at the edge); autoScrollTick applies the
// shallower-penetration fraction of it straight to topRowPosition_, uncoarsened.
inline constexpr double kEdgeAutoScrollMaxPxPerTick = 18.0;
inline constexpr double kEdgeAutoScrollMaxRowsPerTick = 1.0;

// The per-level gridline density guard now lives with the rest of the grid's colour/visibility
// policy in TimelineClipLaneArea.h (synth::ui::kMinGridLinePixels / gridLevelIsReadable), shared
// with the surface that paints the timeline lanes' grid so the two can never disagree about when a
// level is too dense to read.

// ---- Default surface-key bindings (used when no ShortcutManager is installed) ----
// One table so the fallbacks and the ShortcutManager action ids sit side by side and cannot drift:
// every entry is BOTH the id keyPressed() resolves and the key it falls back to. A sibling phase
// adds the same ids (and the same defaults) to ShortcutManager::resetToDefaults(); nothing here
// requires that to have happened, because getBinding() answers an unknown id with an invalid
// KeyPress and an invalid binding simply matches nothing.
inline juce::KeyPress plainKey(int keyCode) noexcept {
    return juce::KeyPress(keyCode, juce::ModifierKeys::noModifiers, 0);
}
inline juce::KeyPress modKey(int keyCode, int modifierFlags) noexcept {
    return juce::KeyPress(keyCode, juce::ModifierKeys(modifierFlags), 0);
}

// "pianoRollToggleVelocityLane"'s fallback: a REAL Ctrl+V on macOS (Cmd+V is Paste there, and Ctrl
// is a separate physical key), Cmd+Shift+V elsewhere — on Windows/Linux JUCE's Cmd IS Ctrl, so
// Ctrl+V would be Paste. Same per-platform split as the AI panel's Ctrl+A / Cmd+Shift+A default.
inline juce::KeyPress velocityLaneToggleKey() noexcept {
#if JUCE_MAC
    return modKey('v', juce::ModifierKeys::ctrlModifier);
#else
    return modKey('v', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
#endif
}

// The properties-file key remembering whether the velocity strip is shown (absent = shown).
inline const char* velocityLaneVisibleKey() noexcept { return "pianoRollVelocityLaneVisible"; }
// ... and the height the user dragged it to (absent = PianoRollVelocityLane::kDefaultHeight).
inline const char* velocityLaneHeightKey() noexcept { return "pianoRollVelocityLaneHeight"; }

inline bool isBlackKeyPitchClass(int pitchClass) noexcept {
    switch (pitchClass) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

// Tolerance for the "is this cut strictly inside the note" / "does this paste still fit" beat
// comparisons. Beats are doubles that have been through a snap multiply-divide, so an exact
// comparison would reject a cut that IS on a grid line by a few ULPs.
inline constexpr double kBeatEpsilon = 1.0e-9;

// Same tolerance, same reasoning, for pitchForY's row-index inversion (see its definition): a
// should-be-exact row boundary can come out a hair to the wrong side of the integer std::ceil needs
// to land on after a divide-then-subtract round trip through doubles.
inline constexpr double kRowEpsilon = 1.0e-9;

} // namespace synth::ui::detail
