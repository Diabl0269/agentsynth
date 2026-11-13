#pragma once

// DragPreviewGrid.h
//
// The dot grid the patch canvas shows while a card is dragged: one dot per major grid step (five snap steps). See
// docs/layout/rendering.md "Per-frame work does not grow with the patch".

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** Paints the grid over `g`'s clip, which must be in canvas coordinates. Its cost does not grow with the clip. */
void paintDragPreviewGrid(juce::Graphics& g, juce::Colour dotColour);

} // namespace synth::ui
