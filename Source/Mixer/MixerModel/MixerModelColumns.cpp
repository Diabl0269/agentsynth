// Concern: FRO11 (P9-5) -- buildMixerSnapshot's column enumeration: strips in track order (a
// channel fed by two tracks appears once, at its first track's position), then any strip no track
// reaches (appended by ascending NodeID, mirroring StemExporter's own fallback ordering), then
// Direct once Master exists, then Master.
#include "MixerModel.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Mixer/TrackChannelLink.h"
#include "MixerModelInternal.h"
#include "Modules/ChannelStripModule.h"
#include <algorithm>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

// This track's bound source node, resolved to a live node id -- the same uuid lookup
// MainComponent::findNodeByUuid does, duplicated here because Core has no MainComponent to call
// into (Source/Mixer/CLAUDE.md: this layer never depends on AppUI).
NodeID resolveTrackSourceNode(juce::AudioProcessorGraph& graph, const Track& track) {
    if (track.bindingUuid.isEmpty())
        return {};
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == track.bindingUuid)
            return node->nodeID;
    return {};
}

struct StripEntry {
    NodeID stripId;
    std::vector<TrackId> feedingTracks; // in first-seen order
};
} // namespace

MixerSnapshot buildMixerSnapshot(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros) {
    MixerSnapshot snapshot;

    // Track-driven strips, in track order, de-duplicated by strip id (a channel fed by two tracks
    // appears once, at the position of the FIRST track that feeds it). One findStripFedByTrackSource
    // call per track (cheap: a bounded forward walk) rather than the fuller resolveTrackChannelLink
    // (a node scan plus two BFS walks) -- that heavier query is only for "is this ONE track linked",
    // not for building the whole column set.
    std::vector<StripEntry> stripEntries;
    for (const auto& track : doc.getTracks()) {
        const auto sourceId = resolveTrackSourceNode(graph, track);
        if (sourceId == NodeID{})
            continue;
        const auto stripId = findStripFedByTrackSource(graph, sourceId);
        if (stripId == NodeID{})
            continue;
        auto it = std::find_if(stripEntries.begin(), stripEntries.end(),
                               [&](const StripEntry& e) { return e.stripId == stripId; });
        if (it == stripEntries.end())
            stripEntries.push_back({stripId, {track.id}});
        else
            it->feedingTracks.push_back(track.id);
    }

    // Orphan strips: a ChannelStripModule no track's walk reached (a hand-built patch, or a bus
    // channel with no track of its own) -- appended by ascending NodeID.
    std::vector<NodeID> orphanStrips;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) == nullptr)
            continue;
        const auto id = node->nodeID;
        const bool alreadyListed =
            std::any_of(stripEntries.begin(), stripEntries.end(), [&](const StripEntry& e) { return e.stripId == id; });
        if (!alreadyListed)
            orphanStrips.push_back(id);
    }
    std::sort(orphanStrips.begin(), orphanStrips.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    for (auto id : orphanStrips)
        stripEntries.push_back({id, {}});

    for (const auto& entry : stripEntries) {
        auto* node = graph.getNodeForId(entry.stripId);
        if (node == nullptr)
            continue;

        MixerColumn column;
        column.kind = MixerColumn::Kind::Strip;
        column.nodeId = entry.stripId;
        column.uuid = node->properties["uuid"].toString();
        column.feedingTracks = entry.feedingTracks;

        if (const auto* macro = macros.findByMember(column.uuid)) {
            column.name = macro->name;
            column.colour = macro->colour;
        } else {
            column.name = channelDisplayName(graph, entry.stripId, doc, "Channel");
        }

        const auto feeders = findTrackSourcesFeedingStrip(graph, entry.stripId);
        column.linkedToTrack = feeders.size() == 1;

        buildInsertsForColumn(graph, doc, macros, column);
        snapshot.columns.push_back(std::move(column));
    }

    auto* masterNode = findMasterNode(graph);
    snapshot.hasMaster = masterNode != nullptr;
    snapshot.hasDirect = snapshot.hasMaster; // Direct is Master's own input bus -- see MixerModel.h

    if (snapshot.hasDirect) {
        MixerColumn direct;
        direct.kind = MixerColumn::Kind::Direct;
        direct.name = "Direct";
        snapshot.columns.push_back(std::move(direct));
    }
    if (masterNode != nullptr) {
        MixerColumn master;
        master.kind = MixerColumn::Kind::Master;
        master.nodeId = masterNode->nodeID;
        master.uuid = masterNode->properties["uuid"].toString();
        master.name = "Master";
        snapshot.columns.push_back(std::move(master));
    }

    return snapshot;
}

} // namespace synth
