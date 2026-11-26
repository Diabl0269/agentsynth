#pragma once

#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>

// CardDepth (docs/layout/theming.md#depth-levels): the one place that says what a card's light and shade are at each
// Depth level, so module cards, macro cards and mixer columns follow the setting through the same code.
//
//   Flat: a plain fill and a border. No drop shadow, no gradient, no texture, no glass sheen, no rim, no seam.
//   Soft: today's card, untouched (the module card's own painter keeps its shadow, gradient and sheen as they were).
//   Rich: Soft, plus a rim light on the left and top edges, a deeper inner shade, an accent seam under the card
//         header, and a stronger drop shadow. A card never moves on hover at any level.
//
// One light, from the top left. Every colour comes from the theme's tokens (the light is the primary text colour on a
// dark theme and white on a light one, the shade is black on a dark theme and the primary text colour on a light one),
// so no theme needs its own numbers. Light themes get no accent seam: Rich there is cleaner and neutral.
namespace synth::theme {

/** The colours a card's light and shade are drawn with, from the theme alone. */
struct CardDepthTones {
    juce::Colour rimTop; // the 1px line just inside the top edge (no left or right rim: the two edges must read alike)
    juce::Colour shade;  // the inner shade along the bottom
    juce::Colour seam;   // the line under a card header (transparent on a light theme)
};

CardDepthTones cardDepthTones(const Theme& theme);

/** How strong the drop shadow is at `level`, as a multiple of today's: 0 Flat, 1 Soft, more on Rich. */
float cardShadowScale(DepthLevel level) noexcept;

/** The stepped drop shadow under a card body, today's three translucent rounded rects scaled by the level. Soft
 *  paints exactly what the card painter always did. Flat paints nothing. */
void paintCardDropShadow(juce::Graphics& g, juce::Rectangle<float> body, float radius, float shadowStrength,
                         DepthLevel level);

/** Rich only: the deeper inner shade along the bottom of a card body (a gradient plus a 1px line). Call after the body
 *  fill, before anything that must sit above it. A no-op below Rich. */
void paintCardInnerShade(juce::Graphics& g, juce::Rectangle<float> body, float radius, const Theme& theme);

/** Rich only: the rim light on the top edge of a card body, one pixel inside the border. Call after the
 *  border. A no-op below Rich. */
void paintCardRim(juce::Graphics& g, juce::Rectangle<float> body, float radius, const Theme& theme);

/** Rich only: the accent seam one pixel under a header band whose bottom hairline is at `hairlineY`. A no-op below Rich
 *  and on a light theme. */
void paintCardHeaderSeam(juce::Graphics& g, juce::Rectangle<float> header, const Theme& theme);

/** Rich only, for a card with no header or drop shadow of its own (a macro card, a mixer column): the inner shade
 *  and the rim in one call. Rounded when `radius` > 0, square otherwise. A no-op below Rich. */
void paintCardRichLight(juce::Graphics& g, juce::Rectangle<float> body, float radius, const Theme& theme);

/** Whether a card draws the Glass style's one-pixel highlight just inside its top edge. A selected card does not: the
 *  accent outline and its inner ring already sit there, and the line would show as a grey stripe under them. */
inline bool cardPaintsTopHighlight(const Theme& theme, bool selected) noexcept {
    return !selected && theme.treatment.style == ThemeStyle::Glass && theme.treatment.depth != DepthLevel::Flat;
}

} // namespace synth::theme
