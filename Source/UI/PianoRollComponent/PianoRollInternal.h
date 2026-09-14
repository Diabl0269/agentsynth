#pragma once

// Private to the PianoRollComponent translation units (PianoRollComponent.cpp,
// PianoRollScaleAssist.cpp, PianoRollPainting.cpp, PianoRollEditTools.cpp, PianoRollAudition.cpp,
// PianoRollClipboardAndKeys.cpp, PianoRollMouse.cpp, PianoRollZoom.cpp). Not a CMake source file —
// each unit that needs one of these constants/helpers includes this header directly and brings the
// names into scope with `using namespace synth::ui::detail;`, so every call site keeps its original,
// unqualified spelling. Extracted verbatim from PianoRollComponent.cpp's old anonymous namespace
// (FRO64); no behavior change.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::detail {

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
