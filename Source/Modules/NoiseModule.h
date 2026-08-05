#pragma once

#include "ModuleBase.h"
#include <juce_core/juce_core.h>

class NoiseModule : public ModuleBase {
public:
    NoiseModule()
        : ModuleBase("Noise", 10, 10) // up to 8 voices out, plus 2 shared CV inputs (Color, Level) -> total 10 out channels for safety
    {
        addParameter(typeParam = new juce::AudioParameterChoice("noiseType", "Type", {"White", "Pink", "Brown"}, 0));
        addParameter(colorParam = new juce::AudioParameterFloat("color", "Color", -1.0f, 1.0f, 0.0f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        juce::ignoreUnused(sampleRate);
        for (int v = 0; v < MAX_VOICES; ++v) {
            voices[v].reset();
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) override {
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

    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Color", 8}, {"Level", 9}};
        return {{"Color", 0}, {"Level", 1}};
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Color", "Level"};
        if (polyParam->get()) {
            // poly mode visible input ports: 0=Color, 1=Level
            return (i >= 0 && i < 2) ? labels[i] : ModuleBase::getInputPortLabel(i);
        } else {
            // mono mode visible input ports: 0=Color, 1=Level
            return (i >= 0 && i < 2) ? labels[i] : ModuleBase::getInputPortLabel(i);
        }
    }

    juce::String getOutputPortLabel(int) const override { return "Audio"; }

    int getVisibleInputPortCount() const override { return 2; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; } // Noise acts like a source
    ModuleType getModuleType() const override { return ModuleType::Noise; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            // Poly mode: raw 8 = Color, raw 9 = Level
            if (raw == 8) {
                p.visibleJackIndex = 0;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 9) {
                p.visibleJackIndex = 1;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        } else {
            // Mono mode: raw 0 = Color CV, 1 = Level CV
            if (raw == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::ModCV;
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

    struct VoiceState {
        // Pink noise state
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        // Brown noise state
        float brown = 0;
        // Filter state for Color param
        float lastOutput = 0;

        void reset() {
            b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0;
            brown = 0;
            lastOutput = 0;
        }

        float generate(int type, float color, float white) {
            float out = white;
            if (type == 1) { // Pink
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
                b6 = white * 0.115926f;
                out *= 0.22f; // Increased gain compensation for Pink noise to better match White noise perceived loudness
            } else if (type == 2) { // Brown
                brown = (brown + (0.02f * white)) / 1.02f;
                out = brown * 9.5f; // Increased gain compensation for Brown noise to better match White/Pink noise perceived loudness
            }
        // Simple 1-pole filter for Color (-1 to 1)
        // Center (0) is flat. <0 is Low Pass, >0 is High Pass.
        if (color > 0.01f) {
            // High pass
            float c = color * color; // non-linear for better feel
            float alpha = 0.99f - c * 0.69f; // alpha goes from 0.99 (cutoff 0) to 0.3 (high cutoff)
            lastOutput = alpha * lastOutput + (1.0f - alpha) * out;
            // Normalizer to keep peak gain at 1.0
            float norm = (1.0f + alpha) / (2.0f * alpha);
            out = (out - lastOutput) * norm;
            // Slight extra make-up for lost bandwidth
            out *= (1.0f + c * 1.5f);
        } else if (color < -0.01f) {
            // Low pass
            float c = -color;
            c = c * c; // non-linear
            float alpha = c * 0.98f; // alpha goes from 0.0 (flat) to 0.98 (low cutoff)
            lastOutput = alpha * lastOutput + (1.0f - alpha) * out;
            out = lastOutput;
            // Make-up gain for lost bandwidth
            out *= (1.0f + c * 2.5f);
        } else {
            // Near center: smoothly decay lastOutput to 0 to avoid clicks when crossing 0
            lastOutput *= 0.99f;
        }
        return out;
        }
    };

    VoiceState voices[MAX_VOICES];

    juce::Random random;

    void processMonoMode(juce::AudioBuffer<float>& buffer) {
        int numSamples = buffer.getNumSamples();
        int numCh = buffer.getNumChannels();

        // Save CV channels
        juce::HeapBlock<float> cvColorSaved(numSamples);
        juce::HeapBlock<float> cvLevelSaved(numSamples);
        juce::FloatVectorOperations::clear(cvColorSaved, numSamples);
        juce::FloatVectorOperations::clear(cvLevelSaved, numSamples);

        auto isChannelActive = [&](int ch) {
            if (ch >= numCh) return false;
            auto* data = buffer.getReadPointer(ch);
            float rms = 0.0f;
            for (int i = 0; i < std::min(numSamples, 64); ++i)
                rms += data[i] * data[i];
            return (rms / std::min(numSamples, 64)) > 1e-6f;
        };

        if (isChannelActive(0))
            juce::FloatVectorOperations::copy(cvColorSaved, buffer.getReadPointer(0), numSamples);
        if (isChannelActive(1))
            juce::FloatVectorOperations::copy(cvLevelSaved, buffer.getReadPointer(1), numSamples);

        // Clear outputs
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        const float* cvColorCh = (numCh > 0) ? cvColorSaved.get() : nullptr;
        const float* cvLevelCh = (numCh > 1) ? cvLevelSaved.get() : nullptr;
        auto* ch0 = buffer.getWritePointer(0);

        int type = typeParam->getIndex();
        float baseColor = colorParam->get();
        float baseLevel = levelParam->get();

        for (int i = 0; i < numSamples; ++i) {
            float white = (random.nextFloat() * 2.0f) - 1.0f;
            float color = baseColor;
            if (cvColorCh) color = juce::jlimit(-1.0f, 1.0f, color + cvColorCh[i]);
            float level = baseLevel;
            if (cvLevelCh) level = juce::jlimit(0.0f, 1.0f, level + cvLevelCh[i]);

            float out = voices[0].generate(type, color, white);
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

        juce::HeapBlock<float> cvColorSaved(numSamples);
        juce::HeapBlock<float> cvLevelSaved(numSamples);
        juce::FloatVectorOperations::clear(cvColorSaved, numSamples);
        juce::FloatVectorOperations::clear(cvLevelSaved, numSamples);

        auto isChannelActive = [&](int ch) {
            if (ch >= numChannels) return false;
            auto* data = buffer.getReadPointer(ch);
            float rms = 0.0f;
            for (int i = 0; i < std::min(numSamples, 64); ++i)
                rms += data[i] * data[i];
            return (rms / std::min(numSamples, 64)) > 1e-6f;
        };

        bool hasColorCV = isChannelActive(8);
        bool hasLevelCV = isChannelActive(9);

        if (hasColorCV)
            juce::FloatVectorOperations::copy(cvColorSaved, buffer.getReadPointer(8), numSamples);
        if (hasLevelCV)
            juce::FloatVectorOperations::copy(cvLevelSaved, buffer.getReadPointer(9), numSamples);

        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        int type = typeParam->getIndex();
        float baseColor = colorParam->get();
        float baseLevel = levelParam->get();

        for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
            float* output = buffer.getWritePointer(v);

            for (int s = 0; s < numSamples; ++s) {
                float white = (random.nextFloat() * 2.0f) - 1.0f;
                float color = baseColor;
                if (hasColorCV) color = juce::jlimit(-1.0f, 1.0f, color + cvColorSaved[s]);
                float level = baseLevel;
                if (hasLevelCV) level = juce::jlimit(0.0f, 1.0f, level + cvLevelSaved[s]);

                float out = voices[v].generate(type, color, white);
                output[s] = juce::jlimit(-1.0f, 1.0f, out * level);
            }
        }

        if (auto* vb = getVisualBuffer()) {
            const float* ch0 = buffer.getReadPointer(0);
            for (int s = 0; s < numSamples; ++s)
                vb->pushSample(ch0[s]);
        }
    }

    juce::AudioParameterChoice* typeParam = nullptr;
    juce::AudioParameterFloat* colorParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseModule)
};
