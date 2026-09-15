// Concern: FRO11 (P9-5, docs/mixer.md §5.6) -- a column's insert list: the chain between its
// feeding track's source and the strip, in signal order, plus linear-vs-branching classification,
// plus the three insert-list mutation primitives (splice out / splice in / reorder).
#include "MixerModel.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "MixerModelInternal.h"
#include "Modules/ModuleBase.h"
#include <algorithm>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

juce::AudioProcessor* processorFor(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

// Distinct NODES, never a raw connection count -- a stereo pair is TWO connections (ch0 and the
// right leg) to the very same next node, which must count as one successor/predecessor, not two,
// or every ordinary stereo chain would misreport as branching.
int signalSuccessorCount(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID node) {
    std::vector<NodeID> destinations;
    for (const auto& c : connections)
        if (c.source.nodeID == node && isSignalEdge(graph, connections, c))
            if (std::find(destinations.begin(), destinations.end(), c.destination.nodeID) == destinations.end())
                destinations.push_back(c.destination.nodeID);
    return (int)destinations.size();
}

int signalPredecessorCount(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID node) {
    std::vector<NodeID> sources;
    for (const auto& c : connections)
        if (c.destination.nodeID == node && isSignalEdge(graph, connections, c))
            if (std::find(sources.begin(), sources.end(), c.source.nodeID) == sources.end())
                sources.push_back(c.source.nodeID);
    return (int)sources.size();
}

// This track's own bound source node, resolved to a live node id -- same lookup
// MixerModelColumns.cpp's resolveTrackSourceNode does; kept file-local here rather than shared,
// since the two call sites want it for different tracks and neither is on a hot path.
NodeID resolveTrackSourceNode(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, TrackId trackId) {
    const auto* track = doc.getTrack(trackId);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return {};
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == track->bindingUuid)
            return node->nodeID;
    return {};
}
} // namespace

void buildInsertsForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                           MixerColumn& column) {
    if (column.feedingTracks.empty())
        return; // an orphan strip has no track-anchored chain to walk

    const auto sourceId = resolveTrackSourceNode(graph, doc, column.feedingTracks.front());
    if (sourceId == NodeID{})
        return;
    column.sourceNodeId = sourceId;

    const auto connections = graph.getConnections();

    // Forward walk from the source along signal edges, collecting the chain up to (not including)
    // the strip. A node with more than one signal predecessor or successor within the reach is a
    // merge/fan-out -- the whole chain is then read-only ("branching"), per §5.6.
    bool branching = false;
    std::vector<NodeID> chain;
    std::vector<NodeID> visited{sourceId};
    NodeID current = sourceId;

    while (current != column.nodeId) {
        if (signalSuccessorCount(graph, connections, current) > 1)
            branching = true;

        NodeID next;
        bool haveNext = false;
        for (const auto& c : connections) {
            if (c.source.nodeID == current && isSignalEdge(graph, connections, c)) {
                next = c.destination.nodeID;
                haveNext = true;
                break;
            }
        }
        if (!haveNext)
            return; // this track's reach never arrives at the strip -- leave the list empty

        if (next != column.nodeId) {
            if (signalPredecessorCount(graph, connections, next) > 1)
                branching = true;
            if (std::find(visited.begin(), visited.end(), next) != visited.end())
                break; // cycle guard -- should not happen in a real patch, never hang if it does
            chain.push_back(next);
            visited.push_back(next);
        }
        current = next;
        if (chain.size() > 64)
            break; // sane upper bound
    }

    column.insertChainIsLinear = !branching;
    for (auto nodeId : chain) {
        auto* processor = processorFor(graph, nodeId);
        auto* module = dynamic_cast<ModuleBase*>(processor);
        MixerInsertEntry entry;
        entry.nodeId = nodeId;
        entry.uuid = module != nullptr ? juce::String(module->getNodeUuid()) : juce::String();
        entry.name = processor != nullptr ? processor->getName() : juce::String("Module");
        entry.bypassed = module != nullptr && module->isBypassed();
        column.inserts.push_back(std::move(entry));
    }

    if (!branching || chain.empty())
        return;

    // "Edit on canvas" points at the owning macro of the first boxed node in the chain, else the
    // chain's first node itself.
    for (auto nodeId : chain) {
        auto* module = dynamic_cast<ModuleBase*>(processorFor(graph, nodeId));
        if (module == nullptr)
            continue;
        if (const auto* macro = macros.findByMember(module->getNodeUuid())) {
            column.editOnCanvasTargetUuid = macro->id;
            return;
        }
    }
    if (auto* module = dynamic_cast<ModuleBase*>(processorFor(graph, chain.front())))
        column.editOnCanvasTargetUuid = module->getNodeUuid();
}

bool spliceOutInsert(juce::AudioProcessorGraph& graph, NodeID node) {
    const auto connections = graph.getConnections();
    std::vector<Connection> preds, succs;
    for (const auto& c : connections) {
        if (c.destination.nodeID == node && isSignalEdge(graph, connections, c))
            preds.push_back(c);
        if (c.source.nodeID == node && isSignalEdge(graph, connections, c))
            succs.push_back(c);
    }
    if (preds.empty() || succs.empty())
        return false;

    // Bridge by matching `node`'s own input channel index to its own output channel index -- true
    // for every ordinary passthrough FX module (see MixerModel.h's own contract note).
    for (const auto& p : preds)
        for (const auto& s : succs)
            if (p.destination.channelIndex == s.source.channelIndex)
                graph.addConnection({p.source, s.destination});

    for (const auto& c : preds)
        graph.removeConnection(c);
    for (const auto& c : succs)
        graph.removeConnection(c);
    return true;
}

bool spliceInInsert(juce::AudioProcessorGraph& graph, NodeID predecessor, NodeID successor, NodeID node) {
    const auto connections = graph.getConnections();
    std::vector<Connection> between;
    for (const auto& c : connections)
        if (c.source.nodeID == predecessor && c.destination.nodeID == successor && isSignalEdge(graph, connections, c))
            between.push_back(c);
    if (between.empty())
        return false;

    // `node`'s own L/R channel numbering need not match `successor`'s (an ordinary FX module's
    // right leg vs. e.g. ChannelStripModule's kRightBase) -- left is always ch0
    // (Source/Modules/CLAUDE.md), right is whichever channel `node` itself reports as its own
    // right leg, read off the connection being replaced only to decide WHICH leg (left vs. right)
    // this connection carries, never to pick node's channel number directly.
    auto* nodeModule = dynamic_cast<ModuleBase*>(processorFor(graph, node));
    const int nodeRight = nodeModule != nullptr ? nodeModule->rightAudioLegChannel() : -1;

    for (const auto& c : between) {
        const bool isRightLeg = c.destination.channelIndex != 0;
        const int nodeChannel = isRightLeg ? (nodeRight >= 0 ? nodeRight : c.destination.channelIndex) : 0;
        graph.removeConnection(c);
        graph.addConnection({c.source, {node, nodeChannel}});
        graph.addConnection({{node, nodeChannel}, c.destination});
    }
    return true;
}

bool reorderInsert(juce::AudioProcessorGraph& graph, NodeID node, NodeID newPredecessor, NodeID newSuccessor) {
    if (!spliceOutInsert(graph, node))
        return false;
    return spliceInInsert(graph, newPredecessor, newSuccessor, node);
}

} // namespace synth
