#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * @brief The one wheel-interpretation policy shared by the piano roll and the timeline panel.
 *
 * Two facts about JUCE wheel events that every consumer here must respect, both verified against
 * juce_MouseEvent.h and juce_Viewport.cpp rather than assumed:
 *
 * 1. The deltas are already OS-direction-adjusted. `MouseWheelDetails::isReversed` only REPORTS
 *    whether the user's OS has "natural" scrolling on — the deltas themselves are pre-flipped so
 *    that JUCE's own Viewport can apply the same `viewPosition -= delta` on every platform and
 *    always feel native. So "natural" for this app means copying the Viewport convention, never
 *    re-reading `isReversed` to flip anything a second time.
 *
 * 2. macOS folds Shift+wheel into `deltaX` (the OS-level axis swap), so a modifier branch that
 *    reads only `deltaY` — e.g. Cmd+Shift+wheel = vertical zoom — receives exactly 0.0 and
 *    silently does nothing. Viewport guards this with `deltaX != 0 ? deltaX : deltaY`; every
 *    zoom/scroll branch that conceptually wants "the amount the wheel moved" must do the same,
 *    via dominantWheelDelta().
 */

/** The wheel movement regardless of which axis the OS parked it on — use this in any branch whose
 *  MODIFIERS already decide the meaning (zoom in/out, vertical zoom), where reading a single axis
 *  would go dead under macOS's Shift axis swap. */
inline float dominantWheelDelta(const juce::MouseWheelDetails& wheel) noexcept {
    return wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
}

/**
 * The amount to ADD to a view origin for a plain scroll, in SCREEN orientation (+x = view moves
 * right, +y = view moves down). Natural (invert == false) is the JUCE Viewport convention
 * (`viewPosition -= delta`): content follows the gesture the way every native app does. The
 * invert flag is the app-level preference stacked ON TOP of the OS setting — the OS already had
 * its say inside the delta (fact 1 above).
 *
 * Consumers whose coordinate grows the other way (the piano roll's firstVisiblePitch_ is the
 * pitch at the TOP row, so "view moves down" means that pitch DECREASES) must apply their own
 * axis mapping on top of this, with a comment saying so at the call site.
 */
inline float scrollAmount(float delta, bool invertScroll) noexcept { return invertScroll ? delta : -delta; }

} // namespace synth::ui
