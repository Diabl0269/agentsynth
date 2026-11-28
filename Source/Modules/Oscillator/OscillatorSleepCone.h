// OscillatorSleepCone.h -- everything a mono Oscillator's output can reach, proven on the message thread
// (Source/AudioEngine/OscillatorSleepProof.cpp) and read on the render thread (OscillatorModuleSleep.cpp): every cable
// out of it enters a VCA whose gain CV comes only from envelopes, directly or through linear Attenuverters and Macro
// pass-throughs. docs/architecture/audio-engine.md#oscillator-sleep.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class VCAModule;
class ADSRModule;
class AttenuverterModule;

namespace synth {

/** Immutable once published. An empty cone (no VCA) never lets the Oscillator sleep. */
struct OscillatorSleepCone {
    std::vector<juce::AudioProcessorGraph::Node::Ptr> keepAlive; ///< every node below, released on the message thread
    std::vector<VCAModule*> vcas;
    std::vector<ADSRModule*> envelopes;
    std::vector<AttenuverterModule*> attenuverters;

    /** Render thread: this block every VCA gates by zero, so the Oscillator's output is not heard. */
    bool atRest() const noexcept;
};

} // namespace synth
