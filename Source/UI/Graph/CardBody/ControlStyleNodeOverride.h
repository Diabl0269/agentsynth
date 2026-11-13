#pragma once

// A module's own knob and fader style: the node property "controlStyle" (kControlStyleNodeProperty), stored
// like the card layout and the custom title, not in the module's extra state.

#include "Modules/CardLayout.h"
#include "UI/Theme/ControlStyleOverride.h"
#include <juce_audio_processors/juce_audio_processors.h>

class AppUndoManager;

namespace synth {

/** The node's own style; empty when it has none or no such node exists. Message thread only. */
theme::ControlStyleOverride getControlStyleNodeOverride(const juce::AudioProcessorGraph& graph,
                                                        juce::AudioProcessorGraph::NodeID nodeId);

/**
 * Sets the node's style, or clears it for an empty override. One undo step through `undo` (null applies
 * without recording); a write that changes nothing records nothing. False when the node is missing.
 */
bool setControlStyleNodeOverride(juce::AudioProcessorGraph& graph, ::AppUndoManager* undo,
                                 juce::AudioProcessorGraph::NodeID nodeId, const theme::ControlStyleOverride& style);

} // namespace synth
