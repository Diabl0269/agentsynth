#pragma once

#include "ModuleBase.h"
#include <algorithm>

class ADSRModule : public ModuleBase {
public:
    ADSRModule(const juce::String& name = "ADSR")
        : ModuleBase(name, 8, 8) // 8 inputs (gate CV per voice), 8 outputs (envelope per voice)
    {
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack", 0.01f, 5.0f, 0.05f));
        addParameter(decayParam = new juce::AudioParameterFloat("decay", "Decay", 0.01f, 5.0f, 0.2f));
        addParameter(sustainParam = new juce::AudioParameterFloat("sustain", "Sustain", 0.0f, 1.0f, 0.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release", 0.01f, 5.0f, 0.1f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        for (int v = 0; v < MAX_VOICES; ++v)
            adsrs[v].setSampleRate(sampleRate);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed() || isMuted() || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0) {
            buffer.clear();
            return;
        }

        float a = std::max(static_cast<float>(*attackParam), 0.002f);
        float d = *decayParam;
        float s = *sustainParam;
        float r = std::max(static_cast<float>(*releaseParam), 0.005f);

        if (a != adsrParams.attack || d != adsrParams.decay || s != adsrParams.sustain || r != adsrParams.release) {
            adsrParams.attack = a;
            adsrParams.decay = d;
            adsrParams.sustain = s;
            adsrParams.release = r;
            for (int v = 0; v < MAX_VOICES; ++v)
                adsrs[v].setParameters(adsrParams);
        }

        bool poly = *polyParam;

        if (!poly) {
            // Mono mode: MIDI gate handling, single envelope on channel 0
            for (const auto metadata : midiMessages) {
                auto message = metadata.getMessage();
                if (message.isNoteOn()) {
                    adsrs[0].noteOn();
                } else if (message.isNoteOff()) {
                    adsrs[0].noteOff();
                }
            }

            // Generate valid control signal by filling all channels with 1.0f
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                auto* data = buffer.getWritePointer(ch);
                std::fill(data, data + buffer.getNumSamples(), 1.0f);
            }

            adsrs[0].applyEnvelopeToBuffer(buffer, 0, buffer.getNumSamples());
            if (auto* vb = getVisualBuffer())
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                    vb->pushSample(buffer.getSample(0, s));
        } else {
            // Poly mode: gate CV per voice
            for (int v = 0; v < MAX_VOICES; ++v)
                adsrs[v].setParameters(adsrParams);

            int numChannels = buffer.getNumChannels();
            int numSamples = buffer.getNumSamples();

            for (int v = 0; v < std::min(MAX_VOICES, numChannels); ++v) {
                // Read gate CV from input channel v
                const float* gateData = buffer.getReadPointer(v);

                // Edge detection at start of block
                bool gateHigh = gateData[0] > 0.5f;
                if (gateHigh && !previousGateState[v])
                    adsrs[v].noteOn();
                if (!gateHigh && previousGateState[v])
                    adsrs[v].noteOff();
                previousGateState[v] = gateHigh;

                // Generate per-voice envelope (can't use applyEnvelopeToBuffer
                // as it applies to ALL channels, corrupting other voices)
                float* out = buffer.getWritePointer(v);
                for (int smp = 0; smp < numSamples; ++smp)
                    out[smp] = adsrs[v].getNextSample();
            }
            if (auto* vb = getVisualBuffer())
                for (int s = 0; s < numSamples; ++s)
                    vb->pushSample(buffer.getSample(0, s));
        }
    }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Envelope; }
    juce::String getInputPortLabel(int) const override { return "Gate"; }
    juce::String getOutputPortLabel(int) const override { return "Env"; }
    int getVisibleInputPortCount() const override { return 1; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModuleType getModuleType() const override { return ModuleType::ADSR; }

private:
    static constexpr int MAX_VOICES = 8;
    juce::ADSR adsrs[MAX_VOICES];
    juce::ADSR::Parameters adsrParams;
    bool previousGateState[MAX_VOICES] = {};
    juce::AudioParameterBool* polyParam = nullptr;

    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* sustainParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
};
