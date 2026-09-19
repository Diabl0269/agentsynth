// Concern: FRO15 (P9-9) -- the send slot flows (add / remove / retarget / resolve target / offer
// legal targets). See MixerSends.h for the contract; docs/mixer/sends-and-buses.md for the design.

#include "MixerSends.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>
#include <set>

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

bool isReachTerminal(const juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return true;
    if (dynamic_cast<const RecordTapModule*>(processor) != nullptr ||
        dynamic_cast<const MasterModule*>(processor) != nullptr)
        return true;
    return dynamic_cast<const juce::AudioProcessorGraph::AudioGraphIOProcessor*>(processor) != nullptr;
}

/** Forward from `start`, expanding THROUGH strips, to see whether `goal` is reachable -- the cycle
 *  guard behind enumerateSendTargets. Stops at the terminals; visited-set cycle guarded. */
bool reaches(juce::AudioProcessorGraph& graph, const std::vector<Connection>& connections, NodeID start, NodeID goal) {
    std::vector<NodeID> queue{start};
    std::set<NodeID> visited{start};

    while (!queue.empty()) {
        const auto nodeId = queue.back();
        queue.pop_back();

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            const auto destId = conn.destination.nodeID;
            if (destId == goal)
                return true;
            if (!visited.insert(destId).second)
                continue;
            if (isReachTerminal(processorAt(graph, destId)))
                continue;
            queue.push_back(destId);
        }
    }
    return false;
}

/** Removes every connection leaving `slot`'s two raw output channels, and RETURNS them, so a caller
 *  that fails half-way can put them back. Collect-then-remove: removing while iterating the list it
 *  came from invalidates it (spliceMasterNode's own reasoning). */
std::vector<Connection> dropSlotCables(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    const int left = ChannelStripModule::sendLeftChannel(slot);
    const int right = ChannelStripModule::sendRightChannel(slot);

    std::vector<Connection> doomed;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == sourceStrip &&
            (conn.source.channelIndex == left || conn.source.channelIndex == right))
            doomed.push_back(conn);
    for (const auto& conn : doomed)
        graph.removeConnection(conn);
    return doomed;
}

/** Wires `slot`'s stereo pair into `target`'s own ch0 / kRightBase. Rolls the left leg back if the
 *  right one is refused, so a half-wired send never exists. */
bool wireSlot(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, NodeID target) {
    const Connection leftEdge{{sourceStrip, ChannelStripModule::sendLeftChannel(slot)}, {target, 0}};
    const Connection rightEdge{{sourceStrip, ChannelStripModule::sendRightChannel(slot)},
                               {target, ChannelStripModule::kRightBase}};
    if (!graph.addConnection(leftEdge))
        return false;
    if (!graph.addConnection(rightEdge)) {
        graph.removeConnection(leftEdge);
        return false;
    }
    return true;
}

bool targetIsLegal(juce::AudioProcessorGraph& graph, NodeID sourceStrip, NodeID target) {
    if (target == sourceStrip || stripAt(graph, target) == nullptr || stripAt(graph, sourceStrip) == nullptr)
        return false;
    const auto connections = graph.getConnections();
    return !reaches(graph, connections, target, sourceStrip);
}
} // namespace

std::vector<NodeID> findStripsFeedingStrip(juce::AudioProcessorGraph& graph, NodeID stripId) {
    std::vector<NodeID> sources;
    if (stripAt(graph, stripId) == nullptr)
        return sources;

    const auto connections = graph.getConnections();
    std::vector<NodeID> queue{stripId};
    std::set<NodeID> visited{stripId};

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        for (const auto& conn : connections) {
            if (conn.destination.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            const auto sourceId = conn.source.nodeID;
            if (!visited.insert(sourceId).second)
                continue;
            if (stripAt(graph, sourceId) != nullptr) {
                sources.push_back(sourceId); // a strip feeding this one -- never expanded past
                continue;
            }
            queue.push_back(sourceId);
        }
    }
    std::sort(sources.begin(), sources.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    return sources;
}

bool isBusStrip(juce::AudioProcessorGraph& graph, NodeID stripId) {
    auto* strip = stripAt(graph, stripId);
    if (strip == nullptr)
        return false;
    return strip->isBus() || !findStripsFeedingStrip(graph, stripId).empty();
}

juce::String busFallbackName(juce::AudioProcessorGraph& graph, NodeID stripId) {
    std::vector<NodeID> buses;
    for (auto* node : graph.getNodes())
        if (node != nullptr && isBusStrip(graph, node->nodeID))
            buses.push_back(node->nodeID);
    std::sort(buses.begin(), buses.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });

    const auto found = std::find(buses.begin(), buses.end(), stripId);
    const int ordinal = found != buses.end() ? static_cast<int>(std::distance(buses.begin(), found)) + 1 : 1;
    return "Bus " + juce::String(ordinal);
}

NodeID findSendTarget(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return {};

    const auto connections = graph.getConnections();
    const int leg = ChannelStripModule::sendLeftChannel(slot);

    std::vector<NodeID> queue;
    std::set<NodeID> visited{sourceStrip};
    for (const auto& conn : connections) {
        if (conn.source.nodeID != sourceStrip || conn.source.channelIndex != leg)
            continue;
        if (!isSignalEdge(graph, connections, conn))
            continue;
        if (visited.insert(conn.destination.nodeID).second)
            queue.push_back(conn.destination.nodeID);
    }

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        auto* processor = processorAt(graph, nodeId);
        if (dynamic_cast<ChannelStripModule*>(processor) != nullptr)
            return nodeId; // the first strip the send reaches IS the bus it feeds
        if (isReachTerminal(processor))
            continue; // the send left the patch without ever reaching a bus

        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isSignalEdge(graph, connections, conn))
                continue;
            if (visited.insert(conn.destination.nodeID).second)
                queue.push_back(conn.destination.nodeID);
        }
    }
    return {};
}

std::vector<NodeID> enumerateSendTargets(juce::AudioProcessorGraph& graph, NodeID sourceStrip) {
    std::vector<NodeID> targets;
    if (stripAt(graph, sourceStrip) == nullptr)
        return targets;

    const auto connections = graph.getConnections();
    for (auto* node : graph.getNodes()) {
        if (node == nullptr || node->nodeID == sourceStrip)
            continue;
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) == nullptr)
            continue;
        if (reaches(graph, connections, node->nodeID, sourceStrip))
            continue; // would close a feedback loop -- never offered
        targets.push_back(node->nodeID);
    }
    std::sort(targets.begin(), targets.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    return targets;
}

int addSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, NodeID target) {
    if (!targetIsLegal(graph, sourceStrip, target))
        return -1;

    auto* strip = stripAt(graph, sourceStrip);
    const int slot = strip->addSend();
    if (slot < 0)
        return -1;

    if (!wireSlot(graph, sourceStrip, slot, target)) {
        strip->setSendActive(slot, false); // nothing changed -- see the header's "no-op" contract
        return -1;
    }
    return slot;
}

bool removeSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot))
        return false;

    dropSlotCables(graph, sourceStrip, slot);
    strip->setSendActive(slot, false);
    strip->setSendPreFader(slot, false);
    return true;
}

bool retargetSend(juce::AudioProcessorGraph& graph, NodeID sourceStrip, int slot, NodeID target) {
    auto* strip = stripAt(graph, sourceStrip);
    if (strip == nullptr || !strip->isSendActive(slot) || !targetIsLegal(graph, sourceStrip, target))
        return false;
    if (findSendTarget(graph, sourceStrip, slot) == target)
        return true; // already there

    // Put the old cables back if the new pair is refused, or a failed retarget would leave the slot
    // silently unwired -- the header promises the same "nothing changed" as addSend. DEFENSIVE, not
    // a path anything reaches today: juce::AudioProcessorGraph::canConnect checks node existence,
    // channel bounds and "not already connected" and nothing else -- notably it does NOT refuse a
    // cycle (measured: wiring one straight back through an Attenuverter is accepted), and
    // targetIsLegal has already ruled out every case left. Same shape, one level up, as wireSlot's
    // own left-leg rollback; it is what keeps the contract true if a future guard or channel-map
    // change ever makes a refusal reachable.
    const auto previous = dropSlotCables(graph, sourceStrip, slot);
    if (wireSlot(graph, sourceStrip, slot, target))
        return true;
    for (const auto& conn : previous)
        graph.addConnection(conn);
    return false;
}

} // namespace synth
