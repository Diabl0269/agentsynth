#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace synth {

class ConnectionIndex;
struct OscillatorSleepCone;

/** Message thread only. The cone of mono Oscillator `oscillator`'s output in `graph`, or an empty cone when any cable
    lets it be heard other than through a VCA gated by envelopes (docs/architecture/audio-engine.md#oscillator-sleep).
 */
std::unique_ptr<OscillatorSleepCone> buildOscillatorSleepCone(const juce::AudioProcessorGraph& graph,
                                                              const ConnectionIndex& cables,
                                                              juce::AudioProcessorGraph::NodeID oscillator);

/** Message thread only. Proves and publishes every Oscillator's cone; idempotent, cheap enough for every graph change.
 */
void publishOscillatorSleepCones(juce::AudioProcessorGraph& graph);

/** Message thread only. Frees the cones the render has replaced since the last publish. */
void collectOscillatorSleepCones(juce::AudioProcessorGraph& graph);

} // namespace synth
