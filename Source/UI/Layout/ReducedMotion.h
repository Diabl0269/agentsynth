#pragma once

#include <optional>

// ReducedMotion.h (docs/layout/animation.md#reduced-motion): whether the OS asks apps to cut non-essential motion.
// A new animation that is not needed to understand what happened (a popup easing in) asks this once when it starts
// and lands on its final state at once when it says yes.
namespace synth::ui {

/** True when the OS asks for reduced motion: macOS Reduce Motion. Other platforms answer false for now (their setting
 *  is not read yet). Message thread only. */
bool prefersReducedMotion();

/** Test seam: forces the answer; an empty value restores the OS's own. */
void setReducedMotionForTest(std::optional<bool> value);

namespace detail {
/** The macOS answer (ReducedMotionMac.mm); false everywhere else. */
bool systemPrefersReducedMotion();
} // namespace detail

} // namespace synth::ui
