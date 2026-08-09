#pragma once

#include "ModuleBase.h"
#include <algorithm>
#include <cmath>

// Channel map:
//   Input  ch0 = A, ch1 = B
//   Output ch0 = Sum (A+B), ch1 = Difference (A-B), ch2 = Minimum, ch3 = Maximum, ch4 = Product (A*B)
class MathModule : public ModuleBase {
public:
    MathModule()
        : ModuleBase("Math", 2, 5) // 2 inputs (A, B), 5 outputs (Sum, Diff, Min, Max, Mult)
    {
        addParameter(clipParam =
                         new juce::AudioParameterChoice("clip", "Clip", juce::StringArray{"Off", "Hard", "Soft"}, 0));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        // Bypass: dry pass-through. Input A (ch0) is this module's dry signal and is left
        // untouched. Every other channel is a *derived* output, so it is cleared — that also
        // stops input B (which aliases the Difference output) from leaking downstream.
        if (isBypassed()) {
            for (int ch = 1; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        if (isMuted()) {
            buffer.clear();
            return;
        }

        // In the JUCE graph the output channels ALIAS the input channels of the same buffer,
        // so ch0 is both input A and the Sum output (and ch1 is both input B and the
        // Difference output). Read `a` and `b` for the current sample FIRST, then write all
        // five outputs for that sample — never clear or overwrite ch0/ch1 before reading them.
        float* ch[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        for (int i = 0; i < 5; ++i)
            if (i < numChannels)
                ch[i] = buffer.getWritePointer(i);

        const int clipMode = clipParam->getIndex();

        for (int s = 0; s < numSamples; ++s) {
            // Read both inputs before writing: ch[0]/ch[1] are also the Sum/Difference outputs.
            const float a = ch[0] != nullptr ? ch[0][s] : 0.0f;
            const float b = ch[1] != nullptr ? ch[1][s] : 0.0f;

            if (ch[0] != nullptr)
                ch[0][s] = applyClip(a + b, clipMode);
            if (ch[1] != nullptr)
                ch[1][s] = applyClip(a - b, clipMode);
            if (ch[2] != nullptr)
                ch[2][s] = applyClip(std::min(a, b), clipMode);
            if (ch[3] != nullptr)
                ch[3][s] = applyClip(std::max(a, b), clipMode);
            if (ch[4] != nullptr)
                ch[4][s] = applyClip(a * b, clipMode);
        }

        // Defensive: clear any channels beyond the 5 defined outputs so nothing stale leaks.
        for (int c = 5; c < numChannels; ++c)
            buffer.clear(c, 0, numSamples);

        // Push the Sum output into the visual buffer.
        if (auto* vb = getVisualBuffer()) {
            if (ch[0] != nullptr)
                for (int s = 0; s < numSamples; ++s)
                    vb->pushSample(ch[0][s]);
        }
    }

    juce::String getInputPortLabel(int i) const override {
        return i == 0 ? "A" : i == 1 ? "B" : ModuleBase::getInputPortLabel(i);
    }

    juce::String getOutputPortLabel(int i) const override {
        const juce::String labels[] = {"Sum", "Diff", "Min", "Max", "Mult"};
        return (i >= 0 && i < 5) ? labels[i] : ModuleBase::getOutputPortLabel(i);
    }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }
    ModuleType getModuleType() const override { return ModuleType::Math; }

    // Note: getModulationTargets() is intentionally NOT overridden here — A and B are signal
    // inputs, not parameter-CV mod targets. (Convention in this repo: getModulationTargets()
    // lists only parameter-CV channels. Leaving it empty keeps AI-authored connections into
    // A/B as raw DirectCV instead of auto-wrapping them in a 0-amount Attenuverter, which
    // would silence them.)

private:
    // Clip modes: 0 = Off (transparent), 1 = Hard (jlimit to +/-1), 2 = Soft (tanh saturation).
    static float applyClip(float v, int mode) {
        switch (mode) {
        case 1:
            return juce::jlimit(-1.0f, 1.0f, v);
        case 2:
            return std::tanh(v);
        default:
            return v;
        }
    }

    juce::AudioParameterChoice* clipParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MathModule)
};
