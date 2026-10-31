#pragma once

// The menu a right-click on a cable opens: "Disconnect Cable", and "Edit curve..." for a cable whose strength can take
// a response curve. A direct mono modulation cable gets its attenuverter spliced in first. docs/layout/cables.md.

#include "GraphEditorTypes.h"
#include <vector>

class GraphEditor;

namespace synth::ui {

enum class CableMenuItem { Disconnect, EditCurve };

/** The items the menu offers for `cable`, in order. Edit curve: an attenuverter chain, or a mono direct modulation
 *  cable (not a poly bus). */
std::vector<CableMenuItem> cableMenuItemsFor(const graph_editor_types::VisibleCable& cable);

/** Replaces a direct mono modulation cable by source -> attenuverter (amount 1, linear) -> destination, on the same
 *  channels, as one undo step and one graph rebuild. Returns the new attenuverter, or uid 0 when nothing changed. */
juce::AudioProcessorGraph::NodeID spliceAttenuverterIntoCable(GraphEditor& editor,
                                                              const graph_editor_types::VisibleCable& cable);

/** Opens the cable's response-curve box at `screenPoint` (splicing an attenuverter in first for a direct cable). */
void editCableCurve(GraphEditor& editor, const graph_editor_types::VisibleCable& cable, juce::Point<int> screenPoint);

/** Builds and shows the cable menu at the pointer. */
void showCableMenu(GraphEditor& editor, const graph_editor_types::VisibleCable& cable);

} // namespace synth::ui
