#pragma once

// The chips drawn on a cable's wire: the curve chips beside the amount knob of a modulation cable when the preference
// "Show curves on cables" is on (one rectangle serves both the paint and the hit test, so what is pressed is what is
// drawn), and the "xN" voice-count badge of a poly bus (polyBadgeRect). docs/layout/cables.md.

#include "GraphEditorTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

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

/** Where the "xN" voice badge of a poly bus (a pill `width` wide) is drawn: beside the cable's midpoint, or, when a
 * card (`cards`: the visible cards' canvas bounds) covers that spot (the cable layer paints behind cards), slid along
 * the wire to the nearest spot no card covers, so the whole label shows. Canvas coordinates. Falls back to the midpoint
 * when the whole wire is under cards (the paint then draws it over the cards: polyBadgeRectIfClear). */
juce::Rectangle<float> polyBadgeRect(const graph_editor_types::VisibleCable& cable, float width,
                                     const std::vector<juce::Rectangle<int>>& cards);

/** The same spot, or nothing when every spot along the wire is under a card. The caller then draws the pill in the pass
 *  that paints over the cards, at polyBadgeRect's midpoint spot, so it never shows only its "x". */
std::optional<juce::Rectangle<float>> polyBadgeRectIfClear(const graph_editor_types::VisibleCable& cable, float width,
                                                           const std::vector<juce::Rectangle<int>>& cards);

} // namespace synth::ui::cable_curve_chips
