#pragma once

// The Mod Matrix's Curve column: the chip after Amount that shows a routing's response curve and opens its editor,
// the same curve the mod dot's row and the cable edit. docs/modules/modulation.md#the-modulation-curve.

#include "UI/ModuleViews/ModCurve/ModCurveChip.h"
#include <juce_audio_processors/juce_audio_processors.h>

class GraphEditor;

namespace synth::ui::mod_matrix_curve {

/** The width of the column, gutters included. */
inline constexpr int kColumnWidth = 40;

/** Points `chip` at the routing's attenuverter. A matrix with no canvas (a headless one) hides the chip instead. */
void wireChip(ModCurveChip& chip, GraphEditor* editor, juce::AudioProcessorGraph::NodeID attenuverter);
/** Takes the column off the right edge of `area` and centres the chip in it. */
void layoutChip(ModCurveChip& chip, juce::Rectangle<int>& area);
/** The screen-reader name for row `index` (zero-based): "Routing N curve". */
void nameChip(ModCurveChip& chip, int index);
/** Draws the "Curve" header over the column. `rowsRight` is the x where the row's bypass and delete buttons begin. */
void drawHeader(juce::Graphics& g, juce::Rectangle<float> headerCell);

} // namespace synth::ui::mod_matrix_curve
