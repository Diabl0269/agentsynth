#pragma once

// A built-in module's per-instance card layout: the node property "cardLayout" (kCardLayoutNodeProperty),
// stored like the custom card title, not in the module's extra state. Free functions so no
// GraphEditor member is needed; docs/layout/module-card-layout.md#where-a-layout-comes-from.

#include "Modules/CardLayout.h"
#include <juce_audio_processors/juce_audio_processors.h>

class AppUndoManager;

namespace synth {

/** The node's layout JSON, void when it has none or no such node exists. Message thread only. */
juce::var getCardLayoutOverride(const juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId);

/** Puts `json` (void = none) back as the node's layout property exactly as it was read, no undo and no
 *  re-serialising; for an editing session handing back what it opened with. False when the node is missing. */
bool restoreCardLayoutOverride(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                               const juce::var& json);

/**
 * Sets the node's layout, or clears it for nullopt. One undo step through `undo` (null applies
 * without recording); a write that changes nothing records nothing. False when the node is missing.
 */
bool setCardLayoutOverride(juce::AudioProcessorGraph& graph, ::AppUndoManager* undo,
                           juce::AudioProcessorGraph::NodeID nodeId, const std::optional<CardLayout>& layout);

} // namespace synth
