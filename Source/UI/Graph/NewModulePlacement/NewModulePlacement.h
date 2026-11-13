#pragma once

// NewModulePlacement.h
//
// Where a module an AI edit plan adds without a "position" lands: beside what it connects to, never on top of a
// card (docs/ai/timeline-ops.md#where-things-land). Runs inside the plan's undo transaction, after its patch.

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class GraphEditor;

namespace synth {

/**
 * Gives every node in `created` (creation order) that has no stored position a spot, one node at a time:
 * - the source of a connection or modulation into an already-placed node goes LEFT of its first such destination;
 * - otherwise the destination of a connection from an already-placed node goes RIGHT of that source;
 * - otherwise it goes below the last node this call placed (the canvas' left edge below everything, for the first).
 * Rows are kept: the spot is the anchor's row, walked down in grid steps until it overlaps no card. A node placed
 * here counts as placed for the rest, so a chain lays out left to right. MIDI cables and the output dock
 * (Master, Rec Tap, Audio Output) never anchor. A node anchored on a macro member joins that member's macro, and
 * the macro makes room for it (neighbours glide clear). Opens no undo record of its own.
 */
void placeNewModulesBesideConnections(GraphEditor& editor,
                                      const std::vector<juce::AudioProcessorGraph::NodeID>& created);

} // namespace synth
