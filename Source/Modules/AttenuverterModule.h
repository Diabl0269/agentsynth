#pragma once

#include "ModuleBase.h"

class AttenuverterModule : public ModuleBase {
public:
    AttenuverterModule()
        : ModuleBase("Attenuverter", 2, 1) // 1 Audio In + 1 CV In (Amount), 1 Out
    {
        addParameter(amountParam = new juce::AudioParameterFloat("amount", "Amount", -1.0f, 1.0f, 0.0f));
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        smoothedAmount.reset(sampleRate, 0.01);
        smoothedAmount.setCurrentAndTargetValue(*amountParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
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
        const float* cvAmountData = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;

        bool cvAmountActive = false;
        if (cvAmountData) {
            float rms = 0.0f;
            for (int i = 0; i < std::min(numSamples, 64); ++i)
                rms += cvAmountData[i] * cvAmountData[i];
            cvAmountActive = (rms / std::min(numSamples, 64)) > 1e-6f;
        }
        smoothedAmount.setTargetValue(*amountParam);

        for (int sample = 0; sample < numSamples; ++sample) {
            float amount = smoothedAmount.getNextValue();
            if (cvAmountActive)
                amount = juce::jlimit(-1.0f, 1.0f, amount + cvAmountData[sample]);
            audioData[sample] *= amount;
        }

        // Track output for UI visualization
        float peak = 0.0f;
        for (int s = 0; s < numSamples; ++s)
            peak = std::max(peak, std::abs(audioData[s]));
        lastOutputPeak.store(peak, std::memory_order_relaxed);
        lastModValue.store(numSamples > 0 ? audioData[numSamples / 2] : 0.0f, std::memory_order_relaxed);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 1; ch < numChannels; ++ch) {
            buffer.clear(ch, 0, numSamples);
        }
    }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Amount", 1}}; }
    juce::String getInputPortLabel(int i) const override { return i == 0 ? "Signal" : "Amount"; }
    juce::String getOutputPortLabel(int) const override { return "Out"; }

    // Pure audio/CV utility — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::Attenuverter; }

    float getLastOutputPeak() const { return lastOutputPeak.load(std::memory_order_relaxed); }
    float getLastModValue() const { return lastModValue.load(std::memory_order_relaxed); }

private:
    juce::AudioParameterFloat* amountParam;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedAmount;
    std::atomic<float> lastOutputPeak{0.0f};
    std::atomic<float> lastModValue{0.0f};
};
