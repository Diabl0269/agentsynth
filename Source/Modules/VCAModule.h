#pragma once

#include "ModuleBase.h"
#include <cmath>

class VCAModule : public ModuleBase {
public:
    VCAModule()
        : ModuleBase("VCA", 16,
                     8) // 0-7: per-voice audio, 8-15: per-voice CV, outputs 0-7: gated audio (poly sums to ch0/1)
    {
        addParameter(gainParam = new juce::AudioParameterFloat("gain", "Gain", 0.0f, 1.0f, 0.5f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        smoothedGain.reset(sampleRate, 0.01);
        smoothedGain.setCurrentAndTargetValue(*gainParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        if (numChannels == 0 || numSamples == 0)
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (isBypassed()) {
            // Clear CV channels (CV starts at 1 in mono, 8 in poly)
            int cvStart = polyParam->get() ? 8 : 1;
            for (int ch = cvStart; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        smoothedGain.setTargetValue(*gainParam);

        if (!polyParam->get()) {
            // --- Mono mode: read CV from ch1 (legacy) or ch8 (poly layout fallback) ---
            auto* audioData = buffer.getWritePointer(0);
            const float* cvData = (numChannels > 1) ? buffer.getReadPointer(1) : nullptr;

            // If ch1 has no signal, try ch8 (poly envelope routing)
            if (cvData != nullptr && numChannels > 8) {
                float rms1 = 0.0f;
                for (int s = 0; s < std::min(numSamples, 64); ++s)
                    rms1 += cvData[s] * cvData[s];
                if (rms1 < 1e-6f)
                    cvData = buffer.getReadPointer(8);
            }

            for (int s = 0; s < numSamples; ++s) {
                float cv = (cvData != nullptr) ? cvData[s] : 1.0f;
                audioData[s] *= smoothedGain.getNextValue() * cv;
            }

            if (numChannels > 1) {
                buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
            }
            if (auto* vb = getVisualBuffer())
                for (int s = 0; s < numSamples; ++s)
                    vb->pushSample(buffer.getSample(0, s));
        } else {
            // --- Poly mode: 8 voices summed to stereo (ch0/ch1) ---
            // Each voice is multiplied by its envelope CV and the master gain,
            // then all voices are accumulated into a single stereo sum.
            // A fixed 1/MAX_VOICES normalization prevents hot signals from clipping,
            // and std::tanh provides gentle soft saturation as a safety net.
            static constexpr float kNorm = 1.0f / static_cast<float>(MAX_VOICES);

            if (numChannels >= 2) {
                auto* outL = buffer.getWritePointer(0);
                auto* outR = buffer.getWritePointer(1);

                for (int s = 0; s < numSamples; ++s) {
                    float gain = smoothedGain.getNextValue();
                    float sum = 0.0f;
                    for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
                        float audio = buffer.getSample(v, s);
                        float cv = (v + MAX_VOICES < numChannels) ? buffer.getSample(v + MAX_VOICES, s) : 1.0f;
                        sum += audio * gain * cv;
                    }
                    float mixed = std::tanh(sum * kNorm);
                    outL[s] = mixed;
                    outR[s] = mixed;
                }

                // Zero out voice channels 2-7 so they don't leak downstream
                for (int v = 2; v < MAX_VOICES && v < numChannels; ++v)
                    buffer.clear(v, 0, numSamples);
            }
            if (auto* vb = getVisualBuffer())
                for (int s = 0; s < numSamples; ++s)
                    vb->pushSample(buffer.getSample(0, s));
        }

        // Clear CV channels to prevent leaking to downstream modules
        int cvStart = polyParam->get() ? 8 : 1;
        // Exception: in mono mode we copy audio to ch1 for visual feedback,
        // so we only clear from ch2 onwards if it exists.
        // If we want absolute cleanliness, we should clear ch1 too, but that breaks VCA viz.
        // We'll stick to the existing behavior for VCA ch1 but clear others.
        for (int ch = (polyParam->get() ? 8 : 2); ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"CV", 1}}; }
    juce::String getInputPortLabel(int i) const override { return (i == 0) ? "Audio" : "CV"; }
    juce::String getOutputPortLabel(int) const override { return "Audio"; }
    int getVisibleInputPortCount() const override { return 2; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModuleType getModuleType() const override { return ModuleType::VCA; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            // Poly mode: raw 0-7 = per-voice Audio fan; raw 8-15 = per-voice ModCV fan
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
            if (raw >= 8 && raw <= 15) {
                p.visibleJackIndex = 1;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = (raw == 8);
                p.polyVoiceSpan = (raw == 8) ? 8 : 1;
                return p;
            }
        } else {
            // Mono mode
            if (raw == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 1) {
                p.visibleJackIndex = 1;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
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
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;
};
