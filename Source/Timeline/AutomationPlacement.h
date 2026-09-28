#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <optional>

// AutomationPlacement.h -- which track an automation lane for a module lives on
// (docs/timeline/track-automation.md#which-track-owns-a-lane).
//
// Core: a pure read of the live graph plus the TimelineDoc, recomputed on demand (the answer moves
// as cables are patched, so a cached flag would lie). Two layers:
//   - OWNERSHIP: a module belongs to track T when T's bound source node reaches it downstream over
//     signal edges and no other track's source does.
//   - PLACEMENT: the ONE seam every "make a lane for this parameter" path goes through -- the owning
//     track when there is one, else the doc's Automation-kind track (created when missing).
namespace synth {

/** Node uid -> owning track, for every node exactly one track reaches. Nodes reached by no track or
 *  by several are absent. */
using TrackOwnershipMap = std::map<juce::uint32, TrackId>;

/** One forward walk per bound Midi/Audio track; see AutomationPlacement.cpp for the edge rule. */
TrackOwnershipMap computeTrackOwnership(const juce::AudioProcessorGraph& graph, const TimelineDoc& doc);

/** The track that owns the node whose "uuid" property is `nodeUuid`, or nullopt (shared, free, or
 *  unresolvable). */
std::optional<TrackId> resolveOwningTrack(const juce::AudioProcessorGraph& graph, const TimelineDoc& doc,
                                          const juce::String& nodeUuid);

/** The first Automation-kind track in doc order, or an invalid id. */
TrackId findAutomationTrack(const TimelineDoc& doc);

/** The track a NEW lane for `nodeUuid` belongs on: the owning track, else the Automation-kind track,
 *  creating it when missing. Mutates `doc` only to add that track -- call inside the caller's own
 *  recordTimelineChange. Invalid when a track is needed and the doc is at kMaxTracks. */
TrackId findOrCreateLaneHostTrack(const juce::AudioProcessorGraph& graph, TimelineDoc& doc,
                                  const juce::String& nodeUuid);

} // namespace synth
