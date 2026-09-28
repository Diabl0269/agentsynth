// AutomationPlacement.cpp
//
// Concern: the track-ownership walk and the one lane-placement seam
// (docs/timeline/track-automation.md#which-track-owns-a-lane).

#include "AutomationPlacement.h"

#include "Mixer/ChannelFlows/ChannelFlowsInternal.h"
#include <set>
#include <vector>

namespace synth {

namespace {

juce::AudioProcessorGraph::Node* findNodeByUuid(const juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    if (uuid.isEmpty())
        return nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

// Every node `sourceId`'s signal reaches, the source itself included. Uses the SAME edge rule the
// track <-> channel link walk uses (isLinkSignalEdge: MIDI and audio signal edges, never an
// attenuverter's hidden modulation leg, never an audio edge landing on a ModCV or sidechain input),
// so a plain CV cable from track A's LFO into track B's filter does not make A "reach" B's chain.
// Terminals (Master, Record Tap, the graph's audio/MIDI IO) end a branch and are never collected:
// every track reaches Master, and a one-track project must not make that track own the master bus.
std::set<juce::uint32> reachFrom(const juce::AudioProcessorGraph& graph,
                                 const std::vector<juce::AudioProcessorGraph::Connection>& connections,
                                 juce::AudioProcessorGraph::NodeID sourceId) {
    std::set<juce::uint32> visited{sourceId.uid};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{sourceId};
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto nodeId = queue[head];
        for (const auto& conn : connections) {
            if (conn.source.nodeID != nodeId || !isLinkSignalEdge(graph, conn))
                continue;
            const auto destId = conn.destination.nodeID;
            if (visited.count(destId.uid) != 0)
                continue;
            if (isReachTerminal(processorFor(graph, destId)))
                continue;
            visited.insert(destId.uid);
            queue.push_back(destId);
        }
    }
    return visited;
}

} // namespace

// Sources are Midi/Audio tracks whose binding resolves to a live node (a Track In or a Track Audio
// node -- both kinds bind through Track::bindingUuid, and isTrackSourceNode covers both). Automation
// tracks, unbound tracks and orphaned tracks contribute nothing: they have no source to walk from.
// A node reached by two or more tracks is shared (a bus, a sampler several MIDI tracks play) and is
// dropped from the map -- its automation is global.
TrackOwnershipMap computeTrackOwnership(const juce::AudioProcessorGraph& graph, const TimelineDoc& doc) {
    std::map<juce::uint32, int> reachCount;
    TrackOwnershipMap owners;
    const auto connections = graph.getConnections();

    for (const auto& track : doc.getTracks()) {
        if (track.kind == TrackKind::Automation || track.bindingUuid.isEmpty() || track.orphaned)
            continue;
        auto* source = findNodeByUuid(graph, track.bindingUuid);
        if (source == nullptr || !isTrackSourceNode(source->getProcessor()))
            continue;
        for (const auto uid : reachFrom(graph, connections, source->nodeID)) {
            if (++reachCount[uid] == 1)
                owners[uid] = track.id;
            else
                owners.erase(uid);
        }
    }
    return owners;
}

std::optional<TrackId> resolveOwningTrack(const juce::AudioProcessorGraph& graph, const TimelineDoc& doc,
                                          const juce::String& nodeUuid) {
    auto* node = findNodeByUuid(graph, nodeUuid);
    if (node == nullptr)
        return std::nullopt;
    const auto owners = computeTrackOwnership(graph, doc);
    const auto it = owners.find(node->nodeID.uid);
    if (it == owners.end())
        return std::nullopt;
    return it->second;
}

TrackId findAutomationTrack(const TimelineDoc& doc) {
    for (const auto& track : doc.getTracks())
        if (track.kind == TrackKind::Automation)
            return track.id;
    return {};
}

// THE placement seam: MainComponent::automateParameter, MainComponent::addPluginAutomationLane and
// TimelineOps' writeLane all call this inside their own mutation, so a knob right-click, a lane
// picker "Add" entry and an AI-written lane can never disagree about where a lane lands. Placement
// happens ONCE, at creation: TimelineDoc::addLane dedupes doc-wide, so a repeat request for an
// already-automated parameter returns the existing lane wherever it lives, and a lane is never
// migrated when later re-patching changes which track owns its module.
TrackId findOrCreateLaneHostTrack(const juce::AudioProcessorGraph& graph, TimelineDoc& doc,
                                  const juce::String& nodeUuid) {
    if (const auto owner = resolveOwningTrack(graph, doc, nodeUuid))
        return *owner;
    if (const auto existing = findAutomationTrack(doc); existing.isValid())
        return existing;
    return doc.addTrack(TrackKind::Automation, "Automation");
}

} // namespace synth
