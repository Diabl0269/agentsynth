// ChannelFlowsTrackChannelLink.cpp
//
// The two signal-reach queries the track <-> channel link rule (docs/mixer.md §5.2) is decided
// from: forward from a track's own source node to the Channel Strip it plays into, and backward
// from a Channel Strip to every track source that feeds it. Both are pure graph reads -- no
// mutation, no undo, no TimelineDoc -- so they live in Core next to the rest of ChannelFlows.
//
// findTrackSourcesFeedingStrip() is StemSession.cpp's former private upstreamTrackSources() (FRO55,
// stem file naming) promoted verbatim to a shared Core query: "which track feeds this strip" is the
// same question the stem namer and the link rule both ask, so there is one BFS for both rather than
// two copies that could drift.

#include "ChannelFlows.h"

#include "Modules/AttenuverterModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>

namespace synth {

namespace {

// True when `conn` carries signal (audio or MIDI) rather than a hidden modulation leg -- the same
// two exclusions ChannelFlows' own isSignalEdge rule uses (that function is private to the "Make
// channel" implementation, so this mirrors just the parts a plain reach walk needs rather than
// exposing it): never cross an AttenuverterModule on either end (AudioEngine::addModRouting always
// wraps a hidden mod leg in one of these), and never treat an audio edge landing on a PortRole::
// ModCV input as part of the signal path (a plain CV cable into some other track's cutoff must not
// make that track "feed" this strip). Unlike ChannelFlows' full rule, this does not resolve through
// macro ports first (resolveThroughPorts) - a CV cable that enters a strip's macro through an
// auto-ported Mono jack is a rare enough patch shape that treating it as signal here is an
// acceptable simplification for a display/naming decision, not a routing one.
//
// Moved here UNCHANGED from StemSession.cpp's isStemNamingSignalEdge (FRO55) - see the file comment.
bool isLinkSignalEdge(juce::AudioProcessorGraph& graph, const juce::AudioProcessorGraph::Connection& conn) {
    auto* srcNode = graph.getNodeForId(conn.source.nodeID);
    auto* dstNode = graph.getNodeForId(conn.destination.nodeID);
    auto* srcProcessor = srcNode != nullptr ? srcNode->getProcessor() : nullptr;
    auto* dstProcessor = dstNode != nullptr ? dstNode->getProcessor() : nullptr;
    if (srcProcessor == nullptr || dstProcessor == nullptr)
        return false;
    if (dynamic_cast<AttenuverterModule*>(srcProcessor) != nullptr ||
        dynamic_cast<AttenuverterModule*>(dstProcessor) != nullptr)
        return false;
    if (conn.source.isMIDI())
        return true;
    if (auto* module = dynamic_cast<ModuleBase*>(dstProcessor))
        return module->mapInputChannel(conn.destination.channelIndex).role != PortRole::ModCV;
    return true;
}

// The three nodes a forward walk must never expand PAST - the same terminals
// findUnchanneledOutputFeeds stops at. Reaching one ends that branch; it is never a strip, so it
// can only ever end the search, never answer it.
bool isReachTerminal(const juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return true;
    if (dynamic_cast<const RecordTapModule*>(processor) != nullptr ||
        dynamic_cast<const MasterModule*>(processor) != nullptr)
        return true;
    return dynamic_cast<const juce::AudioProcessorGraph::AudioGraphIOProcessor*>(processor) != nullptr;
}

bool contains(const std::vector<juce::AudioProcessorGraph::NodeID>& ids, juce::AudioProcessorGraph::NodeID id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

juce::AudioProcessorGraph::NodeID findStripFedByTrackSource(juce::AudioProcessorGraph& graph,
                                                            juce::AudioProcessorGraph::NodeID trackSourceId) {
    if (graph.getNodeForId(trackSourceId) == nullptr)
        return {};

    std::vector<juce::AudioProcessorGraph::NodeID> visited{trackSourceId};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{trackSourceId};
    const auto connections = graph.getConnections();

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isLinkSignalEdge(graph, conn))
                continue;
            const auto destId = conn.destination.nodeID;
            if (contains(visited, destId))
                continue;
            visited.push_back(destId);

            auto* destNode = graph.getNodeForId(destId);
            auto* destProcessor = destNode != nullptr ? destNode->getProcessor() : nullptr;
            if (dynamic_cast<ChannelStripModule*>(destProcessor) != nullptr)
                return destId; // the first strip this track's signal reaches - see the header comment
            if (isReachTerminal(destProcessor))
                continue; // the signal left the patch here without ever passing a strip

            queue.push_back(destId);
        }
    }
    return {};
}

std::vector<juce::AudioProcessorGraph::NodeID> findTrackSourcesFeedingStrip(juce::AudioProcessorGraph& graph,
                                                                            juce::AudioProcessorGraph::NodeID stripId) {
    std::vector<juce::AudioProcessorGraph::NodeID> tracks;
    if (graph.getNodeForId(stripId) == nullptr)
        return tracks;

    std::vector<juce::AudioProcessorGraph::NodeID> visited{stripId};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{stripId};
    const auto connections = graph.getConnections();

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        for (const auto& conn : connections) {
            if (conn.destination.nodeID != nodeId || !isLinkSignalEdge(graph, conn))
                continue;
            const auto sourceId = conn.source.nodeID;
            if (contains(visited, sourceId))
                continue;
            visited.push_back(sourceId);

            auto* sourceNode = graph.getNodeForId(sourceId);
            auto* sourceProcessor = sourceNode != nullptr ? sourceNode->getProcessor() : nullptr;
            if (isTrackSourceNode(sourceProcessor)) {
                if (!contains(tracks, sourceId))
                    tracks.push_back(sourceId);
                continue; // a track source has no inputs of its own - nothing to enqueue
            }
            if (dynamic_cast<ChannelStripModule*>(sourceProcessor) != nullptr)
                continue; // never expand past another strip - see the header comment

            queue.push_back(sourceId);
        }
    }
    return tracks;
}

} // namespace synth
