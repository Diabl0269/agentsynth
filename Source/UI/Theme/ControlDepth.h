#pragma once

#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>

// ControlDepth (docs/layout/theming.md#depth-levels): the one place that says what a knob's or a fader's cap light and
// shade are at each Depth level, so every knob style and the fader follow the setting through the same numbers and a
// fader cap matches a knob cap at every level.
//
//   Flat: a flat cap: a plain fill, no contact shadow, no bevel or highlight, no rim light.
//   Soft: today's cap, untouched (radial or linear gradient, contact shadow, bevel, as each painter always drew it).
//   Rich: Soft plus a rim light on the cap's top left edge and a deeper, larger shadow.
//
// One light, from the top left. The rim is CardDepth's rim tone (the primary text colour on a dark theme, white on a
// light one), so no theme needs its own numbers. Neon (a glowing arc and a number, no body) and Ring (LEDs round a flat
// dark disc) have no cap to light and stay as they are at every level; so does the Ring fader's pointer dot.
namespace synth::theme {

/** True when the cap is a plain flat fill (Flat). */
bool capIsFlat(const Theme& theme) noexcept;

/** A cap's contact shadow alpha: Flat none, Soft `base` as drawn today, Rich deeper (1.5 times, never above 0.95). */
float capShadowAlpha(float base, const Theme& theme) noexcept;

/** How far a cap's contact shadow reaches, as a multiple of today's offset and blur: 0 Flat, 1 Soft, 1.6 Rich. */
float capShadowReach(const Theme& theme) noexcept;

/** Rich only: the rim light along `outline` (a cap's outline, inside its edge), brightest at the top left and gone by
 *  the middle. `bounds` is the cap's bounding box. A no-op below Rich. */
void paintCapRim(juce::Graphics& g, const juce::Path& outline, juce::Rectangle<float> bounds, const Theme& theme);

/** paintCapRim for a disc. */
void paintCapRimDisc(juce::Graphics& g, juce::Rectangle<float> disc, const Theme& theme);

/** paintCapRim for a rounded-rectangle cap. */
void paintCapRimRect(juce::Graphics& g, juce::Rectangle<float> cap, float radius, const Theme& theme);

} // namespace synth::theme
