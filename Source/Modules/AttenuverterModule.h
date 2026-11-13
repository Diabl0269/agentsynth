#pragma once

#include "ModuleBase.h"
#include <memory>

namespace synth {
struct ModCurve;
}

class AttenuverterModule : public ModuleBase {
public:
    AttenuverterModule();
    ~AttenuverterModule() override;

    /** Message thread only. A linear curve costs the audio thread nothing. */
    void setCurve(const synth::ModCurve& curve);
    /** Message thread only. */
    synth::ModCurve getCurve() const;

    /** A void var while the curve is linear, so untouched attenuverters add no state. */
    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        prepareOutputStage(sampleRate, samplesPerBlock);
        smoothedAmount.reset(sampleRate, 0.01);
        smoothedAmount.setCurrentAndTargetValue(*amountParam);
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        if (isBypassed()) {
            lastOutputPeak.store(0.0f, std::memory_order_relaxed);
            lastModValue.store(0.0f, std::memory_order_relaxed);
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        auto* audioData = buffer.getWritePointer(0);
        applyCurve(audioData, numSamples);
        const float* cvAmountData = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;

        // Whole block: an envelope that idles at 0 and fires mid-block must not be missed.
        const bool cvAmountActive = hasAnyNonZero(cvAmountData, numSamples);
        smoothedAmount.setTargetValue(*amountParam);

        if (!cvAmountActive && !smoothedAmount.isSmoothing()) {
            // A steady Amount with no CV: one vector multiply, the same product per sample as the loop.
            juce::FloatVectorOperations::multiply(audioData, smoothedAmount.getCurrentValue(), numSamples);
        } else {
            for (int sample = 0; sample < numSamples; ++sample) {
                float amount = smoothedAmount.getNextValue();
                if (cvAmountActive)
                    amount = juce::jlimit(-1.0f, 1.0f, amount + cvAmountData[sample]);
                audioData[sample] *= amount;
            }
        }

        // Track output for UI visualization
        const auto range = juce::FloatVectorOperations::findMinAndMax(audioData, numSamples);
        lastOutputPeak.store(std::max({0.0f, -range.getStart(), range.getEnd()}), std::memory_order_relaxed);
        lastModValue.store(numSamples > 0 ? audioData[numSamples / 2] : 0.0f, std::memory_order_relaxed);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 1; ch < numChannels; ++ch) {
            buffer.clear(ch, 0, numSamples);
        }
    }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Amount", 1, "amount"}}; }
    juce::String getInputPortLabel(int i) const override { return i == 0 ? "Signal" : "Amount"; }
    juce::String getOutputPortLabel(int) const override { return "Out"; }

    // Pure audio/CV utility — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::Attenuverter; }

    float getLastOutputPeak() const { return lastOutputPeak.load(std::memory_order_relaxed); }
    float getLastModValue() const { return lastModValue.load(std::memory_order_relaxed); }

private:
    /** Audio thread. Maps the signal through the curve before the amount; does nothing while linear. */
    void applyCurve(float* audioData, int numSamples) noexcept;

    juce::AudioParameterFloat* amountParam;
    struct CurveState;
    std::unique_ptr<CurveState> curveState_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAmount;
    std::atomic<float> lastOutputPeak{0.0f};
    std::atomic<float> lastModValue{0.0f};
};
