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
//   Rich: the lit disc with a stronger wash (the icon colour radially from the top left, 52% to 8% instead of 38% to
//         10%), a 1 px rim from CardDepth's highlight (top left) to its shade (bottom right), and a soft drop shadow.
//         A warning also gets a triangular border: the glyph's own triangle stroked kRichBorderStroke on its 24-unit
//         grid, scaled kRichBorderScale (1.5) so it spans about 75% of the disc, over a faint blurred glow copy of
//         itself, with the filled glyph at kRichInnerScale (0.58) inside it, centred. Info and question have no border.
//         On a light theme Rich is neutral: no wash, no glow; the disc is the icon colour at 12% over white with a 50%
//         ring, and the glyph sits centred in the same triangular border.
//
// Static: nothing here animates; a Depth change repaints the alert through the theme's own depth fade.
namespace synth::ui {

constexpr float kAlertGlyphShare = 0.52f; // the glyph's share of the disc side at Soft (and Rich info/question)
constexpr float kFlatGlyphScale = 1.4f;   // Flat: the bare glyph is this much larger than the Soft one
constexpr float kRichBorderScale = 1.5f;  // Rich warning: the border triangle, relative to the Soft glyph
constexpr float kRichBorderStroke = 1.7f; // its stroke, in units of the 24-unit glyph grid (before the scale)
constexpr float kRichInnerScale = 0.58f;  // the filled glyph inside the border, relative to the border triangle
constexpr float kRichWashStart = 0.52f;   // Rich's radial wash, alpha at the light
constexpr float kRichWashEnd = 0.08f;     // and at the edge
constexpr float kRichGlowAlpha = 0.5f;    // the blurred glow under the border, dark themes only
constexpr float kRichNeutralFill = 0.12f; // light theme Rich: share of the icon colour mixed over white for the disc
constexpr float kRichNeutralRing = 0.5f;  // and for its ring

/** Paints the alert icon in `area` (the largest centred square) in `ink` (the readable icon colour), the mark cut out
    in `surface`, in the look `theme.treatment.depth` asks for. */
void paintAlertIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph glyph,
                    juce::Colour surface, const synth::theme::Theme& theme);

} // namespace synth::ui
