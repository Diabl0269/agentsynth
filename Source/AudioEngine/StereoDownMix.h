#pragma once

#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

namespace synth {

/** Message thread only. Marks, on every ModuleBase in `graph`, the raw input channels fed only by
    complete stereo pairs (a source's Left AND Right leg), so its processBlock averages them at
    -6 dB instead of the graph's unity sum (ModuleBase::setInputDownMixMask). A channel with any
    unpaired feed stays at unity; a feed from a silent source does not count. Idempotent; cheap
    enough for every graph change. */
void publishStereoDownMix(juce::AudioProcessorGraph& graph);

/** Message thread only. The sound sources adding exact zeros right now: bypassed, muted, or at
    Level 0 with nothing cabled into their Level jack. */
std::set<juce::AudioProcessorGraph::NodeID> findSilentSources(juce::AudioProcessorGraph& graph);

/** Message thread only. Changes whenever a source's bypass, mute or Level crosses silence; never
    reads the connection list, so it is cheap enough to poll. */
std::uint64_t silentSourcesSignature(juce::AudioProcessorGraph& graph);

} // namespace synth
