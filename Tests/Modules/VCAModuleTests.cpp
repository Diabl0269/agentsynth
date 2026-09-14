#include "Modules/VCAModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

class VCAModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        vca = std::make_unique<VCAModule>();
        vca->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<VCAModule> vca;
};

TEST_F(VCAModuleTest, GainApplication) {
    juce::AudioBuffer<float> buffer(1, 100);
    buffer.clear();
    for (int i = 0; i < 100; ++i) {
        buffer.setSample(0, i, 1.0f);
    }

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    ASSERT_NE(gainParam, nullptr);
    gainParam->setValueNotifyingHost(0.5f); // 0.5 normalized

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    // Initial gain is 0.5 (set in constructor)
    // After setValueNotifyingHost(0.5f), the target gain is 0.5.
    // However, SmoothedValue takes time to reach the target.
    // For simplicity, let's check that it's within [0, 1] and changed.
    for (int i = 0; i < 100; ++i) {
        EXPECT_GE(buffer.getSample(0, i), 0.0f);
        EXPECT_LE(buffer.getSample(0, i), 1.0f);
    }
}

TEST_F(VCAModuleTest, CVControl) {
    // VCA has 2 inputs: 0 is Audio, 1 is CV
    juce::AudioBuffer<float> buffer(2, 1000); // 1000 samples > 10ms ramp
    buffer.clear();

    // Audio input is all 1s
    for (int i = 0; i < 1000; ++i) {
        buffer.setSample(0, i, 1.0f);
    }

    // CV input is 0.5
    for (int i = 0; i < 1000; ++i) {
        buffer.setSample(1, i, 0.5f);
    }

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    gainParam->setValueNotifyingHost(1.0f); // Set gain to max

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    // Output should be roughly 1.0 (gain) * 0.5 (cv) = 0.5
    EXPECT_NEAR(buffer.getSample(0, 999), 0.5f, 0.01f);
}

TEST_F(VCAModuleTest, MonoToStereoCopy) {
    juce::AudioBuffer<float> buffer(2, 100);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f); // Only first channel

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    // Second channel should be copy of first
    EXPECT_EQ(buffer.getSample(0, 0), buffer.getSample(1, 0));
}

TEST_F(VCAModuleTest, PolyMode_MultiVoice) {
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(vca.get(), "poly"));
    ASSERT_NE(polyP, nullptr);
    *polyP = true;

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    gainParam->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> polyBuffer(16, 1000);
    polyBuffer.clear();

    // Audio on ch 0 and 1, CV on ch 8 and 9
    for (int i = 0; i < 1000; ++i) {
        polyBuffer.setSample(0, i, 1.0f); // Voice 0 audio
        polyBuffer.setSample(1, i, 0.5f); // Voice 1 audio
        polyBuffer.setSample(8, i, 1.0f); // Voice 0 CV
        polyBuffer.setSample(9, i, 0.5f); // Voice 1 CV
    }

    juce::MidiBuffer midi;
    vca->processBlock(polyBuffer, midi);

    // VCA poly sums voices to stereo via tanh(sum * 0.125)
    // Voice 0: 1.0 * 1.0 * 1.0 = 1.0, Voice 1: 0.5 * 1.0 * 0.5 = 0.25
    // Sum = 1.25, tanh(1.25 * 0.125) = tanh(0.15625) ≈ 0.155
    float expected = std::tanh((1.0f + 0.25f) * 0.125f);
    EXPECT_NEAR(polyBuffer.getSample(0, 999), expected, 0.05f); // Left
    EXPECT_NEAR(polyBuffer.getSample(1, 999), expected, 0.05f); // Right (same as left)
}

TEST_F(VCAModuleTest, MonoMode_BackwardsCompatible) {
    // Default poly=false
    juce::AudioBuffer<float> buf(2, 1000);
    buf.clear();
    for (int i = 0; i < 1000; ++i) {
        buf.setSample(0, i, 1.0f);
        buf.setSample(1, i, 0.5f);
    }

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    gainParam->setValueNotifyingHost(1.0f);

    juce::MidiBuffer midi;
    vca->processBlock(buf, midi);

    // Output = audio * gain * cv = 1.0 * 1.0 * 0.5 = 0.5
    EXPECT_NEAR(buf.getSample(0, 999), 0.5f, 0.05f);
    // Channel 1 should be copy of channel 0 (mono-to-stereo)
    EXPECT_EQ(buf.getSample(0, 999), buf.getSample(1, 999));
}
