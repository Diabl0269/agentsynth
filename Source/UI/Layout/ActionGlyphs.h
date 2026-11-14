#pragma once

#include "Modules/FX/ParametricEQ/ParametricEQBandTypes.h"
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
// - paintSeverityGlyph: how serious a message is -- a circled "i" for info, a triangle with "!" for a warning, a
//   circled "x" for an error. The toast paints it; any other message surface takes the same drawing.
// - paintHeadphonesGlyph: "listen to this alone" (solo) -- a headband over two ear cups. The EQ window's band solo
//   button draws it.
// - paintEqBandTypeGlyph: a Parametric EQ band's type -- a tiny stroked drawing of that type's response curve (bell,
//   shelves, cuts, notch, band pass, tilt). Unlike the others it fills a 7:5 box (not a square) in `area`. Every
//   surface that shows a band's type draws it (the strip chip, the readout, the type pill, the type menu); the name is
//   for tooltips and screen readers.
namespace synth::ui {

enum class VoiceGlyph { mono, poly, mixed };
enum class SeverityGlyph { info, warning, error };

void paintShowOnCanvasGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

void paintVoiceGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, VoiceGlyph voice,
                     float strokeScale = 1.0f);

void paintHeadphonesGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

void paintSeverityGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph severity,
                        float strokeScale = 1.0f);

void paintEqBandTypeGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, synth::eq::BandType type,
                          float strokeScale = 1.0f);

} // namespace synth::ui
