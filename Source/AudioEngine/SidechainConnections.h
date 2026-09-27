#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** Message thread only. Tells every ModuleBase in `graph` whether a cable currently lands on one of
    its PortRole::Sidechain inputs (ModuleBase::setSidechainConnected), so a keyed Compressor/Gate
    listens to its key only while one is plugged in. Idempotent; cheap enough for every graph change. */
void publishSidechainConnections(juce::AudioProcessorGraph& graph);

} // namespace synth
