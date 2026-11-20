#pragma once

#include "UI/Layout/ActionGlyphs.h"
#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>

// AlertIconPainter (docs/layout/theming.md#depth-levels): the alert box's severity icon at each Depth level. The icon
// is drawn in a 64 px square (kAlertIconSize); a toast's 22 px disc keeps its own look (paintSeverityDisc with the
// theme).
//
//   Flat: a bare glyph, no disc, rim or shadow: the filled rounded triangle with "!" for a warning, the filled circle
//         with "i" / "?" for info and question, in the icon colour with the mark cut out in the surface. Drawn at
//         kFlatGlyphScale (1.4) times the glyph a Soft disc holds, so a bare icon does not look small.
//   Soft: exactly today's lit disc (paintSeverityDisc with no theme), byte for byte.
//   Rich: Soft's disc and glyph unchanged (same fill, size and glyph scale) plus layers only: the shared icon tile
//   layers
//         (a drop shadow, a 1 px rim and a top highlight) around the disc. A warning also gets a triangular border,
//         outside the glyph: the glyph's own triangle stroked kRichBorderStroke on its 24-unit grid, scaled
//         kRichBorderScale (1.5) so it spans about 75% of the disc, over a faint blurred glow copy of itself (dark
//         themes only). Info and question have no border. Same hue in every theme: no neutral variant.
//
// Static: nothing here animates; a Depth change repaints the alert through the theme's own depth fade.
namespace synth::ui {

constexpr float kAlertGlyphShare = 0.52f; // the glyph's share of the disc side at Soft (and Rich info/question)
constexpr float kFlatGlyphScale = 1.4f;   // Flat: the bare glyph is this much larger than the Soft one
constexpr float kRichBorderScale = 1.5f;  // Rich warning: the border triangle, relative to the Soft glyph
constexpr float kRichBorderStroke = 1.7f; // its stroke, in units of the 24-unit glyph grid (before the scale)
constexpr float kRichGlowAlpha = 0.3f;    // the blurred glow under the border, dark themes only

/** Paints the alert icon in `area` (the largest centred square) in `ink` (the readable icon colour), the mark cut out
    in `surface`, in the look `theme.treatment.depth` asks for. */
void paintAlertIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph glyph,
                    juce::Colour surface, const synth::theme::Theme& theme);

} // namespace synth::ui
