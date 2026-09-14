#pragma once

// Shared render/measurement/param-setting helpers for the StereoVoiceModule test suite
// (Tests/Modules/StereoVoiceModule/StereoVoiceModule*Tests.cpp).

#include "Modules/OscillatorModule.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

float rmsOf(const juce::AudioBuffer<float>& buffer, int channel) {
    if (channel >= buffer.getNumChannels())
        return 0.0f;
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples();
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / (float)n);
}

float maxAbsDiff(const juce::AudioBuffer<float>& a, int chA, const juce::AudioBuffer<float>& b, int chB) {
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    float worst = 0.0f;
    for (int i = 0; i < n; ++i)
        worst = std::max(worst, std::abs(a.getReadPointer(chA)[i] - b.getReadPointer(chB)[i]));
    return worst;
}

void setFloatParam(juce::AudioProcessor& module, const juce::String& paramID, float value) {
    for (auto* param : module.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (f->paramID == paramID) {
                *f = value;
                return;
            }
    FAIL() << "no float parameter \"" << paramID << "\"";
}

void setBoolParam(juce::AudioProcessor& module, const juce::String& paramID, bool value) {
    for (auto* param : module.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param))
            if (b->paramID == paramID) {
                *b = value;
                return;
            }
    FAIL() << "no bool parameter \"" << paramID << "\"";
}

/** Renders `blocks` blocks of an Oscillator driven by a MIDI note, returning the final block. */
juce::AudioBuffer<float> renderOscillator(OscillatorModule& osc, int blocks = 2, int panCVChannel = -1,
                                          float panCVValue = 0.0f) {
    juce::AudioBuffer<float> buffer(OscillatorModule::kNumOutputs, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

    for (int b = 0; b < blocks; ++b) {
        buffer.clear();
        if (panCVChannel >= 0)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(panCVChannel, i, panCVValue);
        osc.processBlock(buffer, midi);
        midi.clear();
    }
    return buffer;
}

/** Fills `channel` with a deterministic mid-band tone so filtering has something to bite on. */
void fillTone(juce::AudioBuffer<float>& buffer, int channel, float frequency, float amplitude = 0.5f,
              float phase = 0.0f) {
    float* data = buffer.getWritePointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] =
            amplitude * std::sin(phase + juce::MathConstants<float>::twoPi * frequency * (float)i / (float)kSampleRate);
}

} // namespace
