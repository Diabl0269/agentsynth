#include "Modules/VoiceMixerModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

class VoiceMixerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        mixer = std::make_unique<VoiceMixerModule>();
        mixer->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<VoiceMixerModule> mixer;
};

TEST_F(VoiceMixerModuleTest, SumsMultipleInputChannels) {
    juce::AudioBuffer<float> buffer(8, 512);
    buffer.clear();

    // Set channels 0-3 to 1.0
    for (int ch = 0; ch < 4; ++ch) {
        for (int i = 0; i < 512; ++i) {
            buffer.setSample(ch, i, 1.0f);
        }
    }

    juce::MidiBuffer midi;
    mixer->processBlock(buffer, midi);

    // Should sum the active channels and apply level multiplier
    // At least check that output is non-zero and reasonable
    float output0 = buffer.getSample(0, 511);
    EXPECT_GT(output0, 0.0f); // Should be greater than zero
    EXPECT_LT(output0, 4.0f); // But less than raw sum (since we multiply by level param)
}

TEST_F(VoiceMixerModuleTest, HasEightInputChannels) { EXPECT_EQ(mixer->getTotalNumInputChannels(), 8); }

TEST_F(VoiceMixerModuleTest, HasTwoOutputChannels) { EXPECT_EQ(mixer->getTotalNumOutputChannels(), 2); }

TEST_F(VoiceMixerModuleTest, StereoOutputMatchesMonoOutput) {
    juce::AudioBuffer<float> buffer(8, 512);
    buffer.clear();

    // Set all input channels to 2.0
    for (int ch = 0; ch < 8; ++ch) {
        for (int i = 0; i < 512; ++i) {
            buffer.setSample(ch, i, 2.0f);
        }
    }

    juce::MidiBuffer midi;
    mixer->processBlock(buffer, midi);

    // Both output channels should be identical (mono mixed to stereo)
    for (int i = 0; i < 512; ++i) {
        EXPECT_EQ(buffer.getSample(0, i), buffer.getSample(1, i));
    }
}

TEST_F(VoiceMixerModuleTest, SilentWhenAllInputsAreZero) {
    juce::AudioBuffer<float> buffer(8, 512);
    buffer.clear(); // All zeros

    juce::MidiBuffer midi;
    mixer->processBlock(buffer, midi);

    // Output should remain zero
    for (int sample = 0; sample < 512; ++sample) {
        EXPECT_EQ(buffer.getSample(0, sample), 0.0f);
        EXPECT_EQ(buffer.getSample(1, sample), 0.0f);
    }
}

TEST_F(VoiceMixerModuleTest, LevelParameterExists) {
    // Verify module has a level parameter
    auto* levelParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(mixer.get(), "level"));
    ASSERT_NE(levelParam, nullptr);
    EXPECT_EQ(levelParam->paramID, "level");
}

TEST_F(VoiceMixerModuleTest, OutputAttenuatedByLevelParameter) {
    juce::AudioBuffer<float> buffer1(8, 512);
    juce::AudioBuffer<float> buffer2(8, 512);
    buffer1.clear();
    buffer2.clear();

    // Set input on both buffers
    for (int ch = 0; ch < 8; ++ch) {
        for (int i = 0; i < 512; ++i) {
            buffer1.setSample(ch, i, 1.0f);
            buffer2.setSample(ch, i, 1.0f);
        }
    }

    // Process with default level (0.125)
    juce::MidiBuffer midi;
    mixer->processBlock(buffer1, midi);
    float out1 = buffer1.getSample(0, 511);

    // Change level to higher value
    auto* levelParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(mixer.get(), "level"));
    levelParam->setValueNotifyingHost(1.0f);
    mixer->prepareToPlay(44100.0, 512); // Reset smoothing
    mixer->processBlock(buffer2, midi);
    float out2 = buffer2.getSample(0, 511);

    // Output with higher level should be greater
    EXPECT_GT(out2, out1);
}
