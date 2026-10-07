#pragma once

// MixerModelInternal.h -- shared private helper between MixerModelColumns.cpp and
// MixerModelInserts.cpp (root CLAUDE.md's "<Class>Internal.h" convention for a class split across
// several .cpp units). Not part of the public MixerModel.h surface.

#include "AudioEngine/ConnectionIndex.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/TrackChannelLink.h"
#include "MixerModel.h"
#include <map>
#include <optional>
#include <vector>

namespace synth {

/** One snapshot's read-only view of the graph, built once at the top of buildMixerSnapshot and handed to every
 *  per-column builder below (docs/architecture/graph-queries.md). The graph and `macros` must not change while it
 *  lives. */
struct MixerGraphView {
    MixerGraphView(juce::AudioProcessorGraph& graph, const MacroSet& macros);
    /** isBusStrip(graph, stripId). */
    bool isBus(juce::AudioProcessorGraph::NodeID stripId) const;
    /** busFallbackName(graph, stripId). */
    juce::String busName(juce::AudioProcessorGraph::NodeID stripId) const;
    /** findStripFedByTrackSource(graph, trackSourceId). */
    juce::AudioProcessorGraph::NodeID stripFedBy(juce::AudioProcessorGraph::NodeID trackSourceId) const;
    /** findTrackSourcesFeedingStrip(graph, stripId). */
    const std::vector<juce::AudioProcessorGraph::NodeID>&
    trackSourcesFeeding(juce::AudioProcessorGraph::NodeID strip) const;
    /** The node `track` is bound to (its bindingUuid), or an invalid id. */
    juce::AudioProcessorGraph::NodeID trackSource(const Track& track) const;

    juce::AudioProcessorGraph& graph;
    const MacroSet& macros;
    const ConnectionIndex cables;
    const TrackChannelLinkMap links;
    const ChannelMacroIndex channelMacros;

private:
    mutable std::map<juce::uint32, bool> busByStrip_;
    mutable std::optional<std::vector<juce::AudioProcessorGraph::NodeID>> buses_;
};

/** Fills `column.inserts`/`insertChainIsLinear`/`editOnCanvasTargetUuid` by walking forward from
 *  `column`'s first feeding track's source node to `column.nodeId` along signal edges (see
 *  MixerModelInserts.cpp). No-op (leaves the column's insert fields at their defaults) when
 *  `column.feedingTracks` is empty -- an orphan strip has no track-anchored chain to walk. For
 *  Kind::Master it instead walks forward from Master's own node to `column.chainEndNodeId` (the Rec Tap or Audio
 *  Output) and sets `column.sourceNodeId` to Master. */
void buildInsertsForColumn(const MixerGraphView& view, const TimelineDoc& doc, MixerColumn& column);

// ---- Bus/send column builders (docs/mixer/sends-and-buses.md), defined in MixerModelSends.cpp. The
// bus/send graph queries these build on (isBusStrip, busFallbackName, findSendTarget) are public Core
// surface in Mixer/MixerSends/MixerSends.h. ----------------------------------------------------------

/** What a strip column calls itself: its macro's name, else "Bus N" for a bus, else
 *  channelDisplayName's feeding-track name. Shared by the column build, a bus's source line and a
 *  send row's target label, so all three always agree. */
juce::String stripColumnName(const MixerGraphView& view, const TimelineDoc& doc,
                             juce::AudioProcessorGraph::NodeID stripId);

/** Fills the feeding strips' names: `column.busSources` for Kind::Bus, `column.receivesFrom` for
 *  Kind::Strip (a track channel receiving sends). No-op for Direct/Master. */
void buildBusSourcesForColumn(const MixerGraphView& view, const TimelineDoc& doc, MixerColumn& column);

/** Fills `column.sends` from the strip's active slots, resolving each slot's target off the graph. */
void buildSendsForColumn(const MixerGraphView& view, const TimelineDoc& doc, MixerColumn& column);

} // namespace synth
