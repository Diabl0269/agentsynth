#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace synth {

/** Finds a graph node by its "uuid" property without a whole-graph scan per lookup
 * (docs/architecture/graph-queries.md). Never returns a node that does not carry `uuid` now. Message thread only. */
class NodeUuidCache {
public:
    /** The first node (graph order) carrying `uuid`, or null; null for an empty uuid. */
    juce::AudioProcessorGraph::Node* find(juce::AudioProcessorGraph& graph, const juce::String& uuid);

private:
    void refill(juce::AudioProcessorGraph& graph);
    const juce::AudioProcessorGraph* filledFrom_ = nullptr;
    std::map<juce::String, juce::AudioProcessorGraph::NodeID> ids_;
};

} // namespace synth
