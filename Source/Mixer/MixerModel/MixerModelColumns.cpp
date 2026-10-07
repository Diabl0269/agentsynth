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

struct StripEntry {
    NodeID stripId;
    std::vector<TrackId> feedingTracks; // in first-seen order
};
} // namespace

MixerSnapshot buildMixerSnapshot(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros) {
    MixerSnapshot snapshot;

    // Track-driven strips, in track order, de-duplicated by strip id (a channel fed by two tracks
    // appears once, at the position of the FIRST track that feeds it). Every column below asks this one view
    // (MixerModelGraphView.cpp): a per-column walk of the whole graph made a snapshot, and so every edit, O(columns x
    // cables log cables).
    const MixerGraphView view(graph, macros);
    std::vector<StripEntry> stripEntries;
    std::map<juce::uint32, size_t> entryByStrip;
    // The first track (in track order) each source node belongs to, for a linked column's colour below: asking every
    // track per column made a snapshot grow with tracks x columns.
    std::map<juce::uint32, juce::uint32> firstTrackColourBySource;
    for (const auto& track : doc.getTracks()) {
        const auto sourceId = view.trackSource(track);
        if (sourceId == NodeID{})
            continue;
        firstTrackColourBySource.emplace(sourceId.uid, track.colourArgb);
        const auto stripId = view.stripFedBy(sourceId);
        if (stripId == NodeID{})
            continue;
        if (const auto [it, added] = entryByStrip.emplace(stripId.uid, stripEntries.size()); added)
            stripEntries.push_back({stripId, {track.id}});
        else
            stripEntries[it->second].feedingTracks.push_back(track.id);
    }

    // Orphan strips: a ChannelStripModule no track's walk reached (a hand-built patch, or a bus
    // channel with no track of its own) -- appended by ascending NodeID.
    std::vector<NodeID> orphanStrips;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) == nullptr)
            continue;
        if (entryByStrip.count(node->nodeID.uid) == 0)
            orphanStrips.push_back(node->nodeID);
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
        column.kind = view.isBus(entry.stripId) ? MixerColumn::Kind::Bus : MixerColumn::Kind::Strip;
        column.nodeId = entry.stripId;
        column.uuid = node->properties["uuid"].toString();
        column.feedingTracks = entry.feedingTracks;

        if (const auto* macro = view.channelMacros.nearest(column.uuid))
            column.colour = macro->colour;
        column.name = stripColumnName(view, doc, entry.stripId);

        const auto& feeders = view.trackSourcesFeeding(entry.stripId);
        column.linkedToTrack = column.kind == MixerColumn::Kind::Strip && feeders.size() == 1;

        // A channel exactly ONE track feeds shows THAT track's colour (what the timeline shows), overriding the macro
        // colour; a bus, a shared channel or an orphan keeps the macro colour. Read from `feeders` itself, not from
        // linkedToTrack, so this stays independent of how the column kind is derived.
        if (feeders.size() == 1 && !view.isBus(entry.stripId))
            if (const auto first = firstTrackColourBySource.find(feeders[0].uid);
                first != firstTrackColourBySource.end())
                column.colour = juce::Colour(first->second);

        buildInsertsForColumn(view, doc, column);
        buildBusSourcesForColumn(view, doc, column);
        buildSendsForColumn(view, doc, column);
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
        buildInsertsForColumn(view, doc, master);
        snapshot.columns.push_back(std::move(master));
    }

    return snapshot;
}

} // namespace synth
