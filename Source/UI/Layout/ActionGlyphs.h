#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ActionGlyphs: the small pictures of actions that appear on more than one surface, each drawn by ONE painter so
// a change of icon is one edit (bug kind action-drift). A painter draws in the largest square that fits `area`,
// centred, in `ink`; `strokeScale` thickens or thins every line (1 = the default weight).
//
// - paintShowOnCanvasGlyph: "show this module on the canvas" -- a ring with four ticks. The track header's, the
//   mixer's, the drum pad's and the mod row's button all call it (not ModDotGlyph::Crosshair, which means "aim
//   at a card" and keeps its own drawing).
namespace synth::ui {

void paintShowOnCanvasGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale = 1.0f);

} // namespace synth::ui
