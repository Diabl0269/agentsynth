#include "Modules/FX/DistortionModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

class DistortionSweep : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<DistortionModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<DistortionModule> module;
};

TEST_F(DistortionSweep, MixZeroSineVerification) {
    ASSERT_GE(module->getParameters().size(), 5)
        << "Module has only " << module->getParameters().size() << " parameters, expected at least 5";

    // By ID, not index — ModuleBase's own parameters (bypassed, and dualIO on a stereo-shaped
    // module) sit ahead of the module's, so a positional lookup silently resolves to the wrong one.
    auto* driveP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "drive"));
    auto* mixP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "mix"));
    auto* typeP = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "type"));
    auto* osP = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));

    ASSERT_NE(driveP, nullptr) << "Drive param is null";
    ASSERT_NE(mixP, nullptr) << "Mix param is null";
    ASSERT_NE(typeP, nullptr) << "Type param is null";
    ASSERT_NE(osP, nullptr) << "Oversampling param is null";

    module->prepareToPlay(44100.0, 512); // reset smoothers to target values

    juce::AudioBuffer<float> buffer(4, 8192); // Buffer size larger than delay (4096)
    buffer.clear();

    // Fill with a sine wave
    for (int i = 0; i < 8192; ++i) {
        float val = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 44100.0f);
        buffer.setSample(0, i, val);
        buffer.setSample(1, i, val);
    }

    juce::AudioBuffer<float> original = buffer;
    juce::MidiBuffer midi;

    module->processBlock(buffer, midi);

    // After delay, the dry signal should be aligned.
    // The delay is 4096. So we check from sample 4096 to 8192.
    for (int i = 4096; i < 8192; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), original.getSample(0, i), 1e-5f) << "Sample " << i << " mismatch at mix=0";
    }
}

TEST_F(DistortionSweep, DistortionTypeDistorts) {
    auto* typeP = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "type"));
    auto* mixP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "mix"));
    auto* driveP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "drive"));
    ASSERT_NE(typeP, nullptr);
    ASSERT_NE(mixP, nullptr);
    ASSERT_NE(driveP, nullptr);

    mixP->setValueNotifyingHost(1.0f); // fully wet
    driveP->setValueNotifyingHost(10.0f);

    juce::AudioBuffer<float> buffer(2, 512);
    for (int i = 0; i < 512; ++i)
        buffer.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 44100.0f));

    juce::AudioBuffer<float> before = buffer;
    juce::MidiBuffer midi;

    // Test each type
    for (int t = 0; t < 3; ++t) {
        typeP->setValueNotifyingHost(t / 2.0f); // 0.0=Soft, 0.5=Hard, 1.0=Foldback

        juce::AudioBuffer<float> workBuffer = before;
        module->processBlock(workBuffer, midi);

        bool changed = false;
        for (int i = 0; i < 512; ++i) {
            if (std::abs(workBuffer.getSample(0, i) - before.getSample(0, i)) > 1e-3f) {
                changed = true;
                break;
            }
        }
        EXPECT_TRUE(changed) << "Distortion type " << t << " did not change signal";
    }
}
