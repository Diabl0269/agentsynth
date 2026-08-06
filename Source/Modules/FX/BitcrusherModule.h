#pragma once

#include "../ModuleBase.h"

class BitcrusherModule : public ModuleBase {
public:
    BitcrusherModule()
        : ModuleBase("Bitcrusher", 5, 2) // 2 Audio + 3 CV (Rate, Depth, Mix)
    {
        addParameter(rateParam = new juce::AudioParameterFloat("rate", "Rate Reduction", 1.0f, 50.0f, 1.0f));
        addParameter(depthParam = new juce::AudioParameterFloat("depth", "Bit Depth", 1.0f, 24.0f, 24.0f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 1.0f));
        addParameter(ditherParam = new juce::AudioParameterFloat("dither", "Dither", 0.0f, 1.0f, 0.0f));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;

        dryBuffer.setSize(2, samplesPerBlock);

        smoothedRate.reset(currentSampleRate, 0.005);
        smoothedDepth.reset(currentSampleRate, 0.005);
        smoothedMix.reset(currentSampleRate, 0.005);
        smoothedDither.reset(currentSampleRate, 0.005);

        smoothedRate.setCurrentAndTargetValue(*rateParam);
        smoothedDepth.setCurrentAndTargetValue(*depthParam);
        smoothedMix.setCurrentAndTargetValue(*mixParam);
        smoothedDither.setCurrentAndTargetValue(*ditherParam);

        phase = 0.0f;
        lastSample[0] = 0.0f;
        lastSample[1] = 0.0f;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (isBypassed()) {
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        if (numChannels < 2 || dryBuffer.getNumSamples() < numSamples) {
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        const float* cvRate = (numChannels > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvDepth = (numChannels > 3) ? buffer.getReadPointer(3) : nullptr;
        const float* cvMix = (numChannels > 4) ? buffer.getReadPointer(4) : nullptr;

        bool cvRateActive = isChannelActive(cvRate, numSamples);
        bool cvDepthActive = isChannelActive(cvDepth, numSamples);
        bool cvMixActive = isChannelActive(cvMix, numSamples);

        smoothedRate.setTargetValue(*rateParam);
        smoothedDepth.setTargetValue(*depthParam);
        smoothedMix.setTargetValue(*mixParam);
        smoothedDither.setTargetValue(*ditherParam);

        for (int ch = 0; ch < 2; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer.getReadPointer(ch), numSamples);

        float* outL = buffer.getWritePointer(0);
        float* outR = buffer.getWritePointer(1);

        float rateScale = static_cast<float>(currentSampleRate / 44100.0);

        for (int i = 0; i < numSamples; ++i) {
            float rateMod = cvRateActive ? cvRate[i] * 25.0f : 0.0f;
            float depthMod = cvDepthActive ? cvDepth[i] * 12.0f : 0.0f;
            float mixMod = cvMixActive ? cvMix[i] : 0.0f;

            float currentRate = juce::jlimit(1.0f, 50.0f, smoothedRate.getNextValue() + rateMod) * rateScale;
            float currentDepth = juce::jlimit(1.0f, 24.0f, smoothedDepth.getNextValue() + depthMod);
            float currentMix = juce::jlimit(0.0f, 1.0f, smoothedMix.getNextValue() + mixMod);
            float currentDither = smoothedDither.getNextValue();

            float steps = std::pow(2.0f, currentDepth);

            phase += 1.0f;
            if (phase >= currentRate) {
                while (phase >= currentRate) {
                    phase -= currentRate;
                }

                for (int ch = 0; ch < 2; ++ch) {
                    float input = dryBuffer.getSample(ch, i);

                    if (currentDither > 0.0f) {
                        float noise = random.nextFloat() * 2.0f - 1.0f;
                        input += noise * currentDither * 0.1f;
                    }

                    float quantized = juce::jlimit(-1.0f, 1.0f, std::round(input * steps) / steps);
                    lastSample[ch] = quantized;
                }
            }

            outL[i] = dryBuffer.getSample(0, i) * (1.0f - currentMix) + lastSample[0] * currentMix;
            outR[i] = dryBuffer.getSample(1, i) * (1.0f - currentMix) + lastSample[1] * currentMix;
        }

        if (auto* vb = getVisualBuffer()) {
            for (int i = 0; i < numSamples; ++i) {
                vb->pushSample(outL[i]);
            }
        }

        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Left", "Right", "Rate", "Depth", "Mix"};
        return (i >= 0 && i < 5) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Rate", 2}, {"Depth", 3}, {"Mix", 4}};
    }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Bitcrusher; }

private:
    static bool isChannelActive(const float* channel, int numSamples) {
        if (!channel)
            return false;
        for (int i = 0; i < numSamples; ++i) {
            if (channel[i] != 0.0f)
                return true;
        }
        return false;
    }

    double currentSampleRate = 44100.0;
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDither;

    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::AudioParameterFloat* ditherParam = nullptr;

    float phase = 0.0f;
    float lastSample[2] = {0.0f, 0.0f};
    juce::Random random;
};
