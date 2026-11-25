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
//   Soft: a warning is Flat's bare triangle (same size and place) with a small soft shadow; info and question are the
//         lit disc a toast draws (paintSeverityDisc with no theme), byte for byte.
//   Rich: Soft's icon plus light and shade only, never a shape. A warning keeps Soft's triangle outline exactly and
//         adds a deeper shadow, a top light fading to a darker foot and a fine light line inside its top edges, all
//         inside the outline; info and question add the shared icon tile layers (a drop shadow, a 1 px rim and a top
//         highlight) around Soft's disc. Same hue in every theme: no neutral variant.
//
// Static: nothing here animates; a Depth change repaints the alert through the theme's own depth fade.
namespace synth::ui {

constexpr float kAlertGlyphShare = 0.52f; // the glyph's share of the disc side in an info or question disc
constexpr float kFlatGlyphScale = 1.4f;   // a bare glyph (Flat, and a warning at every level) is this much larger

/** Paints the alert icon in `area` (the largest centred square) in `ink` (the readable icon colour), the mark cut out
    in `surface`, in the look `theme.treatment.depth` asks for. */
void paintAlertIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph glyph,
                    juce::Colour surface, const synth::theme::Theme& theme);

} // namespace synth::ui
