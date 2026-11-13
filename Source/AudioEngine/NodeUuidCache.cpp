// Concern: NodeUuidCache (NodeUuidCache.h) -- uuid lookups that stay O(log n) while the graph changes under them.
#include "AudioEngine/NodeUuidCache.h"

namespace synth {

namespace {
const juce::Identifier& uuidKey() {
    static const juce::Identifier key("uuid");
    return key;
}
} // namespace

// A remembered id is trusted only after checking that node still exists and still carries the uuid; anything else (a
// node added, removed or restored by an undo since the last fill, or a uuid never seen) costs one fresh scan, which is
// what every lookup used to cost. A per-track lookup on each timeline refresh made that refresh O(tracks x nodes).
juce::AudioProcessorGraph::Node* NodeUuidCache::find(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    if (uuid.isEmpty())
        return nullptr;
    const auto check = [&]() -> juce::AudioProcessorGraph::Node* {
        const auto it = ids_.find(uuid);
        auto* node = it != ids_.end() ? graph.getNodeForId(it->second) : nullptr;
        return node != nullptr && node->properties[uuidKey()].toString() == uuid ? node : nullptr;
    };
    if (filledFrom_ == &graph)
        if (auto* node = check())
            return node;
    refill(graph);
    return check();
}

// The first node per uuid, as a scan in graph order answers.
void NodeUuidCache::refill(juce::AudioProcessorGraph& graph) {
    ids_.clear();
    filledFrom_ = &graph;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (const auto uuid = node->properties[uuidKey()].toString(); uuid.isNotEmpty())
                ids_.emplace(uuid, node->nodeID);
}

} // namespace synth
