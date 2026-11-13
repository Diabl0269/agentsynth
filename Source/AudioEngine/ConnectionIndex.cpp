// Concern: the one-pass cable index every per-edit graph walk shares (ConnectionIndex.h).
#include "AudioEngine/ConnectionIndex.h"

namespace synth {

namespace {
const std::vector<ConnectionIndex::Connection>&
listFor(const std::unordered_map<juce::uint32, std::vector<ConnectionIndex::Connection>>& map,
        juce::AudioProcessorGraph::NodeID node) {
    static const std::vector<ConnectionIndex::Connection> none;
    const auto it = map.find(node.uid);
    return it != map.end() ? it->second : none;
}
} // namespace

// The one getConnections() call; every list below keeps its order, so a walk that used to scan the whole list and
// take the first match takes the same first match from the node's own list.
ConnectionIndex::ConnectionIndex(const juce::AudioProcessorGraph& graph)
    : all_(graph.getConnections()) {
    for (const auto& c : all_) {
        in_[c.destination.nodeID.uid].push_back(c);
        out_[c.source.nodeID.uid].push_back(c);
        touching_[c.source.nodeID.uid].push_back(c);
        if (c.destination.nodeID != c.source.nodeID)
            touching_[c.destination.nodeID.uid].push_back(c);
    }
}

const std::vector<ConnectionIndex::Connection>& ConnectionIndex::all() const noexcept { return all_; }

const std::vector<ConnectionIndex::Connection>& ConnectionIndex::into(NodeID node) const { return listFor(in_, node); }

const std::vector<ConnectionIndex::Connection>& ConnectionIndex::outOf(NodeID node) const {
    return listFor(out_, node);
}

const std::vector<ConnectionIndex::Connection>& ConnectionIndex::touching(NodeID node) const {
    return listFor(touching_, node);
}

} // namespace synth
