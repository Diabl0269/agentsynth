// Concern: buildMixerSnapshot's column enumeration: strips in track order (a
// channel fed by two tracks appears once, at its first track's position), then any strip no track
// reaches (appended by ascending NodeID, mirroring StemExporter's own fallback ordering), then
// Direct once Master exists, then Master.
#include "MixerModel.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/MasterSplice.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Mixer/TrackChannelLink.h"
#include "MixerModelInternal.h"
#include "Modules/ChannelStripModule.h"
#include <algorithm>
#include <map>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

// Every node by uuid, built once per snapshot -- the same lookup MainComponent::findNodeByUuid does (the first node
// with the uuid), duplicated here because Core has no MainComponent to call into (Source/Mixer/CLAUDE.md: this layer
// never depends on AppUI). A scan per track, and again per column for its colour, made a snapshot O(tracks x nodes)
// with a string-pooled property key per step (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch).
class NodesByUuid {
public:
    explicit NodesByUuid(juce::AudioProcessorGraph& graph) {
        static const juce::Identifier uuidKey("uuid");
        for (auto* node : graph.getNodes())
            if (const auto uuid = node->properties[uuidKey].toString(); uuid.isNotEmpty())
                ids_.emplace(uuid, node->nodeID);
    }
    // This track's bound source node, resolved to a live node id.
    NodeID trackSource(const Track& track) const {
        const auto it = track.bindingUuid.isNotEmpty() ? ids_.find(track.bindingUuid) : ids_.end();
        return it != ids_.end() ? it->second : NodeID{};
    }

private:
    std::map<juce::String, NodeID> ids_;
};

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
    const NodesByUuid nodes(graph);
    std::vector<StripEntry> stripEntries;
    for (const auto& track : doc.getTracks()) {
        const auto sourceId = nodes.trackSource(track);
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
        // A bus IS a ChannelStrip -- the Kind only changes what the column PAINTS (a BUS badge and a
        // feeding-strips source line instead of a track chip and colour link)
        // (see docs/mixer/sends-and-buses.md#a-bus-is-a-channel-strip).
        column.kind = isBusStrip(graph, entry.stripId) ? MixerColumn::Kind::Bus : MixerColumn::Kind::Strip;
        column.nodeId = entry.stripId;
        column.uuid = node->properties["uuid"].toString();
        column.feedingTracks = entry.feedingTracks;

        if (const auto* macro = nearestChannelMacro(graph, macros, column.uuid))
            column.colour = macro->colour;
        column.name = stripColumnName(graph, doc, macros, entry.stripId);

        const auto feeders = findTrackSourcesFeedingStrip(graph, entry.stripId);
        column.linkedToTrack = column.kind == MixerColumn::Kind::Strip && feeders.size() == 1;

        // A channel exactly ONE track feeds shows THAT track's colour (what the timeline shows), overriding the macro
        // colour; a bus, a shared channel or an orphan keeps the macro colour. Read from `feeders` itself, not from
        // linkedToTrack, so this stays independent of how the column kind is derived.
        if (feeders.size() == 1 && !isBusStrip(graph, entry.stripId))
            for (const auto& track : doc.getTracks())
                if (nodes.trackSource(track) == feeders[0]) {
                    column.colour = juce::Colour(track.colourArgb);
                    break;
                }

        buildInsertsForColumn(graph, doc, macros, column);
        buildBusSourcesForColumn(graph, doc, macros, column);
        buildSendsForColumn(graph, doc, macros, column);
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
        buildInsertsForColumn(graph, doc, macros, master);
        snapshot.columns.push_back(std::move(master));
    }

    return snapshot;
}

} // namespace synth
