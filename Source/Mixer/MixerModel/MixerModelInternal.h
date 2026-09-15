#pragma once

// MixerModelInternal.h -- shared private helper between MixerModelColumns.cpp and
// MixerModelInserts.cpp (root CLAUDE.md's "<Class>Internal.h" convention for a class split across
// several .cpp units). Not part of the public MixerModel.h surface.

#include "MixerModel.h"

namespace synth {

/** Fills `column.inserts`/`insertChainIsLinear`/`editOnCanvasTargetUuid` by walking forward from
 *  `column`'s first feeding track's source node to `column.nodeId` along signal edges (see
 *  MixerModelInserts.cpp). No-op (leaves the column's insert fields at their defaults) when
 *  `column.feedingTracks` is empty -- an orphan strip has no track-anchored chain to walk. */
void buildInsertsForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                           MixerColumn& column);

} // namespace synth
