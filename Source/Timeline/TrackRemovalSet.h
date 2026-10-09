#pragma once

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace synth {

// Which modules deleting a track takes with it besides the track's own nodes
// (docs/timeline/tracks.md#row-context-menu). Pure: node ids and cables in, ids out, so it is testable without a graph.
//
// `seed` is what goes regardless: the track's bound node, its macro with everything inside, its own mixer strip.
// `keep` is what never goes (the output dock, nodes another track is bound to, members of a macro that stays).
// `cables` are every graph connection, audio, CV and MIDI alike, as (source, destination) node ids.
//
// A module goes when EVERY cable it has ends in the seed or in another module that goes: an LFO wired only into
// the track, and an LFO -> LFO -> track chain, but also the Attenuverter between an LFO and a knob (that pair
// leaves together; neither alone passes the test). The answer is the largest such set, minus any cluster that
// touches the seed nowhere. A module with no cable at all was never "used by" the track, so it stays; so does one
// with a single cable to anything outside, which only loses its cables into the removed set.
//
// `relays` are the hidden Attenuverters that carry one modulation routing from its source to a knob. A relay is the
// link, not a module someone else may use, so it goes as soon as everything it feeds goes, even when its source stays
// (an LFO that modulates two tracks keeps its link to the other one and loses the one into the deleted track).
std::set<std::uint32_t> modulesOnlyUsedBy(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& cables,
                                          const std::set<std::uint32_t>& seed, const std::set<std::uint32_t>& keep,
                                          const std::set<std::uint32_t>& relays = {});

} // namespace synth
