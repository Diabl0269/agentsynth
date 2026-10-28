#pragma once

#include "AudioEngine/ConnectionIndex.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

namespace synth {

/** An output jack that carries pitch: the node and its visible jack index (what GraphEditor::connectPorts takes). */
struct PitchSource {
    juce::AudioProcessorGraph::NodeID node;
    int visibleJack = 0;
};

/** True when a cable enters the Pitch input of `module` (any voice of it). */
bool hasPitchCable(const ConnectionIndex& cables, const juce::AudioProcessorGraph& graph,
                   juce::AudioProcessorGraph::NodeID module);

/**
 * The Pitch source that feeds the voice chain `module` sits in: walks upstream over the audio cables into `module`
 * (breadth first, nearest first) and returns the source of the first Pitch input found on the way (an Oscillator's,
 * fed by Poly MIDI), or of the first input a Pitch output feeds (a mono Oscillator's pitch CV). `module`'s own Pitch
 * input is not looked at. Nullopt when no module upstream has a Pitch cable. Pure query, message thread only.
 */
std::optional<PitchSource> upstreamPitchSource(const ConnectionIndex& cables, const juce::AudioProcessorGraph& graph,
                                               juce::AudioProcessorGraph::NodeID module);

} // namespace synth
