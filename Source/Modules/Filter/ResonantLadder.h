#pragma once

#include <array>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace synth {

/** Feedback gain of the ladder (0 = none, 1 = on the edge of self-oscillation) for a Resonance knob value in 0..1. */
float ladderFeedbackForResonance(float resonanceKnob) noexcept;

/** Highest feedback that still decays, for a cutoff of `cutoffOverSampleRate` (cutoff Hz / sample rate): the one-sample
    delay in the feedback path pulls the stability limit below 1 for cutoffs above about an eighth of the sample rate.
 */
float ladderFeedbackCeiling(float cutoffOverSampleRate) noexcept;

/** Gain applied to the signal before the ladder's saturation (and undone after it) for a Drive knob value in 1..10. */
float ladderHeadroomForDrive(float drive) noexcept;

/** A four-stage resonant ladder (low-, high- and band-pass, 12 and 24 dB): the structure of juce::dsp::LadderFilter
    with the Resonance knob's own curve and a clean signal path at Drive 1. Same calls as the JUCE class, so
    LadderCoefficientSetter drives either. `setResonance` takes the knob value; `process` handles any channel count
    the ladder was prepared for. */
class ResonantLadder {
public:
    using Mode = juce::dsp::LadderFilterMode;

    ResonantLadder();

    void setEnabled(bool isEnabled) noexcept { enabled_ = isEnabled; }
    void setMode(Mode newMode) noexcept;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    void setCutoffFrequencyHz(float hz) noexcept;
    void setResonance(float resonanceKnob) noexcept;
    void setDrive(float drive) noexcept;

    template <typename ProcessContext>
    void process(const ProcessContext& context) noexcept {
        const auto& in = context.getInputBlock();
        auto& out = context.getOutputBlock();
        const auto numChannels = out.getNumChannels();
        const auto numSamples = out.getNumSamples();
        if (!enabled_ || context.isBypassed) {
            out.copyFrom(in);
            return;
        }
        for (size_t n = 0; n < numSamples; ++n) {
            cutoffCoefficient_ = cutoffSmoother_.getNextValue();
            feedback_ = feedbackSmoother_.getNextValue();
            for (size_t ch = 0; ch < numChannels; ++ch)
                out.getChannelPointer(ch)[n] = processSample(in.getChannelPointer(ch)[n], ch);
        }
    }

private:
    static constexpr size_t kStates = 5;

    float processSample(float input, size_t channel) noexcept;
    void setSampleRate(float sampleRate) noexcept;
    void updateCutoff() noexcept;
    void updateFeedback() noexcept;

    float drive_ = 1.0f, drive2_ = 1.0f, gain_ = 1.0f, gain2_ = 1.0f, comp_ = 0.5f;
    float headroom_ = 1.0f, inverseHeadroom_ = 1.0f;
    std::vector<std::array<float, kStates>> state_;
    std::array<float, kStates> outputMix_{};
    juce::SmoothedValue<float> cutoffSmoother_, feedbackSmoother_;
    float cutoffCoefficient_ = 0.0f, feedback_ = 0.0f;
    float cutoffHz_ = 200.0f, cutoffScaler_ = 0.0f, sampleRate_ = 1000.0f, knobFeedback_ = 0.0f;
    Mode mode_ = Mode::LPF12;
    bool enabled_ = true;
};

} // namespace synth
