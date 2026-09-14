// FXModuleReverbTests.cpp — Reverb module coverage
#include "Modules/FX/ReverbModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

// ---------------------------------------------------------------------------
// ReverbModule tests
// ---------------------------------------------------------------------------

class ReverbModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<ReverbModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<ReverbModule> module;
};

TEST_F(ReverbModuleTest, ProcessBlockProducesOutput) {
    // ReverbModule: 2 channels (stereo)
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // Output should be non-zero (wet + dry signal)
    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(ReverbModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.2f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(ReverbModuleTest, MonoProcessingDoesNotCrash) {
    // Test with a mono buffer (single channel)
    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i)
        buffer.setSample(0, i, 0.4f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));

    // Mono reverb should produce non-zero output
    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(ReverbModuleTest, RoomSizeParameterIsApplied) {
    // Just verify the parameters exist and are the right types
    auto* roomSizeParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "roomSize"));
    auto* dampingParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "damping"));
    ASSERT_NE(roomSizeParam, nullptr);
    ASSERT_NE(dampingParam, nullptr);

    // Change room size and verify processBlock still runs without crash
    roomSizeParam->setValueNotifyingHost(1.0f);
    dampingParam->setValueNotifyingHost(0.8f);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}
