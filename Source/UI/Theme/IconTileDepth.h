#pragma once

#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>

// IconTileDepth (docs/layout/theming.md#depth-levels): the one place that says what an icon tile's light and shade are
// at each Depth level, so the top bar's chips, the framed icon buttons on card headers and the toast's severity disc
// follow the setting through the same code. A tile is a rounded square (or, with a radius of half its side, a disc) in
// a hue.
//
//   Flat: a plain tile, no ring, no highlight, no shadow, no glow.
//   Soft: today's tile, untouched (each surface keeps the fill it always painted).
//   Rich: Soft's tile (same fill, silhouette, size and glyph) plus layers only: a faint radial light from the top left
//         (the hue on a dark theme, white on a light one), a 1px hue ring, a highlight along the top edge and a
//         small drop shadow. An active tool also gets a soft glow of its hue and an inset shade along its bottom.
//
// The painters below only add the Rich layers around a surface's own fill; each is a no-op below Rich, so Soft and Flat
// stay byte-identical to what the surface painted before. One light, from the top left. Every colour comes from the
// theme's tokens (CardDepth's tones: the light is the primary text colour on a dark theme and white on a light one).
namespace synth::theme {

/** Drop shadow and active glow, painted before the tile's fill so they sit under it. `active` is 0..1 (a toggle that is
 *  lit). A no-op below Rich. */
void paintIconTileUnder(juce::Graphics& g, juce::Rectangle<float> tile, float radius, juce::Colour hue, float active,
                        const Theme& theme);

/** The radial wash of the hue, the 1px hue ring and the top highlight, painted over the tile's fill; an active tile
 *  adds the inset shade along its bottom. `wash` is the strength of the radial light (0..1, 1 for a tile whose fill is
 *  faint; a solid lit tile passes 0). A no-op below Rich. */
void paintIconTileOver(juce::Graphics& g, juce::Rectangle<float> tile, float radius, juce::Colour hue, float active,
                       float wash, const Theme& theme);

} // namespace synth::theme
