#pragma once

#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The Draw shapes' icons: one stroke path per DrawShape in a 24 x 24 box, drawn with a 1.8 px round stroke at that
// size. The pen button and the flyout rows share them, so the button always shows what a row would draw.

/** The shape's icon as a path in the 24 x 24 box. Free is a pen drawing a wobbly line. */
juce::Path drawShapeIconPath(DrawShape shape);

/** Strokes the icon centred in `area` (scaled to fit, stroke scaled with it) in `colour` at `alpha`. */
void paintDrawShapeIcon(juce::Graphics& g, DrawShape shape, juce::Rectangle<float> area, juce::Colour colour,
                        float alpha = 1.0f);

} // namespace synth::ui
