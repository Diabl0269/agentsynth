#pragma once

#include "ModuleBase.h"
#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

class NoiseModule : public ModuleBase {
public:
    NoiseModule()
        : ModuleBase("Noise", 10, 10) // 10 inputs (0-7 voice audio/CV, 8=Color CV, 9=Level CV), 10 outputs
    {
        addParameter(typeParam = new juce::AudioParameterChoice("noiseType", "Type", {"White", "Pink", "Brown"}, 0));
        addParameter(colorParam = new juce::AudioParameterFloat("color", "Color", -1.0f, 1.0f, 0.0f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (int v = 0; v < MAX_VOICES; ++v) {
            voices[v].reset();
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) override {
        if (buffer.getNumChannels() == 0)
            return;

        if (isBypassed() || isMuted()) {
            buffer.clear();
            return;
        }

        bool isPoly = polyParam->get();
        if (isPoly) {
            processPolyMode(buffer);
        } else {
            processMonoMode(buffer);
        }
    }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Color", 8}, {"Level", 9}}; }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Color", "Level"};
        return (i >= 0 && i < 2) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }

    juce::String getOutputPortLabel(int) const override { return "Audio"; }

    int getVisibleInputPortCount() const override { return 2; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Noise; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (raw == 8 || raw == 0) {
            p.visibleJackIndex = 0;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        if (raw == 9 || raw == 1) {
            p.visibleJackIndex = 1;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    bool isAutoPromotableModTarget(int dstChannel) const override {
        if (polyParam->get())
            return false;
        return ModuleBase::isAutoPromotableModTarget(dstChannel);
    }

private:
    static constexpr int MAX_VOICES = 8;

    struct VoiceState {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        float brown = 0;
        float lastOutput = 0;

        void reset() {
            b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0;
            brown = 0;
            lastOutput = 0;
        }

        float generate(int type, float color, float white, double sampleRate) {
            float out = white;
            if (type == 1) { // Pink (Kellett filter)
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
                b6 = white * 0.115926f;
                out *= 0.11f;
            } else if (type == 2) { // Brown
                brown = (brown + (0.02f * white)) / 1.02f;
                out = brown * 3.0f;
            }

            // 1-pole DJ-style filter for Color (-1 to 1)
            if (color > 0.01f) {
                // High pass
                float fc = 20.0f * std::pow(1000.0f, color);
                float omega = 2.0f * juce::MathConstants<float>::pi * fc / (float)sampleRate;
                float alpha = std::exp(-omega);
                lastOutput = alpha * lastOutput + (1.0f - alpha) * out;
                out = out - lastOutput;
                out *= (1.0f + color * 1.5f);
            } else if (color < -0.01f) {
                // Low pass
                float fc = 20000.0f * std::pow(0.001f, -color);
                float omega = 2.0f * juce::MathConstants<float>::pi * fc / (float)sampleRate;
                float alpha = std::exp(-omega);
                lastOutput = alpha * lastOutput + (1.0f - alpha) * out;
                out = lastOutput;
                out *= (1.0f + (-color) * 1.5f);
            } else {
                lastOutput *= 0.99f;
            }
            return out;
        }
    };

    VoiceState voices[MAX_VOICES];
    juce::Random random;
    double currentSampleRate = 44100.0;

    // Pre-allocated arrays to avoid heap allocations in audio thread
    std::array<float, 4096> cvColorCache{};
    std::array<float, 4096> cvLevelCache{};

    static bool isChannelActive(const juce::AudioBuffer<float>& buffer, int ch, int numSamples) {
        if (ch >= buffer.getNumChannels())
            return false;
        const float* data = buffer.getReadPointer(ch);
        int checkLen = std::min(numSamples, 64);
        for (int i = 0; i < checkLen; ++i) {
            if (data[i] != 0.0f)
                return true;
        }
        return false;
    }

    void processMonoMode(juce::AudioBuffer<float>& buffer) {
        int numSamples = buffer.getNumSamples();
        int numCh = buffer.getNumChannels();
        int ns = std::min(numSamples, 4096);

        std::fill_n(cvColorCache.data(), ns, 0.0f);
        std::fill_n(cvLevelCache.data(), ns, 0.0f);

        bool hasColorCV = isChannelActive(buffer, 8, numSamples) || isChannelActive(buffer, 0, numSamples);
        bool hasLevelCV = isChannelActive(buffer, 9, numSamples) || isChannelActive(buffer, 1, numSamples);

        int colorCh = isChannelActive(buffer, 8, numSamples) ? 8 : 0;
        int levelCh = isChannelActive(buffer, 9, numSamples) ? 9 : 1;

        if (hasColorCV && colorCh < numCh)
            std::copy_n(buffer.getReadPointer(colorCh), ns, cvColorCache.data());
        if (hasLevelCV && levelCh < numCh)
            std::copy_n(buffer.getReadPointer(levelCh), ns, cvLevelCache.data());

        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        auto* ch0 = buffer.getWritePointer(0);
        int type = typeParam->getIndex();
        float baseColor = colorParam->get();
        float baseLevel = levelParam->get();

        for (int i = 0; i < numSamples; ++i) {
            int idx = std::min(i, ns - 1);
            float white = (random.nextFloat() * 2.0f) - 1.0f;
            float color = baseColor;
            if (hasColorCV)
                color = juce::jlimit(-1.0f, 1.0f, color + cvColorCache[idx]);
            float level = baseLevel;
            if (hasLevelCV)
                level = juce::jlimit(0.0f, 1.0f, level + cvLevelCache[idx]);

            float out = voices[0].generate(type, color, white, currentSampleRate);
            ch0[i] = juce::jlimit(-1.0f, 1.0f, out * level);
        }

        if (auto* vb = getVisualBuffer()) {
            for (int i = 0; i < numSamples; ++i) {
                vb->pushSample(ch0[i]);
            }
        }
    }

    void processPolyMode(juce::AudioBuffer<float>& buffer) {
        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        int ns = std::min(numSamples, 4096);

        std::fill_n(cvColorCache.data(), ns, 0.0f);
        std::fill_n(cvLevelCache.data(), ns, 0.0f);

        bool hasColorCV = isChannelActive(buffer, 8, numSamples);
        bool hasLevelCV = isChannelActive(buffer, 9, numSamples);

        if (hasColorCV && 8 < numChannels)
            std::copy_n(buffer.getReadPointer(8), ns, cvColorCache.data());
        if (hasLevelCV && 9 < numChannels)
            std::copy_n(buffer.getReadPointer(9), ns, cvLevelCache.data());

        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        int type = typeParam->getIndex();
        float baseColor = colorParam->get();
        float baseLevel = levelParam->get();

        for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
            float* output = buffer.getWritePointer(v);

            for (int s = 0; s < numSamples; ++s) {
                int idx = std::min(s, ns - 1);
                float white = (random.nextFloat() * 2.0f) - 1.0f;
                float color = baseColor;
                if (hasColorCV)
                    color = juce::jlimit(-1.0f, 1.0f, color + cvColorCache[idx]);
                float level = baseLevel;
                if (hasLevelCV)
                    level = juce::jlimit(0.0f, 1.0f, level + cvLevelCache[idx]);

                float out = voices[v].generate(type, color, white, currentSampleRate);
                output[s] = juce::jlimit(-1.0f, 1.0f, out * level);
            }
        }

        if (auto* vb = getVisualBuffer()) {
            if (numChannels > 0) {
                const float* ch0 = buffer.getReadPointer(0);
                for (int s = 0; s < numSamples; ++s)
                    vb->pushSample(ch0[s]);
            }
        }
    }

    juce::AudioParameterChoice* typeParam = nullptr;
    juce::AudioParameterFloat* colorParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseModule)
};
