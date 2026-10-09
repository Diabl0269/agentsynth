#pragma once

// PortConnector.h -- the ways the port connections panel (and the cable drag) put a new connection on the graph, in
// one place so they cannot drift apart. A friend of GraphEditor: it needs the same private seams the cable drop used
// (the Track In auto channel, the macro auto ports, the slide of a cable out of a minted port).
//
//  * connectJacks         the drop rules for two jacks: a poly fan, MIDI, a mono CV jack through its attenuverter, the
//                         macro-boundary auto ports and a Track In's auto channel. The cable drag calls it on a
//                         release; the panel calls it for a picked jack.
//  * connectToJack/Knob   what a panel pick does: one undo step, the new cable growing out of its source jack.
//  * addModuleAndConnect  a new module beside the jack's card, its first compatible jack cabled to it: the card, its
//                         place, the cable and any macro join, as ONE undo step.
//
// docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include <optional>

namespace synth::ui {

class PortConnector {
public:
    struct Result {
        bool connected = false;        // the connection was made
        bool mintedMacroPorts = false; // the cable crossed a macro boundary and macro ports were made for it
    };

    /** Wires `srcId`'s output `srcJack` to `dstId`'s input `dstJack` (VISIBLE jack indices; a MIDI jack passes the
     *  MIDI channel index) with the rules of a completed cable drag. `recordUndo` false leaves the undo step to the
     *  caller. With `slideFrom` (a canvas point) the cables a macro-boundary drop makes slide in from there. */
    static Result connectJacks(GraphEditor& editor, juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                               juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi, bool recordUndo,
                               std::optional<juce::Point<float>> slideFrom = std::nullopt);

    /** Cables `jack` to `other` (an output and an input, either way round) with the drop rules, as one undo step; the
     *  new cable grows out of its source jack. */
    static Result connectToJack(GraphEditor& editor, const PortRef& jack, const PortRef& other);

    /** The output `jack` onto raw CV channel `destChannel` of `destNode`: a routing at the mod dot's new-source
     *  depth, one undo step, the cable growing in. True when it was made. */
    static bool connectToKnob(GraphEditor& editor, const PortRef& jack, juce::AudioProcessorGraph::NodeID destNode,
                              int destChannel);

    /** A new module of factory type `typeName` beside `jack`'s card, its first jack that can take `jack`'s cable
     *  cabled to it, as ONE undo step. The new node, or an invalid id when it could not be made. */
    static juce::AudioProcessorGraph::NodeID addModuleAndConnect(GraphEditor& editor, const PortRef& jack,
                                                                 const juce::String& typeName);

    /** The first visible jack of a throwaway `typeName` module that can take a cable from `jack` (an output wants an
     *  input of the same kind and the other way round); -1 when it has none. MIDI answers the MIDI channel index. */
    static int firstCompatibleJack(const juce::String& typeName, const PortRef& jack);

    /** The raw output channel a routing reads for visible output `jack` of `node`; -1 when the node is gone. */
    static int rawSourceChannel(GraphEditor& editor, juce::AudioProcessorGraph::NodeID node, int jack);
};

} // namespace synth::ui
