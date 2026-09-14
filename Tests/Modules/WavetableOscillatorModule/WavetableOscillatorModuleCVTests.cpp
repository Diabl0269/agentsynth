// WavetableOscillatorModuleCVTests.cpp
// CV modulation of position/level/pitch/octave in mono and poly mode.

#include "WavetableOscillatorModuleTestFixture.h"

TEST_F(WavetableOscillatorModuleTest, PositionCVScansTheTableInMonoMode) {
    // Baseline at position 0 (sine).
    const auto dry = render(*module, 13, 8);
    const float dryThird = magnitudeAt(dry, 0, 1320.0f, kSampleRate);

    // Full-scale CV on channel 1 pushes the scan to the square end of the table.
    auto osc = std::make_unique<WavetableOscillatorModule>();
    osc->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(13, 8 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i)
            block.getWritePointer(1)[i] = 1.0f; // Position CV
        juce::MidiBuffer midi;
        osc->processBlock(block, midi);
        out.copyFrom(0, b * kBlockSize, block, 0, 0, kBlockSize);
    }

    const float wetThird = magnitudeAt(out, 0, 1320.0f, kSampleRate);
    EXPECT_GT(wetThird, dryThird * 10.0f) << "position CV must scan the table";
}

TEST_F(WavetableOscillatorModuleTest, LevelCVAttenuatesInMonoMode) {
    const auto dry = render(*module, 13, 4);

    auto osc = std::make_unique<WavetableOscillatorModule>();
    osc->prepareToPlay(kSampleRate, kBlockSize);
    setFloat(*osc, "level", 1.0f);

    juce::AudioBuffer<float> block(13, kBlockSize);
    float wetRms = 0.0f;
    for (int b = 0; b < 4; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i)
            block.getWritePointer(5)[i] = -0.8f; // Level CV, negative
        juce::MidiBuffer midi;
        osc->processBlock(block, midi);
        wetRms = rms(block, 0);
    }

    EXPECT_LT(wetRms, rms(dry, 0) * 0.6f) << "negative level CV must attenuate";
}

TEST_F(WavetableOscillatorModuleTest, PolyModeRendersOneVoicePerPitchCVChannel) {
    setBool(*module, "poly", true);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(13, kBlockSize);
    const float voiceHz[3] = {220.0f, 330.0f, 440.0f};

    for (int b = 0; b < 4; ++b) {
        block.clear();
        for (int v = 0; v < 3; ++v)
            for (int i = 0; i < kBlockSize; ++i)
                block.getWritePointer(v)[i] = voiceHz[v];
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
    }

    for (int v = 0; v < 3; ++v)
        EXPECT_GT(rms(block, v), 0.05f) << "voice " << v << " should sound";
    for (int v = 3; v < 8; ++v)
        EXPECT_NEAR(rms(block, v), 0.0f, 1.0e-6f) << "voice " << v << " has no pitch CV and must stay silent";
    for (int ch = 8; ch < 13; ++ch)
        EXPECT_NEAR(rms(block, ch), 0.0f, 1.0e-6f) << "CV channel " << ch << " must not leak audio downstream";
}

TEST_F(WavetableOscillatorModuleTest, PolyModePositionCVScansAllVoices) {
    setBool(*module, "poly", true);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(13, 8 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i) {
            block.getWritePointer(0)[i] = 440.0f; // voice 0 pitch, Hz
            block.getWritePointer(8)[i] = 1.0f;   // shared Position CV
        }
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        out.copyFrom(0, b * kBlockSize, block, 0, 0, kBlockSize);
    }

    // Scanned to the square end: the third harmonic must be present.
    const float third = magnitudeAt(out, 0, 1320.0f, kSampleRate);
    const float fundamental = magnitudeAt(out, 0, 440.0f, kSampleRate);
    EXPECT_GT(third, fundamental * 0.1f);
}

TEST_F(WavetableOscillatorModuleTest, OctaveParameterTransposesInPolyMode) {
    setBool(*module, "poly", true);
    setInt(*module, "octave", 1);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(13, 8 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i)
            block.getWritePointer(0)[i] = 440.0f;
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        out.copyFrom(0, b * kBlockSize, block, 0, 0, kBlockSize);
    }

    EXPECT_GT(magnitudeAt(out, 0, 880.0f, kSampleRate), 0.4f) << "one octave up should sound at 880 Hz";
    EXPECT_LT(magnitudeAt(out, 0, 440.0f, kSampleRate), 0.1f);
}
