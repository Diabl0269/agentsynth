// MainComponentAutomationOwner.cpp — which timeline track an automation lane belongs on.
// A lane for a module's parameter goes on the track that plays that module (the pure rule is
// synth::resolveTrackOwners, docs/timeline/automation.md#which-track-a-lane-lands-on); a module no
// single track plays falls back to the shared Automation track. This unit builds the rule's input from
// the live graph and doc, and moves lanes saved before the rule existed.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Timeline/TrackOwnership.h"
#include <set>

std::map<juce::String, synth::TrackId> MainComponent::resolveAutomationOwners() const {
    const auto& graph = audioEngine.getGraph();
    synth::OwnershipGraph owned;
    // The same edges auto-arrange flattens the canvas into (GraphEditorAutoArrange.cpp): every cable is a flow
    // edge, every modulation routing a mod edge.
    std::map<juce::uint32, juce::String> keyOf;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        keyOf[node->nodeID.uid] = detail::ownershipKey(*node);
        owned.nodes.push_back(keyOf[node->nodeID.uid]);
    }
    for (const auto& conn : graph.getConnections()) {
        const auto from = keyOf.find(conn.source.nodeID.uid);
        const auto to = keyOf.find(conn.destination.nodeID.uid);
        if (from != keyOf.end() && to != keyOf.end())
            owned.flowEdges.push_back({from->second, to->second});
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        const auto from = keyOf.find(r.sourceNodeID.uid);
        const auto to = keyOf.find(r.destNodeID.uid);
        if (r.hasSource && r.hasDest && from != keyOf.end() && to != keyOf.end())
            owned.modEdges.push_back({from->second, to->second});
    }

    std::vector<std::pair<synth::TrackId, juce::String>> starts;
    for (const auto& track : timelineDoc.getTracks())
        if (track.bindingUuid.isNotEmpty())
            starts.push_back({track.id, track.bindingUuid});
    return synth::resolveTrackOwners(owned, starts);
}

// The track a new lane for `nodeUuid` lands on: its owning track, else the doc's Automation track
// (created on first use). Called inside the caller's mutation so a created track and its lane are one undo
// step. Invalid only at kMaxTracks.
synth::TrackId MainComponent::trackForNewLane(const juce::String& nodeUuid) {
    const auto owners = resolveAutomationOwners();
    if (const auto owner = owners.find(nodeUuid); owner != owners.end())
        return owner->second;
    for (const auto& track : timelineDoc.getTracks())
        if (track.kind == synth::TrackKind::Automation)
            return track.id;
    return timelineDoc.addTrack(synth::TrackKind::Automation, "Automation");
}

// Load-time half: lanes saved on the Automation track move to the track that plays their module, and an
// Automation track that loses its last lane (and holds no clips) goes. Direct doc mutations, not an undo
// step: it is part of opening the project, which no undo entry reverses. Lanes whose module no single track
// plays stay put. Idempotent.
void MainComponent::moveLanesToOwningTracks() {
    const auto owners = resolveAutomationOwners();
    if (owners.empty())
        return;

    std::vector<std::pair<synth::LaneId, synth::TrackId>> moves;
    std::set<synth::TrackId> sources;
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.kind != synth::TrackKind::Automation)
            continue;
        for (const auto& lane : track.lanes) {
            const auto owner = owners.find(lane.nodeUuid);
            if (owner == owners.end() || owner->second == track.id)
                continue;
            moves.push_back({lane.id, owner->second});
            sources.insert(track.id);
        }
    }
    for (const auto& [lane, dest] : moves)
        timelineDoc.moveLaneToTrack(lane, dest);

    for (const auto id : sources)
        if (const auto* track = timelineDoc.getTrack(id);
            track != nullptr && track->lanes.empty() && track->clips.empty())
            timelineDoc.removeTrack(id);
}
