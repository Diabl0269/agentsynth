#pragma once

#include "Modules/FX/ParametricEQ/ParametricEQBandTypes.h"
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

// ActionGlyphs: the small pictures of actions that appear on more than one surface, each drawn by ONE painter so
// a change of icon is one edit (bug kind action-drift). A painter draws in the largest square that fits `area`,
// centred, in `ink`; `strokeScale` thickens or thins every line (1 = the default weight).
//
// - paintShowOnCanvasGlyph: "show this module on the canvas" -- a ring with four ticks. The track header's, the
//   mixer's, the drum pad's and the mod row's button all call it (not ModDotGlyph::Crosshair, which means "aim
//   at a card" and keeps its own drawing).
// - paintVoiceGlyph: a track's voice mode -- one note (head and stem) for mono, a chord of three heads on one stem for
// poly, the chord outlined
//   for a track that is half and half.
// - paintResponseGlyph: "show the filter's response" -- a low-pass curve (flat, a small bump, then the roll-off) over a
//   baseline. The Filter picture's Show Response icon draws it.
// - paintSpectrumGlyph: "show the spectrum" -- four bars of different heights on a baseline. The Filter picture's Show
//   Spectrum icon draws it.
// - paintSeverityDisc: how serious a message is -- a lit disc holding a rounded "i" (info), "?" (question), triangle
//   with "!" (warning) or "x" (error). Alert boxes and toasts paint it; any other message surface takes the same
//   drawing.
// - paintWindowGlyph: "open this in its own window" -- a window frame with a title bar. The Parametric EQ card's Open
// EQ
//   Window icon.
// - paintHeadphonesGlyph: "listen to this alone" (solo) -- a headband over two ear cups. The EQ window's band solo
//   button draws it.
// - paintEqBandTypeGlyph: a Parametric EQ band's type -- a tiny stroked drawing of that type's response curve (bell,
//   shelves, cuts, notch, band pass, tilt). Unlike the others it fills a 7:5 box (not a square) in `area`. Every
//   surface that shows a band's type draws it (the strip chip, the readout, the type pill, the type menu); the name is
//   for tooltips and screen readers.
namespace synth::ui {

enum class VoiceGlyph { mono, poly, mixed };
enum class SeverityGlyph { info, warning, error, question };

void paintShowOnCanvasGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

void paintVoiceGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, VoiceGlyph voice,
                     float strokeScale = 1.0f);

void paintHeadphonesGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

void paintResponseGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

void paintSpectrumGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

void paintWindowGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

/** The lit disc: a round-cornered glyph (warning triangle, info, question, error) on a disc lit from the top left, in
    `ink`, with a fine rim and a soft shadow under the glyph. `cut` is the surface behind it: the mark inside the glyph
    is drawn in it, so it reads as a hole. Alert boxes (64 px) and toasts (22 px) both draw it. `theme` supplies the
   Depth level (none, the default, is Soft): Soft is the disc as described, Flat a plain disc, Rich adds the shared tile
   light (theme/IconTileDepth.h). */
void paintSeverityDisc(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph severity,
                       juce::Colour cut, const synth::theme::Theme* theme = nullptr);

/** The glyph of the lit disc on its own, on a 24-unit grid of side `glyphSide` centred on `centre`: the filled body
    (rounded triangle, or circle for info, question and error) in `ink` with the mark cut out in `cut`. The disc painter
    calls it at 52% of the disc; `shadow` adds the small soft shadow under the body (`large` sizes it for a 64 px icon
    rather than a 22 px one). The alert icon's Flat and Rich looks call it too (AlertIconPainter.h). */
void paintSeverityShape(juce::Graphics& g, juce::Point<float> centre, float glyphSide, juce::Colour ink,
                        SeverityGlyph severity, juce::Colour cut, bool shadow, bool large);

/** The body path `paintSeverityShape` fills (the warning triangle is nudged up 1.3 units to look centred), placed
    the same way, so a border drawn round it follows it exactly. */
juce::Path severityBodyPath(SeverityGlyph severity, juce::Point<float> centre, float glyphSide);

void paintEqBandTypeGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, synth::eq::BandType type,
                          float strokeScale = 1.0f);

} // namespace synth::ui
