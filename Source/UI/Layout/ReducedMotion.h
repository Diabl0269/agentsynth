#pragma once

#include <juce_core/juce_core.h>
#include <optional>

// ReducedMotion.h (docs/layout/animation.md#reduced-motion): whether the OS asks apps to cut non-essential motion.
// A new animation that is not needed to understand what happened (a popup easing in) asks this once when it starts
// and lands on its final state at once when it says yes.
namespace synth::ui {

/** The Animations preference: how much the app animates. followSystem lets the OS decide (macOS Reduce Motion), full
 *  always animates, reduced behaves as if Reduce Motion were on, off makes everything land instantly. */
enum class AnimationMode { followSystem, full, reduced, off };

/** The process-wide mode (default followSystem). Set at startup from the user settings and whenever the preference
 *  changes. Message thread only. */
AnimationMode animationMode();
void setAnimationMode(AnimationMode mode);

/** The user-settings key and the string each mode is stored as; an unknown string reads as followSystem. */
constexpr const char* kAnimationModeKey = "animationMode";
juce::String animationModeToString(AnimationMode mode);
AnimationMode animationModeFromString(const juce::String& text);

/** True when the app should cut non-essential motion: followSystem asks the OS (macOS Reduce Motion; other
 *  platforms answer false for now), full says false, reduced and off say true. Message thread only. */
bool prefersReducedMotion();

/** True when the mode is Off: animations run for no time at all and land on their final state at once. */
bool animationsOff();

/** A duration in ms by mode: `full` when animating fully, `reduced` under reduced motion, 0 under Off. Use it where
 *  a duration already branches on prefersReducedMotion(). */
double motionMs(double full, double reduced);

/** Test seam: forces the answer; an empty value restores the OS's own. */
void setReducedMotionForTest(std::optional<bool> value);

namespace detail {
/** The macOS answer (ReducedMotionMac.mm); false everywhere else. */
bool systemPrefersReducedMotion();
} // namespace detail

} // namespace synth::ui
