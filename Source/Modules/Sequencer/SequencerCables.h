#pragma once

#include "Modules/ModTargetList.h"
#include "SequencerPlayer.h"

namespace synth {

/** The step knobs' cables: Transpose, Gate, Velocity and Chance, read on the sample each step hits. The cable moves
 *  the value that one step plays with and is never written into the knob. Audio thread only. */
class SequencerCables {
public:
    SequencerCables(const ModTargetList& list, const juce::RangedAudioParameter& transpose,
                    const juce::RangedAudioParameter& gate, const juce::RangedAudioParameter& velocity,
                    const juce::RangedAudioParameter& chance) noexcept
        : list_(list)
        , transpose_(transpose)
        , gate_(gate)
        , velocity_(velocity)
        , chance_(chance) {}

    /** The block whose channels the cables are read from; it must outlive the renderBlock() call it serves. */
    void attach(const juce::AudioBuffer<float>& block) noexcept { block_ = &block; }

    /** `base` with each of the four knobs moved by its cable at `sample`. */
    SequencerSettings at(const SequencerSettings& base, int sample) const noexcept;

private:
    const ModTargetList& list_;
    const juce::RangedAudioParameter& transpose_;
    const juce::RangedAudioParameter& gate_;
    const juce::RangedAudioParameter& velocity_;
    const juce::RangedAudioParameter& chance_;
    const juce::AudioBuffer<float>* block_ = nullptr;
};

} // namespace synth
