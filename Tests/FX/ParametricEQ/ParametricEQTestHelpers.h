// ParametricEQTestHelpers.h
// Shared fixtures for the Parametric EQ test units: parameter lookup, band setup, sine
// measurement and the analytic/digital response helpers.

#pragma once

#include "Modules/FX/ParametricEQModule.h"
#include <array>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <juce_dsp/juce_dsp.h>

namespace eqtest {

using BandType = ParametricEQModule::BandType;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumBands = ParametricEQModule::kNumBands;

inline juce::AudioParameterFloat* floatParamById(ParametricEQModule& eq, const juce::String& id) {
    for (auto* p : eq.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == id)
                return dynamic_cast<juce::AudioParameterFloat*>(p);
    return nullptr;
}

inline juce::AudioParameterBool* boolParamById(ParametricEQModule& eq, const juce::String& id) {
    for (auto* p : eq.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == id)
                return dynamic_cast<juce::AudioParameterBool*>(p);
    return nullptr;
}

inline juce::String bandId(int band, const juce::String& suffix) { return "band" + juce::String(band + 1) + suffix; }

inline void setFloatParam(ParametricEQModule& eq, const juce::String& id, float value) {
    auto* p = floatParamById(eq, id);
    ASSERT_NE(p, nullptr) << "missing parameter: " << id.toStdString();
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

/** Turns a band on and parks it at the given shape, using the module's own setters. */
inline void enableBand(ParametricEQModule& eq, int band, float freqHz, float gainDb,
                       float q = ParametricEQModule::kDefaultQ) {
    eq.setBandFreq(band, freqHz);
    eq.setBandGain(band, gainDb);
    eq.setBandQ(band, q);
    eq.setBandEnabled(band, true);
}

constexpr float kSineAmplitude = 0.5f;
// RMS of a kSineAmplitude sine: 0.5 / sqrt(2).
constexpr float kFlatRMS = 0.35355339f;

// Warm-up blocks (filter transient + 20 ms parameter smoothing) then the measurement window.
// 8 x 512 = 4096 samples = 85 ms at 48 kHz — enough whole cycles that even a 50 Hz tone's RMS
// is within ~0.1 dB of the ideal, so phase shift through the filter can't skew the comparison.
constexpr int kWarmupBlocks = 12;
constexpr int kMeasureBlocks = 8;

// Fills `buffer` (channels 0..min(2,numCh)-1) with a sine at `freq`, continuing the phase from
// absolute sample index `startSample` so successive blocks form one unbroken tone.
inline void fillSine(juce::AudioBuffer<float>& buffer, float freq, double sampleRate, int startSample = 0,
                     float amplitude = kSineAmplitude) {
    const int numSamples = buffer.getNumSamples();
    const int audioCh = std::min(2, buffer.getNumChannels());
    for (int i = 0; i < numSamples; ++i) {
        const float s = amplitude * std::sin(juce::MathConstants<float>::twoPi * freq *
                                             static_cast<float>(startSample + i) / static_cast<float>(sampleRate));
        for (int ch = 0; ch < audioCh; ++ch)
            buffer.setSample(ch, i, s);
    }
}

/** Drives `eq` with a continuous sine at `freq` and returns the output RMS of channel 0 over the
 *  measurement window. `prepBlock`, if given, runs after the sine is written and before
 *  processBlock, so a test can stamp CV values into channels 2-5.
 */
inline float measureRMS(ParametricEQModule& eq, float freq, int numChannels = 6,
                        const std::function<void(juce::AudioBuffer<float>&)>& prepBlock = {}) {
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(numChannels, kBlockSize);
    double sumSquares = 0.0;
    int measuredSamples = 0;

    for (int b = 0; b < kWarmupBlocks + kMeasureBlocks; ++b) {
        buffer.clear();
        fillSine(buffer, freq, kSampleRate, b * kBlockSize);
        if (prepBlock)
            prepBlock(buffer);
        eq.processBlock(buffer, midi);

        if (b >= kWarmupBlocks && buffer.getNumChannels() > 0) {
            const auto* out = buffer.getReadPointer(0);
            for (int i = 0; i < kBlockSize; ++i)
                sumSquares += static_cast<double>(out[i]) * out[i];
            measuredSamples += kBlockSize;
        }
    }

    if (measuredSamples == 0)
        return 0.0f;
    return static_cast<float>(std::sqrt(sumSquares / measuredSamples));
}

/** Level change in dB that `eq` applies to a `freq` tone, relative to a flat (all-off) EQ. */
inline float measureGainDb(ParametricEQModule& eq, float freq, int numChannels = 6,
                           const std::function<void(juce::AudioBuffer<float>&)>& prepBlock = {}) {
    const float rms = measureRMS(eq, freq, numChannels, prepBlock);
    return 20.0f * std::log10(std::max(rms, 1.0e-9f) / kFlatRMS);
}

// Magnitude in dB of the digital biquad that writeBiquad() produces, evaluated at `freq`.
// Uses juce::dsp::IIR::Coefficients to do the z-plane evaluation so the check is independent
// of our own maths.
inline float digitalMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq) {
    float raw[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    ParametricEQModule::writeBiquad(type, centreHz, gainDb, q, kSampleRate, raw);
    juce::dsp::IIR::Coefficients<float> coefs(raw[0], raw[1], raw[2], 1.0f, raw[3], raw[4]);
    const double mag = coefs.getMagnitudeForFrequency(freq, kSampleRate);
    return 20.0f * std::log10(static_cast<float>(std::max(mag, 1.0e-12)));
}

/** An all-off snapshot with one band enabled at the given shape — for responseDb tests. */
inline std::array<ParametricEQModule::BandSnapshot, kNumBands> oneBandSnapshot(int band, float freqHz, float gainDb,
                                                                               float q) {
    std::array<ParametricEQModule::BandSnapshot, kNumBands> bands{};
    for (int b = 0; b < kNumBands; ++b) {
        bands[(size_t)b].type = ParametricEQModule::bandTypeFor(b);
        bands[(size_t)b].enabled = false;
        bands[(size_t)b].freqHz = ParametricEQModule::defaultFreqFor(b);
    }
    bands[(size_t)band].enabled = true;
    bands[(size_t)band].freqHz = freqHz;
    bands[(size_t)band].gainDb = gainDb;
    bands[(size_t)band].q = q;
    return bands;
}

} // namespace eqtest
