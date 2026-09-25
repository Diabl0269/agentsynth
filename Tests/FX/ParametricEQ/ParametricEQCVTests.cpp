// ParametricEQCVTests.cpp
// CV modulation of the Parametric EQ: the original bell jacks (ch2-5, bespoke exponential/linear
// curves) and the normalised-CV jacks for every remaining parameter (ch6-14).

#include "ParametricEQTestHelpers.h"
#include <gtest/gtest.h>

using namespace eqtest;

// ============================================================================
// CV modulation (channels 2-5 drive the two bell bands)
// ============================================================================

TEST(ParametricEQCV, FreqCVSweepsTheFullRangeExponentially) {
    constexpr float lo = ParametricEQModule::kMinFreq;
    constexpr float hi = ParametricEQModule::kMaxFreq;
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 0.0f), 500.0f, 0.01f);
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 1.0f), hi, 1.0f);
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, -1.0f), lo, 0.1f);
    // Half-way up is the geometric mean of the base and the top — an exponential sweep.
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 0.5f), std::sqrt(500.0f * hi), 1.0f);
    // Out-of-range CV is clamped, not extrapolated.
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, 4.0f), hi, 1.0f);
    EXPECT_NEAR(ParametricEQModule::applyFreqCV(500.0f, -4.0f), lo, 0.1f);
}

TEST(ParametricEQCV, GainCVAddsOntoTheKnobAndClamps) {
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(0.0f, 0.5f), 12.0f);
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(0.0f, -1.0f), -ParametricEQModule::kMaxGainDb);
    // Knob at +12 dB plus full CV saturates at the +24 dB ceiling.
    EXPECT_FLOAT_EQ(ParametricEQModule::applyGainCV(12.0f, 1.0f), ParametricEQModule::kMaxGainDb);
}

TEST(ParametricEQCV, GainCVOnChannel3ModulatesTheFirstBell) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 0.0f, 1.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // +50% of the +/-24 dB range == +12 dB on band 2 (the first bell).
    const float gainDb = measureGainDb(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(3, i, 0.5f);
    });

    EXPECT_NEAR(eq.getBandSnapshots()[1].gainDb, 12.0f, 0.2f);
    EXPECT_NEAR(gainDb, 12.0f, 0.7f) << "CV gain boost should show up in the output level";
}

TEST(ParametricEQCV, SilentCVJackLeavesTheBandAtItsKnobValue) {
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 5.0f);
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
    enableBand(baseline, 1, 1000.0f, 0.0f);
    baseline.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(baseline, 1000.0f);
    const float baselineGain = baseline.getBandSnapshots()[1].gainDb;

    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 0.0f);
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
    enableBand(eq, 1, 1000.0f, 0.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(3, i, 0.25f);
    });

    EXPECT_NEAR(eq.getBandSnapshots()[1].gainDb, 6.0f, 0.2f);
}

TEST(ParametricEQCV, FreqCVOnChannel2MovesTheFirstBell) {
    ParametricEQModule eq;
    enableBand(eq, 1, 500.0f, 12.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // Sweep the bell to the top of the 20 Hz - 20 kHz range.
    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(2, i, 1.0f);
    });

    EXPECT_GT(eq.getBandSnapshots()[1].freqHz, 500.0f);
    EXPECT_NEAR(eq.getBandSnapshots()[1].freqHz, ParametricEQModule::kMaxFreq, 500.0f);
}

TEST(ParametricEQCV, NegativeFreqCVOnChannel4MovesTheSecondBellDown) {
    ParametricEQModule eq;
    enableBand(eq, 2, 3000.0f, 0.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(4, i, -1.0f);
    });

    EXPECT_NEAR(eq.getBandSnapshots()[2].freqHz, ParametricEQModule::kMinFreq, 5.0f);
}

TEST(ParametricEQCV, ShelvesAreNotCVModulated) {
    // Only the two bells take CV; stamping every CV channel must leave the shelves put.
    ParametricEQModule eq;
    enableBand(eq, 0, 150.0f, 6.0f);
    enableBand(eq, 3, 9000.0f, -6.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    measureRMS(eq, 1000.0f, 6, [](juce::AudioBuffer<float>& buffer) {
        for (int ch = 2; ch < 6; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, 1.0f);
    });

    const auto bands = eq.getBandSnapshots();
    EXPECT_NEAR(bands[0].freqHz, 150.0f, 1.0f);
    EXPECT_NEAR(bands[0].gainDb, 6.0f, 0.1f);
    EXPECT_NEAR(bands[3].freqHz, 9000.0f, 5.0f);
    EXPECT_NEAR(bands[3].gainDb, -6.0f, 0.1f);
}

// ============================================================================
// CV modulation of the remaining parameters (ch6-14: B1/B4 Freq+Gain, every band's Q, Output) —
// normalised convention, docs/modules/modulation.md#cv-in-normalised-units
// ============================================================================

TEST(ParametricEQCV, FreqCVOnChannel6MovesTheLowShelf) {
    ParametricEQModule eq;
    enableBand(eq, 0, 200.0f, 0.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    measureRMS(eq, 1000.0f, 15, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(6, i, 1.0f);
    });

    EXPECT_GT(eq.getBandSnapshots()[0].freqHz, 200.0f) << "positive CV on B1 Freq should sweep the low shelf up";
}

TEST(ParametricEQCV, GainCVOnChannel9MovesTheHighShelf) {
    ParametricEQModule eq;
    enableBand(eq, 3, 8000.0f, 0.0f);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    measureRMS(eq, 1000.0f, 15, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(9, i, 1.0f);
    });

    EXPECT_GT(eq.getBandSnapshots()[3].gainDb, 0.0f) << "positive CV on B4 Gain should raise the high shelf";
}

TEST(ParametricEQCV, QCVChangesTheBandsResponse) {
    // Widening Q on a boosted bell moves energy at a frequency one octave below centre, even
    // though the centre gain itself is untouched by Q.
    ParametricEQModule eq;
    enableBand(eq, 1, 1000.0f, 18.0f, ParametricEQModule::kMaxQ);
    eq.prepareToPlay(kSampleRate, kBlockSize);

    const float narrowGainDb = measureGainDb(eq, 500.0f, 15);

    ParametricEQModule widened;
    enableBand(widened, 1, 1000.0f, 18.0f, ParametricEQModule::kMaxQ);
    widened.prepareToPlay(kSampleRate, kBlockSize);
    const float widenedGainDb = measureGainDb(widened, 500.0f, 15, [](juce::AudioBuffer<float>& buffer) {
        // B2 Q is channel 11 (B1 Q is 10 — see getModulationTargets/getInputPortLabel); negative
        // CV widens Q toward kMinQ, positive CV narrows it further toward kMaxQ.
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(11, i, -1.0f);
    });

    EXPECT_GT(widenedGainDb, narrowGainDb) << "widening Q via CV should lift more energy an octave below centre";
}

TEST(ParametricEQCV, OutputCVChangesTheGain) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);

    // Output CV is channel 14; +1.0 should sweep the trim from 0 dB toward its +24 dB ceiling.
    const float gainDb = measureGainDb(eq, 1000.0f, 15, [](juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(14, i, 1.0f);
    });

    EXPECT_GT(gainDb, 6.0f) << "full-scale positive CV on Output should raise the level well above unity";
}

TEST(ParametricEQCV, ZeroCVOnTheNewChannelsMatchesNoCVAtAll) {
    // A 6-channel buffer (today's shape) and a 15-channel buffer with the new CV channels present
    // but silent must produce the identical resolved band state.
    ParametricEQModule legacy;
    enableBand(legacy, 0, 300.0f, 4.0f, 2.0f);
    enableBand(legacy, 3, 6000.0f, -4.0f, 2.0f);
    legacy.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(legacy, 1000.0f, 6);

    ParametricEQModule wide;
    enableBand(wide, 0, 300.0f, 4.0f, 2.0f);
    enableBand(wide, 3, 6000.0f, -4.0f, 2.0f);
    wide.prepareToPlay(kSampleRate, kBlockSize);
    measureRMS(wide, 1000.0f, 15); // extra channels present but silent (unpatched)

    const auto legacyBands = legacy.getBandSnapshots();
    const auto wideBands = wide.getBandSnapshots();
    for (int b = 0; b < kNumBands; ++b) {
        EXPECT_NEAR(wideBands[(size_t)b].freqHz, legacyBands[(size_t)b].freqHz, 1.0f) << "band " << b;
        EXPECT_NEAR(wideBands[(size_t)b].gainDb, legacyBands[(size_t)b].gainDb, 0.05f) << "band " << b;
        EXPECT_NEAR(wideBands[(size_t)b].q, legacyBands[(size_t)b].q, 0.05f) << "band " << b;
    }
    EXPECT_NEAR(wide.getOutputGainDb(), legacy.getOutputGainDb(), 0.05f);
}

TEST(ParametricEQCV, NewCVChannelsDoNotLeakIntoAudioOutputs) {
    ParametricEQModule eq;
    eq.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(15, kBlockSize);
    buffer.clear();
    for (int ch = 2; ch < 15; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    eq.processBlock(buffer, midi);

    for (int ch = 2; ch < 15; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch" << ch << " must not leak downstream";
}
