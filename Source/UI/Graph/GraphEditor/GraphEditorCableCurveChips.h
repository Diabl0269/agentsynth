#pragma once

// The curve chips drawn beside the amount knob of a modulation cable when the preference "Show curves on cables" is on.
// One rectangle serves both the paint and the hit test, so what is pressed is what is drawn. docs/layout/cables.md.

#include "GraphEditorTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>

class GraphEditor;

namespace synth::ui::cable_curve_chips {

/** Where the chip of an attenuverter-chain cable is drawn, in canvas coordinates: just right of the knob, clear of its
 *  grab radius. */
juce::Rectangle<float> chipRect(const graph_editor_types::VisibleCable& cable);

/** Draws the chip for `cable` (an attenuverter chain). Draws nothing when the preference is off. */
void paint(juce::Graphics& g, GraphEditor& editor, const graph_editor_types::VisibleCable& cable);

/** The attenuverter whose chip is under `canvasPos`, or uid 0. Always 0 while the preference is off. */
juce::AudioProcessorGraph::NodeID chipAt(GraphEditor& editor, juce::Point<float> canvasPos);

/** A press at `canvasPos`: opens that cable's curve box and returns true when it landed on a chip. `canvas` is the
 *  component `canvasPos` is relative to. */
bool press(GraphEditor& editor, juce::Component& canvas, juce::Point<float> canvasPos);

} // namespace synth::ui::cable_curve_chips
