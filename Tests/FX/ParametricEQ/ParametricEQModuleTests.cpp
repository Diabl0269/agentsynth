// ParametricEQModuleTests.cpp
// Unit tests for ParametricEQModule (issue #154):
//   • identity/name/type/port metadata, and that all four bands start DISABLED
//   • bypass dry pass-through and mute silencing (the two-branch contract)
//   • enable/disable semantics — a disabled band is a straight wire, in both maths and audio
//   • band-slot selection for a new point (findBandForNewPoint)
//   • setters clamp to range and round-trip
//   • analytic response helpers (bandMagnitudeDb / responseDb) against known anchor points
//   • RBJ biquad coefficients agree with the analytic prototype they are derived from
//   • real audio: enabling/boosting/cutting a band actually moves that band's energy
//   • edge cases: zero-length buffer, mono buffer, no prepareToPlay, state round-trip
// CV modulation lives in ParametricEQCVTests.cpp; shared helpers in ParametricEQTestHelpers.h.

#include "ParametricEQTestHelpers.h"
#include <gtest/gtest.h>

using namespace eqtest;

// ============================================================================
// Identity / metadata
// ============================================================================

TEST(ParametricEQModuleTest, NameTypeAndCategory) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getName(), "Parametric EQ");
    EXPECT_EQ(eq.getModuleType(), ModuleType::ParametricEQ);
    EXPECT_EQ(eq.getModulationCategory(), ModulationCategory::Filter);
}

TEST(ParametricEQModuleTest, ChannelLayoutIsStereoPlusThirteenCV) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getTotalNumInputChannels(), 15);
    EXPECT_EQ(eq.getTotalNumOutputChannels(), 2);
    EXPECT_EQ(eq.getVisibleInputPortCount(), 14);
    EXPECT_EQ(eq.getVisibleOutputPortCount(), 1);
}

TEST(ParametricEQModuleTest, PortLabelsNameAllFifteenInputs) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getInputPortLabel(0), "Audio");
    EXPECT_EQ(eq.getInputPortLabel(1), "B2 Freq");
    EXPECT_EQ(eq.getInputPortLabel(2), "B2 Gain");
    EXPECT_EQ(eq.getInputPortLabel(3), "B3 Freq");
    EXPECT_EQ(eq.getInputPortLabel(4), "B3 Gain");
    EXPECT_EQ(eq.getInputPortLabel(5), "B1 Freq");
    EXPECT_EQ(eq.getInputPortLabel(6), "B1 Gain");
    EXPECT_EQ(eq.getInputPortLabel(7), "B4 Freq");
    EXPECT_EQ(eq.getInputPortLabel(8), "B4 Gain");
    EXPECT_EQ(eq.getInputPortLabel(9), "B1 Q");
    EXPECT_EQ(eq.getInputPortLabel(10), "B2 Q");
    EXPECT_EQ(eq.getInputPortLabel(11), "B3 Q");
    EXPECT_EQ(eq.getInputPortLabel(12), "B4 Q");
    EXPECT_EQ(eq.getInputPortLabel(13), "Output");
    EXPECT_EQ(eq.getOutputPortLabel(0), "Audio");
}

TEST(ParametricEQModuleTest, ModulationTargetsCoverEveryContinuousParameter) {
    ParametricEQModule eq;
    const auto targets = eq.getModulationTargets();
    ASSERT_EQ(targets.size(), 13u);
    for (int i = 0; i < 13; ++i) {
        EXPECT_EQ(targets[(size_t)i].channelIndex, 2 + i);
        EXPECT_TRUE(eq.isAutoPromotableModTarget(targets[(size_t)i].channelIndex));
        EXPECT_TRUE(targets[(size_t)i].paramId.isNotEmpty()) << "target " << i << " has no paramId";
        EXPECT_NE(eq.parameterForModTarget(targets[(size_t)i]), nullptr) << "target " << i << " does not bind";
    }
}

TEST(ParametricEQModuleTest, LogicalPortRolesSplitAudioFromCV) {
    ParametricEQModule eq;
    // Default Dual I/O off: raw 0/1 collapse onto visible Audio jack 0; CV jacks shift down.
    auto in0 = eq.mapInputChannel(0);
    EXPECT_EQ(in0.role, PortRole::Audio);
    EXPECT_EQ(in0.visibleJackIndex, 0);
    EXPECT_TRUE(in0.isPolyGroupHead);
    EXPECT_EQ(in0.polyVoiceSpan, 2);

    auto in1 = eq.mapInputChannel(1);
    EXPECT_EQ(in1.role, PortRole::Audio);
    EXPECT_EQ(in1.visibleJackIndex, 0);
    EXPECT_FALSE(in1.isPolyGroupHead);

    for (int raw = 2; raw < 15; ++raw) {
        EXPECT_EQ(eq.mapInputChannel(raw).role, PortRole::ModCV) << "raw " << raw;
        EXPECT_EQ(eq.mapInputChannel(raw).visibleJackIndex, raw - 1) << "raw " << raw;
    }
}

TEST(ParametricEQModuleTest, BandTypeLayoutIsShelfBellBellShelf) {
    EXPECT_EQ(ParametricEQModule::bandTypeFor(0), BandType::LowShelf);
    EXPECT_EQ(ParametricEQModule::bandTypeFor(1), BandType::Peak);
    EXPECT_EQ(ParametricEQModule::bandTypeFor(2), BandType::Peak);
    EXPECT_EQ(ParametricEQModule::bandTypeFor(3), BandType::HighShelf);
}

TEST(ParametricEQModuleTest, RowLabelsNameTheSlotAndItsType) {
    EXPECT_EQ(ParametricEQModule::rowLabelFor(0), "1 Low Shelf");
    EXPECT_EQ(ParametricEQModule::rowLabelFor(1), "2 Peak");
    EXPECT_EQ(ParametricEQModule::rowLabelFor(2), "3 Peak");
    EXPECT_EQ(ParametricEQModule::rowLabelFor(3), "4 High Shelf");
}

TEST(ParametricEQModuleTest, DefaultFrequenciesAscendAcrossSlots) {
    for (int b = 1; b < kNumBands; ++b)
        EXPECT_GT(ParametricEQModule::defaultFreqFor(b), ParametricEQModule::defaultFreqFor(b - 1)) << "slot " << b;
    // Out-of-range indices clamp rather than reading past the array.
    EXPECT_FLOAT_EQ(ParametricEQModule::defaultFreqFor(-5), ParametricEQModule::defaultFreqFor(0));
    EXPECT_FLOAT_EQ(ParametricEQModule::defaultFreqFor(99), ParametricEQModule::defaultFreqFor(kNumBands - 1));
}

// ============================================================================
// Enable / disable semantics — the curve starts empty
// ============================================================================

TEST(ParametricEQModuleTest, AllBandsStartDisabled) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getEnabledBandCount(), 0);
    for (int b = 0; b < kNumBands; ++b) {
        EXPECT_FALSE(eq.isBandEnabled(b)) << "band " << b;
        EXPECT_FALSE(eq.getBandSnapshots()[(size_t)b].enabled) << "band " << b;
    }
    // With nothing enabled the response must be dead flat at every frequency.
    const auto bands = eq.getBandSnapshots();
    for (float freq : {30.0f, 200.0f, 1000.0f, 5000.0f, 18000.0f})
        EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, freq), 0.0f, 1.0e-4f) << "at " << freq << " Hz";
}

TEST(ParametricEQModuleTest, EnableRoundTripsAndCountsBands) {
    ParametricEQModule eq;
    eq.setBandEnabled(1, true);
    EXPECT_TRUE(eq.isBandEnabled(1));
    EXPECT_EQ(eq.getEnabledBandCount(), 1);

    eq.setBandEnabled(3, true);
    EXPECT_EQ(eq.getEnabledBandCount(), 2);

    eq.setBandEnabled(1, false);
    EXPECT_FALSE(eq.isBandEnabled(1));
    EXPECT_EQ(eq.getEnabledBandCount(), 1);

    // Out-of-range indices are ignored rather than corrupting state.
    eq.setBandEnabled(-1, true);
    eq.setBandEnabled(kNumBands, true);
    EXPECT_EQ(eq.getEnabledBandCount(), 1);
    EXPECT_FALSE(eq.isBandEnabled(-1));
    EXPECT_FALSE(eq.isBandEnabled(kNumBands));
}

TEST(ParametricEQModuleTest, DisabledBandWithGainStillContributesNothing) {
    // Setting a big gain on a disabled band must not colour the response — otherwise turning a
    // band off would not really bypass it.
    ParametricEQModule eq;
    eq.setBandFreq(1, 1000.0f);
    eq.setBandGain(1, 24.0f);
    EXPECT_FALSE(eq.isBandEnabled(1));

    const auto bands = eq.getBandSnapshots();
    EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, 1000.0f), 0.0f, 1.0e-4f);
}

TEST(ParametricEQModuleTest, SettersClampToRange) {
    ParametricEQModule eq;
    eq.setBandFreq(0, 5.0f);
    EXPECT_NEAR(floatParamById(eq, bandId(0, "Freq"))->get(), ParametricEQModule::kMinFreq, 0.5f);
    eq.setBandFreq(0, 999999.0f);
    EXPECT_NEAR(floatParamById(eq, bandId(0, "Freq"))->get(), ParametricEQModule::kMaxFreq, 1.0f);

    eq.setBandGain(0, 999.0f);
    EXPECT_NEAR(floatParamById(eq, bandId(0, "Gain"))->get(), ParametricEQModule::kMaxGainDb, 0.01f);
    eq.setBandGain(0, -999.0f);
    EXPECT_NEAR(floatParamById(eq, bandId(0, "Gain"))->get(), -ParametricEQModule::kMaxGainDb, 0.01f);

    eq.setBandQ(0, 0.0f);
    EXPECT_NEAR(floatParamById(eq, bandId(0, "Q"))->get(), ParametricEQModule::kMinQ, 0.01f);
    eq.setBandQ(0, 500.0f);
    EXPECT_NEAR(floatParamById(eq, bandId(0, "Q"))->get(), ParametricEQModule::kMaxQ, 0.01f);
}

// ============================================================================
// Slot selection for a new point
// ============================================================================

TEST(ParametricEQPointPlacement, PicksTheSlotClosestToTheClickedFrequency) {
    ParametricEQModule eq;
    // Home frequencies are 100 / 500 / 3000 / 8000 Hz.
    EXPECT_EQ(eq.findBandForNewPoint(60.0f), 0);
    EXPECT_EQ(eq.findBandForNewPoint(450.0f), 1);
    EXPECT_EQ(eq.findBandForNewPoint(2500.0f), 2);
    EXPECT_EQ(eq.findBandForNewPoint(14000.0f), 3);
}

TEST(ParametricEQPointPlacement, SkipsSlotsAlreadyInUse) {
    ParametricEQModule eq;
    ASSERT_EQ(eq.findBandForNewPoint(60.0f), 0);
    eq.setBandEnabled(0, true);
    // The low shelf is taken, so a second low click falls to the next-nearest slot.
    EXPECT_EQ(eq.findBandForNewPoint(60.0f), 1);

    eq.setBandEnabled(1, true);
    EXPECT_EQ(eq.findBandForNewPoint(60.0f), 2);
}

TEST(ParametricEQPointPlacement, ReturnsMinusOneWhenAllSlotsAreUsed) {
    ParametricEQModule eq;
    for (int b = 0; b < kNumBands; ++b)
        eq.setBandEnabled(b, true);
    EXPECT_EQ(eq.findBandForNewPoint(1000.0f), -1);
}

TEST(ParametricEQPointPlacement, OutOfRangeFrequenciesStillResolveToASlot) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.findBandForNewPoint(0.0f), 0);
    EXPECT_EQ(eq.findBandForNewPoint(500000.0f), 3);
}

// ============================================================================
// Bypass / mute contract
// ============================================================================

TEST(ParametricEQModuleTest, BypassPassesDryAudio) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);
    enableBand(eq, 1, 1000.0f, 18.0f); // would be very audible if it were applied

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
    constexpr float q = ParametricEQModule::kDefaultQ;
    // Deep below the corner the shelf has reached its full gain; far above it is out of the way.
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::LowShelf, centre, gain, q, 5.0f), gain, 0.3f);
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::LowShelf, centre, gain, q, 15000.0f), 0.0f, 0.3f);
    // At the corner a shelf sits at half its gain.
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::LowShelf, centre, gain, q, centre), gain * 0.5f, 0.2f);
}

TEST(ParametricEQResponse, HighShelfLiftsAboveAndLeavesBottomAlone) {
    constexpr float centre = 6000.0f;
    constexpr float gain = -9.0f;
    constexpr float q = ParametricEQModule::kDefaultQ;
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::HighShelf, centre, gain, q, 200000.0f), gain, 0.3f);
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::HighShelf, centre, gain, q, 20.0f), 0.0f, 0.3f);
    EXPECT_NEAR(ParametricEQModule::bandMagnitudeDb(BandType::HighShelf, centre, gain, q, centre), gain * 0.5f, 0.2f);
}

TEST(ParametricEQResponse, DegenerateInputsReturnUnityInsteadOfNaN) {
    EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(BandType::Peak, 0.0f, 12.0f, 1.0f, 1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(BandType::Peak, 1000.0f, 12.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::bandMagnitudeDb(BandType::Peak, -100.0f, 12.0f, 1.0f, 1000.0f), 0.0f);
    // A zero Q is clamped rather than dividing by zero.
    EXPECT_TRUE(std::isfinite(ParametricEQModule::bandMagnitudeDb(BandType::Peak, 1000.0f, 12.0f, 0.0f, 1000.0f)));
}

TEST(ParametricEQResponse, ResponseDbSkipsDisabledBandsAndAddsTheOutputTrim) {
    auto bands = oneBandSnapshot(1, 1000.0f, 6.0f, 1.0f);
    EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, 1000.0f), 6.0f, 0.01f);

    // Enabling a second, cutting bell two octaves up pulls 1 kHz down a little.
    bands[2].enabled = true;
    bands[2].freqHz = 4000.0f;
    bands[2].gainDb = -6.0f;
    bands[2].q = 1.0f;
    const float withBoth = ParametricEQModule::responseDb(bands, 0.0f, 1000.0f);
    EXPECT_LT(withBoth, 6.0f);
    EXPECT_NEAR(withBoth, 6.0f, 0.8f);

    // Disabling it again restores the single-band answer exactly.
    bands[2].enabled = false;
    EXPECT_NEAR(ParametricEQModule::responseDb(bands, 0.0f, 1000.0f), 6.0f, 0.01f);

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
        {BandType::LowShelf, 150.0f, 9.0f, ParametricEQModule::kDefaultQ},
        {BandType::LowShelf, 150.0f, -9.0f, ParametricEQModule::kDefaultQ},
        {BandType::HighShelf, 4000.0f, 9.0f, ParametricEQModule::kDefaultQ},
        {BandType::HighShelf, 4000.0f, -9.0f, ParametricEQModule::kDefaultQ},
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

TEST(ParametricEQAudio, AllBandsOffIsAStraightWire) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureRMS(eq, 1000.0f), kFlatRMS, 0.005f);

    for (float freq : {80.0f, 400.0f, 5000.0f, 12000.0f}) {
        ParametricEQModule flat;
        flat.prepareToPlay(kSampleRate, kBlockSize);
        EXPECT_NEAR(measureGainDb(flat, freq), 0.0f, 0.2f) << "at " << freq << " Hz";
    }
}

TEST(ParametricEQAudio, DisabledBandDoesNotTouchAudio) {
    // The band is fully configured for a big boost but left off — audio must be untouched.
    ParametricEQModule eq;
    eq.setBandFreq(1, 1000.0f);
    eq.setBandGain(1, 24.0f);
    eq.setBandQ(1, 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 1000.0f), 0.0f, 0.1f);
}

TEST(ParametricEQAudio, EnablingABandAppliesItAndDisablingRemovesIt) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 12.0f, 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq, 1000.0f), 12.0f, 0.7f);

    eq.setBandEnabled(1, false);
    EXPECT_NEAR(measureGainDb(eq, 1000.0f), 0.0f, 0.1f) << "turning the band off must restore unity gain";
}

TEST(ParametricEQAudio, BoostingABellRaisesThatBandsLevel) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 12.0f, 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 1000.0f), 12.0f, 0.7f)
        << "a +12 dB bell at the tone's frequency should lift it ~12 dB";
}

TEST(ParametricEQAudio, CuttingABellLowersThatBandsLevel) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, -12.0f, 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 1000.0f), -12.0f, 0.7f);
}

TEST(ParametricEQAudio, BellLeavesDistantFrequenciesAlone) {
    ParametricEQModule eq;
    enableBand(eq, 1, 4000.0f, 18.0f, 4.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_NEAR(measureGainDb(eq, 60.0f), 0.0f, 0.5f) << "a narrow 4 kHz boost must not move a 60 Hz tone";
}

TEST(ParametricEQAudio, LowShelfBoostRaisesTheBottomEndOnly) {
    ParametricEQModule eq;
    enableBand(eq, 0, 200.0f, 12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_GT(measureGainDb(eq, 50.0f), 8.0f);

    ParametricEQModule eq2;
    enableBand(eq2, 0, 200.0f, 12.0f);
    eq2.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq2, 8000.0f), 0.0f, 0.5f) << "a low shelf must leave the top end alone";
}

TEST(ParametricEQAudio, HighShelfCutLowersTheTopEndOnly) {
    ParametricEQModule eq;
    enableBand(eq, 3, 6000.0f, -12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_LT(measureGainDb(eq, 14000.0f), -8.0f);

    ParametricEQModule eq2;
    enableBand(eq2, 3, 6000.0f, -12.0f);
    eq2.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq2, 80.0f), 0.0f, 0.5f) << "a high shelf must leave the bottom end alone";
}

TEST(ParametricEQAudio, MeasuredResponseTracksTheAnalyticCurve) {
    // The curve the UI draws must be the curve the DSP realises, or the display lies.
    const auto configure = [](ParametricEQModule& eq) {
        enableBand(eq, 0, 150.0f, 6.0f);
        enableBand(eq, 1, 1000.0f, -9.0f, 1.5f);
        enableBand(eq, 2, 4000.0f, 6.0f, 1.0f);
        enableBand(eq, 3, 8000.0f, -3.0f);
    };

    for (float freq : {60.0f, 300.0f, 1000.0f, 2000.0f, 4000.0f, 10000.0f}) {
        ParametricEQModule probe;
        configure(probe);
        probe.prepareToPlay(kSampleRate, kBlockSize);

        const float measured = measureGainDb(probe, freq);
        const float predicted = ParametricEQModule::responseDb(probe.getBandSnapshots(), probe.getOutputGainDb(), freq);
        EXPECT_NEAR(measured, predicted, 1.0f) << "at " << freq << " Hz";
    }
}

TEST(ParametricEQAudio, OutputGainScalesTheWholeSignal) {
    ParametricEQModule eq;
    setFloatParam(eq, "outputGain", -6.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(measureGainDb(eq, 1000.0f), -6.0f, 0.3f);
}

TEST(ParametricEQAudio, ProcessedOutputStaysFinite) {
    ParametricEQModule eq;
    enableBand(eq, 0, 100.0f, 24.0f);
    enableBand(eq, 1, 500.0f, 24.0f, 8.0f);
    enableBand(eq, 2, 3000.0f, -24.0f, 8.0f);
    enableBand(eq, 3, 8000.0f, 24.0f);
    setFloatParam(eq, "outputGain", 24.0f);
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

TEST(ParametricEQAudio, SnapshotReportsResolvedBandSettings) {
    ParametricEQModule eq;
    enableBand(eq, 1, 2000.0f, 8.0f, 2.5f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(eq, 1000.0f);

    const auto bands = eq.getBandSnapshots();
    EXPECT_TRUE(bands[1].enabled);
    EXPECT_NEAR(bands[1].freqHz, 2000.0f, 2.0f);
    EXPECT_NEAR(bands[1].gainDb, 8.0f, 0.1f);
    EXPECT_NEAR(bands[1].q, 2.5f, 0.05f);
    EXPECT_EQ(bands[1].type, BandType::Peak);
    EXPECT_FALSE(bands[0].enabled);
}

TEST(ParametricEQAudio, StereoChannelsAreFilteredIdentically) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 12.0f);
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
    enableBand(eq, 1, 1000.0f, 12.0f, 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // A single-channel buffer must still get the boost rather than passing through untouched.
    EXPECT_NEAR(measureGainDb(eq, 1000.0f, /*numChannels*/ 1), 12.0f, 0.7f);
}

TEST(ParametricEQEdgeCases, ProcessWithoutPrepareDoesNotCrash) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 12.0f);
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
    enableBand(eq, 1, 1000.0f, 6.0f);
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
        enableBand(eq, 1, 1000.0f, 12.0f, 1.0f);
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

TEST(ParametricEQEdgeCases, StateRoundTripPreservesBandsAndEnableFlags) {
    ParametricEQModule saved;
    enableBand(saved, 1, 2500.0f, -7.5f, 4.0f);
    saved.setBandEnabled(2, false);
    setFloatParam(saved, "outputGain", -3.0f);
    saved.setBypassed(true);

    juce::MemoryBlock state;
    saved.getStateInformation(state);

    ParametricEQModule restored;
    ASSERT_FALSE(restored.isBandEnabled(1));
    restored.setStateInformation(state.getData(), (int)state.getSize());

    EXPECT_TRUE(restored.isBandEnabled(1)) << "an enabled band must survive a state round-trip";
    EXPECT_FALSE(restored.isBandEnabled(2));
    EXPECT_NEAR(floatParamById(restored, bandId(1, "Freq"))->get(), 2500.0f, 2.0f);
    EXPECT_NEAR(floatParamById(restored, bandId(1, "Gain"))->get(), -7.5f, 0.05f);
    EXPECT_NEAR(floatParamById(restored, bandId(1, "Q"))->get(), 4.0f, 0.05f);
    EXPECT_NEAR(floatParamById(restored, "outputGain")->get(), -3.0f, 0.05f);
    EXPECT_TRUE(restored.isBypassed());
}

TEST(ParametricEQEdgeCases, EveryBandExposesTheFullParameterSet) {
    ParametricEQModule eq;
    for (int b = 0; b < kNumBands; ++b) {
        EXPECT_NE(boolParamById(eq, bandId(b, "On")), nullptr) << "band " << b << " on/off";
        EXPECT_NE(floatParamById(eq, bandId(b, "Freq")), nullptr) << "band " << b << " freq";
        EXPECT_NE(floatParamById(eq, bandId(b, "Gain")), nullptr) << "band " << b << " gain";
        EXPECT_NE(floatParamById(eq, bandId(b, "Q")), nullptr) << "band " << b << " Q";
    }
    EXPECT_NE(floatParamById(eq, "outputGain"), nullptr);
}
