// FXModuleDelayTests.cpp — Delay module coverage
#include "Modules/FX/DelayModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

// ---------------------------------------------------------------------------
// DelayModule tests
// ---------------------------------------------------------------------------

class DelayModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<DelayModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<DelayModule> module;
};

TEST_F(DelayModuleTest, ProcessBlockPassesSignalThrough) {
    // DelayModule: 2 inputs, 2 outputs
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    // Fill both channels with signal
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.8f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // With mix=0.3 (default), output = wet*0.3 + dry*0.7
    // Dry portion alone should keep output non-zero
    float outSample = buffer.getSample(0, 0);
    EXPECT_NE(outSample, 0.0f);
}

TEST_F(DelayModuleTest, FeedbackParameterExists) {
    auto* feedbackParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "feedback"));
    ASSERT_NE(feedbackParam, nullptr);
    EXPECT_FLOAT_EQ(feedbackParam->get(), 0.5f); // default
}

TEST_F(DelayModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.1f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}
