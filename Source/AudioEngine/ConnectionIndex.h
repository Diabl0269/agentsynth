#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include <vector>

namespace synth {

/** Every cable of a graph, taken once and indexed by the node it enters and the node it leaves, each list in
 *  getConnections() order. getConnections() copies and sorts the whole cable set on every call, so a pass that asks
 *  about many nodes builds one index and asks it instead (docs/architecture/graph-queries.md). A snapshot: a cable
 *  added or removed after construction is not seen. Message thread only. */
class ConnectionIndex {
public:
    using Connection = juce::AudioProcessorGraph::Connection;
    using NodeID = juce::AudioProcessorGraph::NodeID;
    ConnectionIndex() = default; // no cables
    explicit ConnectionIndex(const juce::AudioProcessorGraph& graph);
    /** Every cable, in getConnections() order. */
    const std::vector<Connection>& all() const noexcept;
    const std::vector<Connection>& into(NodeID node) const;
    const std::vector<Connection>& outOf(NodeID node) const;
    /** Every cable into or out of `node` (a self-cable once), in getConnections() order. */
    const std::vector<Connection>& touching(NodeID node) const;

private:
    std::vector<Connection> all_;
    std::unordered_map<juce::uint32, std::vector<Connection>> in_, out_, touching_;
};

} // namespace synth
