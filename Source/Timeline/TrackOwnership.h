#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <map>
#include <utility>
#include <vector>

namespace synth {

// Which timeline track "plays" which graph node, so an automation lane can sit on the track whose module it
// moves (docs/timeline/automation.md#which-track-a-lane-lands-on). Pure: uuids and edges in, a map out, so it is
// testable without a graph.
//
// This is the same claim auto-arrange makes for its track rows (HierarchicalArrange.cpp assignRows,
// docs/layout/layout.md#auto-arrange), with one difference: arrange gives a block reachable from two tracks to the
// EARLIER track, because a block has to sit in some row; here a node two tracks reach (the master bus, a shared
// effect) belongs to NO track, because moving a lane onto "the first track that happens to reach it" would hide it
// somewhere arbitrary. Keep the two rules in step when either changes.
struct OwnershipGraph {
    std::vector<juce::String> nodes;                              // node uuids
    std::vector<std::pair<juce::String, juce::String>> flowEdges; // every graph connection (audio/CV/MIDI)
    std::vector<std::pair<juce::String, juce::String>> modEdges;  // modulation routings, source -> destination
};

// `trackStarts` is each track's start node uuid (Track In / Track Audio) in timeline order. Unowned nodes are
// absent from the result.
//
//  1. A track owns the nodes reachable forward over flow edges from its start, unless another track reaches
//     them too.
//  2. A node no track reaches but whose flow neighbours (either direction) that have an owner all share one owner
//     joins it (an instrument feeding the chain, an LFO cabled into a filter's CV jack). Repeats to a fixed point.
//  3. A pure modulator (modulation consumers, no modulation input, no outgoing flow edge) is invisible to 1 and
//     2 -- an LFO that takes MIDI from track A but modulates track B belongs to B. It takes the one owner its
//     owned consumers share; consumers in two tracks leave it unowned.
std::map<juce::String, TrackId> resolveTrackOwners(const OwnershipGraph& graph,
                                                   const std::vector<std::pair<TrackId, juce::String>>& trackStarts);

} // namespace synth
