// ParametricEQModuleTests.cpp
// Unit tests for ParametricEQModule (issue #154):
//   • identity/name/type/port metadata
//   • bypass dry pass-through and mute silencing (the two-branch contract)
//   • analytic response helpers (bandMagnitudeDb / responseDb) against known anchor points
//   • RBJ biquad coefficients agree with the analytic prototype they are derived from
//   • real audio: boosting/cutting a band actually moves that band's energy
//   • CV mapping helpers and end-to-end CV modulation of the bells
//   • edge cases: zero-length buffer, mono buffer, no prepareToPlay

#include "../Source/Modules/FX/ParametricEQModule.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <juce_dsp/juce_dsp.h>

namespace {

using BandType = ParametricEQModule::BandType;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

juce::AudioParameterFloat* paramById(ParametricEQModule& eq, const juce::String& id) {
    for (auto* p : eq.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == id)
                return dynamic_cast<juce::AudioParameterFloat*>(p);
    return nullptr;
}

void setParam(ParametricEQModule& eq, const juce::String& id, float value) {
    auto* p = paramById(eq, id);
    ASSERT_NE(p, nullptr) << "missing parameter: " << id.toStdString();
    p->setValueNotifyingHost(p->convertTo0to1(value));
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
void fillSine(juce::AudioBuffer<float>& buffer, float freq, double sampleRate, int startSample = 0,
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
float measureRMS(ParametricEQModule& eq, float freq, int numChannels = 6,
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

/** Level change in dB that `eq` applies to a `freq` tone, relative to a flat (default) EQ. */
float measureGainDb(ParametricEQModule& eq, float freq, int numChannels = 6,
                    const std::function<void(juce::AudioBuffer<float>&)>& prepBlock = {}) {
    const float rms = measureRMS(eq, freq, numChannels, prepBlock);
    return 20.0f * std::log10(std::max(rms, 1.0e-9f) / kFlatRMS);
}

// Magnitude in dB of the digital biquad that writeBiquad() produces, evaluated at `freq`.
// Uses juce::dsp::IIR::Coefficients to do the z-plane evaluation so the check is independent
// of our own maths.
float digitalMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq) {
    float raw[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    ParametricEQModule::writeBiquad(type, centreHz, gainDb, q, kSampleRate, raw);
    juce::dsp::IIR::Coefficients<float> coefs(raw[0], raw[1], raw[2], 1.0f, raw[3], raw[4]);
    const double mag = coefs.getMagnitudeForFrequency(freq, kSampleRate);
    return 20.0f * std::log10(static_cast<float>(std::max(mag, 1.0e-12)));
}

} // namespace

// ============================================================================
// Identity / metadata
// ============================================================================

TEST(ParametricEQModuleTest, NameTypeAndCategory) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getName(), "Parametric EQ");
    EXPECT_EQ(eq.getModuleType(), ModuleType::ParametricEQ);
    EXPECT_EQ(eq.getModulationCategory(), ModulationCategory::Filter);
}

TEST(ParametricEQModuleTest, ChannelLayoutIsStereoPlusFourCV) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getTotalNumInputChannels(), 6);
    EXPECT_EQ(eq.getTotalNumOutputChannels(), 2);
    EXPECT_EQ(eq.getVisibleInputPortCount(), 6);
    EXPECT_EQ(eq.getVisibleOutputPortCount(), 2);
}

TEST(ParametricEQModuleTest, PortLabels) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getInputPortLabel(0), "Left");
    EXPECT_EQ(eq.getInputPortLabel(1), "Right");
    EXPECT_EQ(eq.getInputPortLabel(2), "B1 Freq");
    EXPECT_EQ(eq.getInputPortLabel(3), "B1 Gain");
    EXPECT_EQ(eq.getInputPortLabel(4), "B2 Freq");
    EXPECT_EQ(eq.getInputPortLabel(5), "B2 Gain");
    EXPECT_EQ(eq.getOutputPortLabel(0), "Left");
    EXPECT_EQ(eq.getOutputPortLabel(1), "Right");
}

TEST(ParametricEQModuleTest, ModulationTargetsMatchCVChannels) {
    ParametricEQModule eq;
    const auto targets = eq.getModulationTargets();
    ASSERT_EQ(targets.size(), 4u);
    EXPECT_EQ(targets[0].channelIndex, 2);
    EXPECT_EQ(targets[1].channelIndex, 3);
    EXPECT_EQ(targets[2].channelIndex, 4);
    EXPECT_EQ(targets[3].channelIndex, 5);
    for (const auto& t : targets)
        EXPECT_TRUE(eq.isAutoPromotableModTarget(t.channelIndex));
}

TEST(ParametricEQModuleTest, LogicalPortRolesSplitAudioFromCV) {
    ParametricEQModule eq;
    for (int raw = 0; raw < 2; ++raw) {
        EXPECT_EQ(eq.mapInputChannel(raw).role, PortRole::Audio) << "raw " << raw;
        EXPECT_EQ(eq.mapInputChannel(raw).visibleJackIndex, raw);
    }
    for (int raw = 2; raw < 6; ++raw) {
        EXPECT_EQ(eq.mapInputChannel(raw).role, PortRole::ModCV) << "raw " << raw;
        EXPECT_EQ(eq.mapInputChannel(raw).visibleJackIndex, raw);
    }
}

TEST(ParametricEQModuleTest, BandTypeLayoutIsShelfBellBellShelf) {
    EXPECT_EQ(ParametricEQModule::bandTypeFor(0), BandType::LowShelf);
    EXPECT_EQ(ParametricEQModule::bandTypeFor(1), BandType::Peak);
    EXPECT_EQ(ParametricEQModule::bandTypeFor(2), BandType::Peak);
    EXPECT_EQ(ParametricEQModule::bandTypeFor(3), BandType::HighShelf);
}

TEST(ParametricEQModuleTest, DefaultsAreFlat) {
    ParametricEQModule eq;
    const auto bands = eq.getBandSnapshots();
    for (const auto& band : bands)
        EXPECT_FLOAT_EQ(band.gainDb, 0.0f);
    EXPECT_FLOAT_EQ(eq.getOutputGainDb(), 0.0f);
    // A flat EQ must be 0 dB everywhere, not just at the band centres.
    for (float freq : {30.0f, 200.0f, 1000.0f, 5000.0f, 18000.0f})
        EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, freq), 0.0f, 1.0e-4f) << "at " << freq << " Hz";
}

// ============================================================================
// Bypass / mute contract
// ============================================================================

TEST(ParametricEQModuleTest, BypassPassesDryAudio) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);
    setParam(eq, "band1Gain", 18.0f); // would be very audible if it were applied

    juce::AudioBuffer<float> buffer(6, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, 0.7f);
        buffer.setSample(1, i, -0.4f);
    }

    eq.setBypassed(true);
    juce::MidiBuffer midi;
    eq.processBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.7f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), -0.4f) << "Ch1 sample " << i;
    }
}

TEST(ParametricEQModuleTest, BypassClearsCVChannels) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(6, kBlockSize);
    buffer.clear();
    for (int ch = 2; ch < 6; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.5f);

    eq.setBypassed(true);
    juce::MidiBuffer midi;
    eq.processBlock(buffer, midi);

    for (int ch = 2; ch < 6; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch" << ch << " sample " << i;
}

TEST(ParametricEQModuleTest, MuteSilencesOutput) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(6, kBlockSize);
    for (int ch = 0; ch < 6; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.7f);

    eq.setMuted(true);
    juce::MidiBuffer midi;
    eq.processBlock(buffer, midi);

    for (int ch = 0; ch < 6; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

TEST(ParametricEQModuleTest, ProcessingClearsCVChannels) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(6, kBlockSize);
    buffer.clear();
    fillSine(buffer, 1000.0f, kSampleRate);
    for (int ch = 2; ch < 6; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    eq.processBlock(buffer, midi);

    for (int ch = 2; ch < 6; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch" << ch << " must not leak downstream";
}

// ============================================================================
// Analytic response — bandMagnitudeDb anchor points
// ============================================================================

TEST(ParametricEQResponse, PeakHitsItsGainAtCentreAndFlatFarAway) {
    constexpr float centre = 1000.0f;
    constexpr float gain = 12.0f;
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, gain, 1.0f, centre), gain, 0.01f);
    // Two decades out in either direction the bell has stopped doing anything.
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, gain, 1.0f, 20.0f), 0.0f, 0.2f);
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, gain, 1.0f, 20000.0f), 0.0f, 0.2f);
}

TEST(ParametricEQResponse, PeakCutIsSymmetricWithBoost) {
    constexpr float centre = 800.0f;
    for (float freq : {200.0f, 800.0f, 3000.0f}) {
        const float boost = ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, 9.0f, 2.0f, freq);
        const float cut = ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, -9.0f, 2.0f, freq);
        EXPECT_NEAR(boost, -cut, 0.01f) << "at " << freq << " Hz";
    }
}

TEST(ParametricEQResponse, HigherQNarrowsTheBell) {
    constexpr float centre = 1000.0f;
    constexpr float gain = 12.0f;
    // One octave below the centre: a wide bell still lifts a lot, a narrow one barely does.
    const float wide = ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, gain, 0.5f, 500.0f);
    const float narrow = ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, gain, 8.0f, 500.0f);
    EXPECT_GT(wide, narrow);
    EXPECT_LT(narrow, 2.0f);
    // Both must still reach the full gain at the centre.
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::Peak, centre, gain, 8.0f, centre), gain, 0.01f);
}

TEST(ParametricEQResponse, ZeroGainBandIsFlatAtEveryFrequency) {
    for (auto type : {BandType::LowShelf, BandType::Peak, BandType::HighShelf})
        for (float freq : {20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f})
            EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(type, 1000.0f, 0.0f, 0.707f, freq), 0.0f, 1.0e-4f);
}

TEST(ParametricEQResponse, LowShelfLiftsBelowAndLeavesTopAlone) {
    constexpr float centre = 150.0f;
    constexpr float gain = 9.0f;
    // Deep below the corner the shelf has reached its full gain; far above it is out of the way.
    EXPECT_NEAR(
        ParametricEQModule::bandMagnitudeDb(BandType::LowShelf, centre, gain, ParametricEQModule::kShelfQ, 5.0f), gain,
        0.3f);
    EXPECT_NEAR(
        ParametricEQModule::bandMagnitudeDb(BandType::LowShelf, centre, gain, ParametricEQModule::kShelfQ, 15000.0f),
        0.0f, 0.3f);
    // At the corner a shelf sits at half its gain.
    EXPECT_NEAR(
        ParametricEQModule::bandMagnitudeDb(BandType::LowShelf, centre, gain, ParametricEQModule::kShelfQ, centre),
        gain * 0.5f, 0.2f);
}

TEST(ParametricEQResponse, HighShelfLiftsAboveAndLeavesBottomAlone) {
    constexpr float centre = 6000.0f;
    constexpr float gain = -9.0f;
    EXPECT_NEAR(
        ParametricEQModule::bandMagnitudeDb(BandType::HighShelf, centre, gain, ParametricEQModule::kShelfQ, 200000.0f),
        gain, 0.3f);
    EXPECT_NEAR(
        ParametricEQModule::bandMagnitudeDb(BandType::HighShelf, centre, gain, ParametricEQModule::kShelfQ, 20.0f),
        0.0f, 0.3f);
    EXPECT_NEAR(
        ParametricEQModule::bandMagnitudeDb(BandType::HighShelf, centre, gain, ParametricEQModule::kShelfQ, centre),
        gain * 0.5f, 0.2f);
}

TEST(ParametricEQResponse, DegenerateInputsReturnUnityInsteadOfNaN) {
    EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(BandType::Peak, 0.0f, 12.0f, 1.0f, 1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(BandType::Peak, 1000.0f, 12.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(BandType::Peak, -100.0f, 12.0f, 1.0f, 1000.0f), 0.0f);
    // A zero Q is clamped rather than dividing by zero.
    EXPECT_TRUE(std::isfinite(ParametricEQModule::bandMagnitudeDb(BandType::Peak, 1000.0f, 12.0f, 0.0f, 1000.0f)));
}

TEST(ParametricEQResponse, ResponseDbSumsBandsAndOutputTrim) {
    std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands> bands{};
    bands[0] = {BandType::LowShelf, 120.0f, 0.0f, ParametricEQModule::kShelfQ};
    bands[1] = {BandType::Peak, 1000.0f, 6.0f, 1.0f};
    bands[2] = {BandType::Peak, 4000.0f, -6.0f, 1.0f};
    bands[3] = {BandType::HighShelf, 8000.0f, 0.0f, ParametricEQModule::kShelfQ};

    // At 1 kHz the only active band is the +6 dB bell (the -6 dB bell two octaves up
    // contributes a little, so allow a small tolerance) plus the output trim.
    const float withoutTrim = ParametricEQModule::responseDb(bands, 0.0f, 1000.0f);
    const float withTrim = ParametricEQModule::responseDb(bands, -3.0f, 1000.0f);
    EXPECT_NEAR(withTrim, withoutTrim - 3.0f, 1.0e-4f);
    EXPECT_NEAR(withoutTrim, 6.0f, 0.8f);

    // The output trim shifts the whole curve by the same amount.
    for (float freq : {50.0f, 500.0f, 12000.0f})
        EXPECT_NEAR(ParametricEQModule::responseDb(bands, 4.0f, freq),
                    ParametricEQModule::responseDb(bands, 0.0f, freq) + 4.0f, 1.0e-3f);
}

// ============================================================================
// Digital coefficients vs. the analytic prototype they came from
// ============================================================================

TEST(ParametricEQCoefficients, DigitalBiquadMatchesAnalyticPrototype) {
    struct Case {
        BandType type;
        float centre;
        float gain;
        float q;
    };
    // Well below Nyquist, where the bilinear transform's frequency warping is negligible.
    const Case cases[] = {
        {BandType::Peak, 1000.0f, 12.0f, 1.0f},
        {BandType::Peak, 1000.0f, -12.0f, 3.0f},
        {BandType::Peak, 300.0f, 6.0f, 0.7f},
        {BandType::LowShelf, 150.0f, 9.0f, ParametricEQModule::kShelfQ},
        {BandType::LowShelf, 150.0f, -9.0f, ParametricEQModule::kShelfQ},
        {BandType::HighShelf, 4000.0f, 9.0f, ParametricEQModule::kShelfQ},
        {BandType::HighShelf, 4000.0f, -9.0f, ParametricEQModule::kShelfQ},
    };

    for (const auto& c : cases) {
        for (float freq : {50.0f, 200.0f, 1000.0f, 3000.0f}) {
            const float analytic = ParametricEQModule::bandMagnitudeDb(c.type, c.centre, c.gain, c.q, freq);
            const float digital = digitalMagnitudeDb(c.type, c.centre, c.gain, c.q, freq);
            EXPECT_NEAR(analytic, digital, 0.6f) << "type " << static_cast<int>(c.type) << " centre " << c.centre
                                                 << " gain " << c.gain << " at " << freq << " Hz";
        }
    }
}

TEST(ParametricEQCoefficients, ZeroGainProducesAPassThroughBiquad) {
    for (auto type : {BandType::LowShelf, BandType::Peak, BandType::HighShelf}) {
        float raw[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        ParametricEQModule::writeBiquad(type, 1000.0f, 0.0f, 0.707f, kSampleRate, raw);
        // b == a for a unity biquad, so the transfer function is 1 at every frequency.
        EXPECT_NEAR(raw[0], 1.0f, 1.0e-5f);
        EXPECT_NEAR(raw[1], raw[3], 1.0e-5f);
        EXPECT_NEAR(raw[2], raw[4], 1.0e-5f);
    }
}

TEST(ParametricEQCoefficients, CentreAboveNyquistStaysFinite) {
    float raw[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // 40 kHz at a 48 kHz sample rate is past Nyquist — must clamp, not produce NaN.
    ParametricEQModule::writeBiquad(BandType::HighShelf, 40000.0f, 12.0f, 0.707f, kSampleRate, raw);
    for (float v : raw)
        EXPECT_TRUE(std::isfinite(v));
    // A zero/negative sample rate falls back to 44.1 kHz rather than dividing by zero.
    ParametricEQModule::writeBiquad(BandType::Peak, 1000.0f, 6.0f, 1.0f, 0.0, raw);
    for (float v : raw)
        EXPECT_TRUE(std::isfinite(v));
}

// ============================================================================
// Real audio behaviour
// ============================================================================

TEST(ParametricEQAudio, FlatEQLeavesLevelUnchanged) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureRMS(eq, 1000.0f), kFlatRMS, 0.005f);
    // Flat means flat across the band, not just at 1 kHz.
    for (float freq : {80.0f, 400.0f, 5000.0f, 12000.0f}) {
        ParametricEQModule flat;
        flat.prepareToPlay(kSampleRate, kBlockSize);
        EXPECT_NEAR(measureGainDb(flat, freq), 0.0f, 0.2f) << "at " << freq << " Hz";
    }
}

TEST(ParametricEQAudio, BoostingABellRaisesThatBandsLevel) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 1000.0f);
    setParam(eq, "band1Gain", 12.0f);
    setParam(eq, "band1Q", 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 1000.0f), 12.0f, 0.7f)
        << "a +12 dB bell at the tone's frequency should lift it ~12 dB";
}

TEST(ParametricEQAudio, CuttingABellLowersThatBandsLevel) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 1000.0f);
    setParam(eq, "band1Gain", -12.0f);
    setParam(eq, "band1Q", 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 1000.0f), -12.0f, 0.7f);
}

TEST(ParametricEQAudio, BellLeavesDistantFrequenciesAlone) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 4000.0f);
    setParam(eq, "band1Gain", 18.0f);
    setParam(eq, "band1Q", 4.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 60.0f), 0.0f, 0.5f) << "a narrow 4 kHz boost must not move a 60 Hz tone";
}

TEST(ParametricEQAudio, LowShelfBoostRaisesTheBottomEndOnly) {
    ParametricEQModule eq;
    setParam(eq, "lowFreq", 200.0f);
    setParam(eq, "lowGain", 12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_GT(measureGainDb(eq, 50.0f), 8.0f);

    ParametricEQModule eq2;
    setParam(eq2, "lowFreq", 200.0f);
    setParam(eq2, "lowGain", 12.0f);
    eq2.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq2, 8000.0f), 0.0f, 0.5f) << "a low shelf must leave the top end alone";
}

TEST(ParametricEQAudio, HighShelfCutLowersTheTopEndOnly) {
    ParametricEQModule eq;
    setParam(eq, "highFreq", 6000.0f);
    setParam(eq, "highGain", -12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_LT(measureGainDb(eq, 14000.0f), -8.0f);

    ParametricEQModule eq2;
    setParam(eq2, "highFreq", 6000.0f);
    setParam(eq2, "highGain", -12.0f);
    eq2.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq2, 80.0f), 0.0f, 0.5f) << "a high shelf must leave the bottom end alone";
}

TEST(ParametricEQAudio, MeasuredResponseTracksTheAnalyticCurve) {
    // The curve the UI draws must be the curve the DSP realises, or the display lies.
    ParametricEQModule eq;
    setParam(eq, "lowFreq", 150.0f);
    setParam(eq, "lowGain", 6.0f);
    setParam(eq, "band1Freq", 1000.0f);
    setParam(eq, "band1Gain", -9.0f);
    setParam(eq, "band1Q", 1.5f);
    setParam(eq, "band2Freq", 4000.0f);
    setParam(eq, "band2Gain", 6.0f);
    setParam(eq, "band2Q", 1.0f);
    setParam(eq, "highFreq", 8000.0f);
    setParam(eq, "highGain", -3.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    for (float freq : {60.0f, 300.0f, 1000.0f, 2000.0f, 4000.0f, 10000.0f}) {
        ParametricEQModule probe;
        setParam(probe, "lowFreq", 150.0f);
        setParam(probe, "lowGain", 6.0f);
        setParam(probe, "band1Freq", 1000.0f);
        setParam(probe, "band1Gain", -9.0f);
        setParam(probe, "band1Q", 1.5f);
        setParam(probe, "band2Freq", 4000.0f);
        setParam(probe, "band2Gain", 6.0f);
        setParam(probe, "band2Q", 1.0f);
        setParam(probe, "highFreq", 8000.0f);
        setParam(probe, "highGain", -3.0f);
        probe.prepareToPlay(kSampleRate, kBlockSize);

        const float measured = measureGainDb(probe, freq);
        const float predicted = ParametricEQModule::responseDb(probe.getBandSnapshots(), probe.getOutputGainDb(), freq);
        EXPECT_NEAR(measured, predicted, 1.0f) << "at " << freq << " Hz";
    }
}

TEST(ParametricEQAudio, OutputGainScalesTheWholeSignal) {
    ParametricEQModule eq;
    setParam(eq, "outputGain", -6.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq, 1000.0f), -6.0f, 0.3f);
}

TEST(ParametricEQAudio, ProcessedOutputStaysFinite) {
    ParametricEQModule eq;
    setParam(eq, "lowGain", 24.0f);
    setParam(eq, "band1Gain", 24.0f);
    setParam(eq, "band2Gain", -24.0f);
    setParam(eq, "highGain", 24.0f);
    setParam(eq, "outputGain", 24.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(6, kBlockSize);
    for (int b = 0; b < 20; ++b) {
        buffer.clear();
        fillSine(buffer, 440.0f, kSampleRate, b * kBlockSize, 0.9f);
        eq.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_TRUE(std::isfinite(buffer.getSample(ch, i))) << "block " << b << " ch" << ch << " sample " << i;
    }
}

TEST(ParametricEQAudio, ChangingBandsUpdatesTheDisplaySnapshot) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 2000.0f);
    setParam(eq, "band1Gain", 8.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(eq, 1000.0f);

    const auto bands = eq.getBandSnapshots();
    EXPECT_NEAR(bands[1].freqHz, 2000.0f, 1.0f);
    EXPECT_NEAR(bands[1].gainDb, 8.0f, 0.1f);
    EXPECT_EQ(bands[1].type, BandType::Peak);
    // Shelves are hard-wired to the maximally-flat slope.
    EXPECT_FLOAT_EQ(bands[0].q, ParametricEQModule::kShelfQ);
    EXPECT_FLOAT_EQ(bands[3].q, ParametricEQModule::kShelfQ);
}

TEST(ParametricEQAudio, StereoChannelsAreFilteredIdentically) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 1000.0f);
    setParam(eq, "band1Gain", 12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(6, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        buffer.clear();
        fillSine(buffer, 1000.0f, kSampleRate, b * kBlockSize);
        eq.processBlock(buffer, midi);
    }
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), buffer.getSample(1, i)) << "sample " << i;
}

// ============================================================================
// CV modulation
// ============================================================================

TEST(ParametricEQCV, FreqCVSweepsTheFullRangeExponentially) {
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 0.0f, 40.0f, 8000.0f), 500.0f, 0.01f);
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 1.0f, 40.0f, 8000.0f), 8000.0f, 1.0f);
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, -1.0f, 40.0f, 8000.0f), 40.0f, 0.1f);
    // Half-way up is the geometric mean of the base and the top — an exponential sweep.
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 0.5f, 40.0f, 8000.0f), std::sqrt(500.0f * 8000.0f), 1.0f);
    // Out-of-range CV is clamped, not extrapolated.
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 4.0f, 40.0f, 8000.0f), 8000.0f, 1.0f);
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, -4.0f, 40.0f, 8000.0f), 40.0f, 0.1f);
}

TEST(ParametricEQCV, GainCVAddsOntoTheKnobAndClamps) {
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(0.0f, 0.5f), 12.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(0.0f, -1.0f), -ParametricEQModule::kMaxGainDb);
    // Knob at +12 dB plus full CV saturates at the +24 dB ceiling.
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(12.0f, 1.0f), ParametricEQModule::kMaxGainDb);
}

TEST(ParametricEQCV, GainCVOnChannel3ModulatesBandOne) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 1000.0f);
    setParam(eq, "band1Gain", 0.0f);
    setParam(eq, "band1Q", 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // +50% of the +/-24 dB range == +12 dB on band 1.
    const float gainDb = measureGainDb(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(3, i, 0.5f);
    });

    EXPECT_NEAR(eq.getBandSnapshots()[1].gainDb, 12.0f, 0.2f);
    EXPECT_NEAR(gainDb, 12.0f, 0.7f) << "CV gain boost should show up in the output level";
}

TEST(ParametricEQCV, SilentCVJackLeavesTheBandAtItsKnobValue) {
    ParametricEQModule eq;
    setParam(eq, "band1Gain", 5.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(eq, 1000.0f); // CV channels stay silent, i.e. nothing patched in

    EXPECT_NEAR(eq.getBandSnapshots()[1].gainDb, 5.0f, 0.1f);
}

TEST(ParametricEQCV, NearSilentCVIsGatedToZero) {
    // An unconnected jack can still carry a tiny amount of numerical dirt; it must not nudge the
    // band at all. The gate threshold is a mean-square of 1e-6, i.e. ~1e-3 amplitude.
    // Compared against the no-CV case rather than literal 0.0, because a JUCE parameter
    // round-tripped through NormalisableRange::convertTo0to1 lands a few ULPs off its nominal
    // value — that offset is the baseline here, not part of what is being tested.
    ParametricEQModule baseline;
    setParam(baseline, "band1Gain", 0.0f);
    baseline.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(baseline, 1000.0f);
    const float baselineGain = baseline.getBandSnapshots()[1].gainDb;

    ParametricEQModule eq;
    setParam(eq, "band1Gain", 0.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(3, i, 1.0e-5f);
    });

    EXPECT_FLOAT_EQ(eq.getBandSnapshots()[1].gainDb, baselineGain);
}

TEST(ParametricEQCV, CVAboveTheGateThresholdIsApplied) {
    // The complement of the test above: CV loud enough to clear the gate must get through, so
    // the gate can't be silently swallowing real modulation.
    ParametricEQModule eq;
    setParam(eq, "band1Gain", 0.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(3, i, 0.25f);
    });

    EXPECT_NEAR(eq.getBandSnapshots()[1].gainDb, 6.0f, 0.2f);
}

TEST(ParametricEQCV, FreqCVOnChannel2MovesBandOne) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 500.0f);
    setParam(eq, "band1Gain", 12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // Sweep the bell to the top of its range (40 Hz - 8 kHz).
    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(2, i, 1.0f);
    });

    EXPECT_GT(eq.getBandSnapshots()[1].freqHz, 500.0f);
    EXPECT_NEAR(eq.getBandSnapshots()[1].freqHz, 8000.0f, 200.0f);
}

TEST(ParametricEQCV, NegativeFreqCVMovesBandDown) {
    ParametricEQModule eq;
    setParam(eq, "band2Freq", 3000.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(4, i, -1.0f);
    });

    // Band 2's range is 200 Hz - 16 kHz, so full negative CV lands on 200 Hz.
    EXPECT_NEAR(eq.getBandSnapshots()[2].freqHz, 200.0f, 20.0f);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ParametricEQEdgeCases, ZeroLengthBufferDoesNotCrash) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);
    juce::AudioBuffer<float> buffer(6, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_FATAL_FAILURE(eq.processBlock(buffer, midi));
}

TEST(ParametricEQEdgeCases, ZeroChannelBufferDoesNotCrash) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);
    juce::AudioBuffer<float> buffer(0, kBlockSize);
    juce::MidiBuffer midi;
    EXPECT_NO_FATAL_FAILURE(eq.processBlock(buffer, midi));
}

TEST(ParametricEQEdgeCases, MonoBufferIsStillFiltered) {
    ParametricEQModule eq;
    setParam(eq, "band1Freq", 1000.0f);
    setParam(eq, "band1Gain", 12.0f);
    setParam(eq, "band1Q", 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // A single-channel buffer must still get the boost rather than passing through untouched.
    EXPECT_NEAR(measureGainDb(eq, 1000.0f, /*numChannels*/ 1), 12.0f, 0.7f);
}

TEST(ParametricEQEdgeCases, ProcessWithoutPrepareDoesNotCrash) {
    ParametricEQModule eq;
    juce::AudioBuffer<float> buffer(6, kBlockSize);
    buffer.clear();
    fillSine(buffer, 1000.0f, 44100.0);
    juce::MidiBuffer midi;
    EXPECT_NO_FATAL_FAILURE(eq.processBlock(buffer, midi));
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_TRUE(std::isfinite(buffer.getSample(0, i)));
}

TEST(ParametricEQEdgeCases, RepreparingResetsCleanly) {
    ParametricEQModule eq;
    eq.prepareToPlay(44100.0, 256);
    measureRMS(eq, 1000.0f);
    eq.prepareToPlay(96000.0, 1024);
    EXPECT_DOUBLE_EQ(eq.getLastSampleRate(), 96000.0);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(6, 1024);
    buffer.clear();
    fillSine(buffer, 1000.0f, 96000.0);
    eq.processBlock(buffer, midi);
    for (int i = 0; i < 1024; ++i)
        EXPECT_TRUE(std::isfinite(buffer.getSample(0, i)));
}

TEST(ParametricEQEdgeCases, BandsAreIndependentOfSampleRate) {
    // The same +12 dB bell at 1 kHz must measure the same at 44.1 kHz and 96 kHz.
    for (double sr : {44100.0, 96000.0}) {
        ParametricEQModule eq;
        setParam(eq, "band1Freq", 1000.0f);
        setParam(eq, "band1Gain", 12.0f);
        setParam(eq, "band1Q", 1.0f);
        eq.prepareToPlay(sr, 512);

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer(6, 512);
        double sumSquares = 0.0;
        int measured = 0;
        for (int b = 0; b < 24; ++b) {
            buffer.clear();
            fillSine(buffer, 1000.0f, sr, b * 512);
            eq.processBlock(buffer, midi);
            if (b >= 12) {
                const auto* out = buffer.getReadPointer(0);
                for (int i = 0; i < 512; ++i)
                    sumSquares += static_cast<double>(out[i]) * out[i];
                measured += 512;
            }
        }
        const float rms = static_cast<float>(std::sqrt(sumSquares / measured));
        EXPECT_NEAR(20.0f * std::log10(rms / kFlatRMS), 12.0f, 0.8f) << "at " << sr << " Hz sample rate";
    }
}

TEST(ParametricEQEdgeCases, StateRoundTripPreservesBandSettings) {
    ParametricEQModule saved;
    setParam(saved, "band1Freq", 2500.0f);
    setParam(saved, "band1Gain", -7.5f);
    setParam(saved, "band2Q", 4.0f);
    setParam(saved, "outputGain", -3.0f);
    saved.setBypassed(true);

    juce::MemoryBlock state;
    saved.getStateInformation(state);

    ParametricEQModule restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());

    EXPECT_NEAR(paramById(restored, "band1Freq")->get(), 2500.0f, 1.0f);
    EXPECT_NEAR(paramById(restored, "band1Gain")->get(), -7.5f, 0.05f);
    EXPECT_NEAR(paramById(restored, "band2Q")->get(), 4.0f, 0.02f);
    EXPECT_NEAR(paramById(restored, "outputGain")->get(), -3.0f, 0.05f);
    EXPECT_TRUE(restored.isBypassed());
}
