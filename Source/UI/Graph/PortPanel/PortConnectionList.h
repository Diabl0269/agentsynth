#pragma once

// What a jack is wired to, as the user sees it: one entry per drawn cable (never per graph edge) that lands on the
// jack, with the name of the module and port at the far end and the cable's colour. The port connections panel lists
// these; this is pure data, so it is tested without a window. docs/layout/cables.md#port-connections-panel.

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <vector>

namespace synth::ui {

/** One jack: the node, its VISIBLE jack index (what a click on the card reports, not a raw channel) and its kind. */
struct PortRef {
    juce::AudioProcessorGraph::NodeID node;
    int jack = 0;
    bool isInput = false;
    bool isMidi = false;

    bool operator==(const PortRef& o) const noexcept {
        return node == o.node && jack == o.jack && isInput == o.isInput && isMidi == o.isMidi;
    }
};

struct PortConnection {
    GraphEditor::VisibleCable cable;
    PortRef far;           // the jack at the other end (VISIBLE index; a MIDI jack answers the MIDI channel index)
    int farRawChannel = 0; // the RAW channel there: a knob's CV channel, what a Knob target is matched by
    juce::String label;    // "<other module title> . <other port name>" (a macro port: its own name)
    juce::Colour colour;
};

/** The cables landing on `port` as of now, in the canvas' own order (a copy: the editor's list is a memo). */
std::vector<PortConnection> listPortConnections(GraphEditor& editor, const PortRef& port);
/** "<module title> . <port name>" for the jack itself (a macro port: its own name); empty when the node is gone. */
juce::String portTitle(GraphEditor& editor, const PortRef& port);
/** The jack's own name ("Out 1", "Cutoff CV", "Midi input"); empty when the node is gone. */
juce::String jackName(GraphEditor& editor, const PortRef& port);
/** " . " as the panel writes it between a module and a port. */
juce::String portSeparator();
/** "No connections yet", "1 connection" or "N connections". */
juce::String connectionCountText(int count);

} // namespace synth::ui
