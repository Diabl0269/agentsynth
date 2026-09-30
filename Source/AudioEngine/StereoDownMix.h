#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** Message thread only. Marks, on every ModuleBase in `graph`, the raw input channels fed only by
    complete stereo pairs (a source's Left AND Right leg), so its processBlock averages them at
    -6 dB instead of the graph's unity sum (ModuleBase::setInputDownMixMask). A channel with any
    unpaired feed stays at unity. Idempotent; cheap enough for every graph change. */
void publishStereoDownMix(juce::AudioProcessorGraph& graph);

} // namespace synth
