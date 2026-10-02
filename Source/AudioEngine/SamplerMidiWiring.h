#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** Message thread only. Tells every SamplerModule in `graph` whether a cable lands on its MIDI input
    (SamplerModule::setMidiInputWired), so a Sampler fed only by MIDI stays silent until its first note
    instead of free-running. Idempotent. applyJSONToGraph calls it before releasing the graph's
    callback lock, so no block renders between a project's connections appearing and the flag being set. */
void publishSamplerMidiWiring(juce::AudioProcessorGraph& graph);

} // namespace synth
