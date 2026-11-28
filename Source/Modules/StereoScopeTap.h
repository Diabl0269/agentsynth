#pragma once

#include "ModuleBase.h"

namespace synth {

/** Feeds a stereo audio module's scope from its output block: the mid of the left leg (raw ch0) and the right leg
    (`rightAudioLegChannel()`) while Dual I/O is split and that leg is in the block, so a balance pan to one side still
    draws; the left leg alone otherwise (a joined jack's legs are the same signal). Audio thread: no allocation. */
inline void pushOutputScope(VisualBuffer& vb, const ModuleBase& module, const juce::AudioBuffer<float>& buffer,
                            int numSamples) noexcept {
    if (buffer.getNumChannels() == 0)
        return;
    const int right = module.rightAudioLegChannel();
    if (module.isDualIO() && right > 0 && right < buffer.getNumChannels())
        vb.pushStereoBlock(buffer.getReadPointer(0), buffer.getReadPointer(right), numSamples);
    else
        vb.pushBlock(buffer.getReadPointer(0), numSamples);
}

} // namespace synth
