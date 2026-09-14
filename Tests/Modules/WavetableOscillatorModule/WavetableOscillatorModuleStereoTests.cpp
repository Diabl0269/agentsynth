// WavetableOscillatorModuleStereoTests.cpp
// Stereo output, panning, and unison width (issue #180 phase 3).

#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, DefaultsKeepAudioLIdenticalToAudioR) {
    // Width 0 / pan 0 is mono-compatible: both jacks carry the same signal at full level, so a
    // patch that only cables Audio L sounds exactly as it did before the module went stereo.
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto out = renderNote(*module, 60, 2);

    const float left = rms(out, 0);
    ASSERT_GT(left, 0.05f);
    EXPECT_NEAR(rms(out, WavetableOscillatorModule::kRightBase), left, 1.0e-6f);
}

TEST_F(WavetableOscillatorModuleTest, UnisonWidthSeparatesTheStereoLegs) {
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 30.0f);
    setFloat(*module, "width", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto centred = renderNote(*module, 60, 2);
    const int R = WavetableOscillatorModule::kRightBase;

    // At width 0 the legs are identical, so their difference is silence.
    float centredDiff = 0.0f;
    for (int i = 0; i < centred.getNumSamples(); ++i)
        centredDiff = std::max(centredDiff, std::abs(centred.getReadPointer(0)[i] - centred.getReadPointer(R)[i]));
    EXPECT_LT(centredDiff, 1.0e-5f);

    setFloat(*module, "width", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto wide = renderNote(*module, 60, 2);

    float wideDiff = 0.0f;
    for (int i = 0; i < wide.getNumSamples(); ++i)
        wideDiff = std::max(wideDiff, std::abs(wide.getReadPointer(0)[i] - wide.getReadPointer(R)[i]));
    EXPECT_GT(wideDiff, 0.05f) << "width must place the detuned unison voices differently in L and R";
}

TEST_F(WavetableOscillatorModuleTest, PanShiftsEnergyBetweenTheLegs) {
    const int R = WavetableOscillatorModule::kRightBase;

    setFloat(*module, "pan", -1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto left = renderNote(*module, 60, 2);
    EXPECT_GT(rms(left, 0), 0.05f);
    EXPECT_NEAR(rms(left, R), 0.0f, 1.0e-6f);

    setFloat(*module, "pan", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto right = renderNote(*module, 60, 2);
    EXPECT_NEAR(rms(right, 0), 0.0f, 1.0e-6f);
    EXPECT_GT(rms(right, R), 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, PolyModeWritesBothAudioBlocks) {
    using WT = WavetableOscillatorModule;
    setBool(*module, "poly", true);
    setFloat(*module, "pan", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(kCh, 2 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(kCh, kBlockSize);
    for (int b = 0; b < 2; ++b) {
        block.clear();
        // Three sounding voices at distinct pitches.
        juce::FloatVectorOperations::fill(block.getWritePointer(0), 220.0f, kBlockSize);
        juce::FloatVectorOperations::fill(block.getWritePointer(1), 330.0f, kBlockSize);
        juce::FloatVectorOperations::fill(block.getWritePointer(2), 440.0f, kBlockSize);
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    for (int v = 0; v < 3; ++v) {
        EXPECT_GT(rms(out, v), 0.05f) << "voice " << v << " left leg";
        EXPECT_GT(rms(out, WT::kRightBase + v), 0.05f) << "voice " << v << " right leg";
    }
    // Silent voices stay silent on both legs.
    EXPECT_NEAR(rms(out, 3), 0.0f, 1.0e-6f);
    EXPECT_NEAR(rms(out, WT::kRightBase + 3), 0.0f, 1.0e-6f);
    // The shared CV block between the two audio blocks must not leak audio.
    EXPECT_NEAR(rms(out, WT::kPolyModCVBase), 0.0f, 1.0e-6f);
}
