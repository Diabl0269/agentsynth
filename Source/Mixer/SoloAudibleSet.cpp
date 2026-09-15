// Concern: FRO15 (P9-9) -- computeSoloAudibleLegs, the per-leg mixer solo rule. See
// SoloAudibleSet.h for the rule itself and why it is per leg rather than per strip.
//
// Every walk here goes through synth::isSignalEdge (ChannelFlows.h), the ONE shared signal-edge
// rule -- never a third private traversal that could drift from the insert list's and the
// track/channel link's.

#include "SoloAudibleSet.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>
#include <set>
#include <vector>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

juce::AudioProcessor* processorAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    return dynamic_cast<ChannelStripModule*>(processorAt(graph, id));
}

// The three nodes a forward walk must never expand PAST -- the same terminals
// findUnchanneledOutputFeeds and findStripFedByTrackSource stop at. None of them is a strip, so
// reaching one can only ever end a branch, never answer it.
bool isReachTerminal(const juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return true;
    if (dynamic_cast<const RecordTapModule*>(processor) != nullptr ||
        dynamic_cast<const MasterModule*>(processor) != nullptr)
        return true;
    return dynamic_cast<const juce::AudioProcessorGraph::AudioGraphIOProcessor*>(processor) != nullptr;
}

/** Step 2 of the rule: grows `audible` with every strip reachable downstream of one already in it,
 *  expanding THROUGH strips (a bus feeding another bus stays audible too). Visited-set cycle
 *  guarded. */
void collectDownstreamStrips(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                             std::set<NodeID>& audible) {
    std::vector<NodeID> queue(audible.begin(), audible.end());
    std::set<NodeID> visited(audible.begin(), audible.end());

    while (!queue.empty()) {
        const auto nodeId = queue.back();
        queue.pop_back();

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            const auto destId = conn.destination.nodeID;
            if (!visited.insert(destId).second)
                continue;

            auto* destProcessor = processorAt(graph, destId);
            if (dynamic_cast<ChannelStripModule*>(destProcessor) != nullptr)
                audible.insert(destId); // a bus fed by a soloed strip stays audible, and so do ITS buses
            else if (isReachTerminal(destProcessor))
                continue;

            queue.push_back(destId);
        }
    }
}

/** Step 4 of the rule: does a forward walk from `strip`'s raw output channel `rawChannel` land on a
 *  strip in `audible`? Stops at the first strip it meets (never expanding past it) and at the
 *  terminals. Visited-set cycle guarded. */
bool legReachesAudibleStrip(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID strip,
                            int rawChannel, const std::set<NodeID>& audible) {
    std::vector<NodeID> queue;
    std::set<NodeID> visited{strip};

    // Seed from this LEG only -- that is the whole point of a per-leg gate.
    for (const auto& conn : connections) {
        if (conn.source.nodeID != strip || conn.source.channelIndex != rawChannel)
            continue;
        if (!isSignalEdge(graph, connections, conn))
            continue;
        const auto destId = conn.destination.nodeID;
        if (!visited.insert(destId).second)
            continue;
        if (dynamic_cast<ChannelStripModule*>(processorAt(graph, destId)) != nullptr) {
            if (audible.count(destId) != 0)
                return true;
            continue; // never expand past another strip
        }
        if (isReachTerminal(processorAt(graph, destId)))
            continue;
        queue.push_back(destId);
    }

    while (!queue.empty()) {
        const auto nodeId = queue.back();
        queue.pop_back();

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            const auto destId = conn.destination.nodeID;
            if (!visited.insert(destId).second)
                continue;
            if (dynamic_cast<ChannelStripModule*>(processorAt(graph, destId)) != nullptr) {
                if (audible.count(destId) != 0)
                    return true;
                continue;
            }
            if (isReachTerminal(processorAt(graph, destId)))
                continue;
            queue.push_back(destId);
        }
    }
    return false;
}

juce::uint32 maskForGatedStrip(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                               NodeID stripId, ChannelStripModule& strip, const std::set<NodeID>& audible) {
    juce::uint32 mask = 0;
    if (legReachesAudibleStrip(graph, connections, stripId, 0, audible) ||
        legReachesAudibleStrip(graph, connections, stripId, ChannelStripModule::kRightBase, audible))
        mask |= ChannelStripModule::kMainLegBit;

    for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot) {
        if (!strip.isSendActive(slot))
            continue;
        if (legReachesAudibleStrip(graph, connections, stripId, ChannelStripModule::sendLeftChannel(slot), audible) ||
            legReachesAudibleStrip(graph, connections, stripId, ChannelStripModule::sendRightChannel(slot), audible))
            mask |= ChannelStripModule::sendLegBit(slot);
    }
    return mask;
}

/** Step 5 of the rule: grows `contributing` (seeded with D) to a FIXED POINT — a strip whose leg
 *  reaches a strip that itself contributes is contributing too. Without this, a walk that stops at
 *  the first strip answers "does this leg reach D in ONE hop?", which silences every source behind a
 *  chain of buses: source -> inner bus -> soloed outer bus would leave the source closed and feed
 *  the soloed bus silence. Monotone (a leg can only ever open), so the loop runs at most once per
 *  strip and the legs a pass opens stay open in the next. */
void closeOverContributingStrips(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections,
                                 const std::vector<NodeID>& strips, std::set<NodeID>& contributing) {
    for (bool grew = true; grew;) {
        grew = false;
        for (auto id : strips) {
            if (contributing.count(id) != 0)
                continue;
            auto* strip = stripAt(graph, id);
            if (strip == nullptr)
                continue;
            if (maskForGatedStrip(graph, connections, id, *strip, contributing) != 0) {
                contributing.insert(id);
                grew = true;
            }
        }
    }
}

} // namespace

std::map<NodeID, juce::uint32> computeSoloAudibleLegs(juce::AudioProcessorGraph& graph) {
    std::map<NodeID, juce::uint32> masks;

    std::vector<NodeID> strips;
    std::set<NodeID> audible; // = D, seeded with S
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor());
        if (strip == nullptr)
            continue;
        strips.push_back(node->nodeID);
        if (strip->isSoloed())
            audible.insert(node->nodeID);
    }

    if (audible.empty()) {
        for (auto id : strips)
            masks[id] = ~0u; // nothing soloed: the gate is never consulted anyway
        return masks;
    }

    const auto connections = graph.getConnections();
    collectDownstreamStrips(graph, connections, audible);

    // D is "fully audible"; `contributing` additionally holds every strip that merely FEEDS the
    // soloed path, however many buses away. Each of those still gets a per-leg mask below (only the
    // legs that actually reach the path open) — being a feeder is not the same as being audible.
    std::set<NodeID> contributing = audible;
    closeOverContributingStrips(graph, connections, strips, contributing);

    for (auto id : strips) {
        if (audible.count(id) != 0) {
            masks[id] = ~0u;
            continue;
        }
        auto* strip = stripAt(graph, id);
        masks[id] = strip != nullptr ? maskForGatedStrip(graph, connections, id, *strip, contributing) : 0u;
    }
    return masks;
}

} // namespace synth
