#pragma once

// MixerModelInternal.h -- shared private helper between MixerModelColumns.cpp and
// MixerModelInserts.cpp (root CLAUDE.md's "<Class>Internal.h" convention for a class split across
// several .cpp units). Not part of the public MixerModel.h surface.

#include "MixerModel.h"

namespace synth {

/** Fills `column.inserts`/`insertChainIsLinear`/`editOnCanvasTargetUuid` by walking forward from
 *  `column`'s first feeding track's source node to `column.nodeId` along signal edges (see
 *  MixerModelInserts.cpp). No-op (leaves the column's insert fields at their defaults) when
 *  `column.feedingTracks` is empty -- an orphan strip has no track-anchored chain to walk. FRO148: for
 *  Kind::Master it instead walks forward from Master's own node to `column.chainEndNodeId` (the Rec Tap or Audio
 *  Output) and sets `column.sourceNodeId` to Master. */
void buildInsertsForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                           MixerColumn& column);

// ---- FRO15 (P9-9, docs/mixer/sends-and-buses.md), defined in MixerModelSends.cpp. The bus/send graph
// queries these build on (isBusStrip, busFallbackName, findSendTarget) are public Core surface in
// Mixer/MixerSends/MixerSends.h. ----------------------------------------------------------------

/** What a strip column calls itself: its macro's name, else "Bus N" for a bus, else
 *  channelDisplayName's feeding-track name. Shared by the column build, a bus's source line and a
 *  send row's target label, so all three always agree. */
juce::String stripColumnName(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                             juce::AudioProcessorGraph::NodeID stripId);

/** Fills `column.busSources` with the feeding strips' names. No-op unless `column` is Kind::Bus. */
void buildBusSourcesForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                              MixerColumn& column);

/** Fills `column.sends` from the strip's active slots, resolving each slot's target off the graph. */
void buildSendsForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                         MixerColumn& column);

} // namespace synth
