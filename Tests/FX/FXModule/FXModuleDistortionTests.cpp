// FXModuleDistortionTests.cpp — Distortion module coverage
#include "Modules/FX/DistortionModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

// ---------------------------------------------------------------------------
// DistortionModule tests
// ---------------------------------------------------------------------------

class DistortionModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<DistortionModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<DistortionModule> module;
};

TEST_F(DistortionModuleTest, ProcessBlockProducesOutput) {
    // DistortionModule: 4 channels (2 audio + 2 CV)
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // Output should be non-zero (signal passed through)
    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);

    // CV channels should be cleared
    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f);
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f);
    }
}

TEST_F(DistortionModuleTest, HighDriveClipsSignal) {
    DistortionModule lowDrive, highDrive;
    lowDrive.prepareToPlay(44100.0, 512);
    highDrive.prepareToPlay(44100.0, 512);

    auto* driveParamLow = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&lowDrive, "drive"));
    auto* driveParamHigh = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&highDrive, "drive"));
    ASSERT_NE(driveParamLow, nullptr);
    ASSERT_NE(driveParamHigh, nullptr);

    driveParamLow->setValueNotifyingHost(0.0f);  // min drive (1.0)
    driveParamHigh->setValueNotifyingHost(1.0f); // max drive (20.0)

    // Set mix=1.0 (fully wet) on both so we hear the distortion effect clearly
    auto* mixParamLow = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&lowDrive, "mix"));
    auto* mixParamHigh = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&highDrive, "mix"));
    mixParamLow->setValueNotifyingHost(1.0f);
    mixParamHigh->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> bufferLow(4, 512);
    juce::AudioBuffer<float> bufferHigh(4, 512);
    bufferLow.clear();
    bufferHigh.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i) {
            bufferLow.setSample(ch, i, 0.5f);
            bufferHigh.setSample(ch, i, 0.5f);
        }

    juce::MidiBuffer midi;
    lowDrive.processBlock(bufferLow, midi);
    highDrive.processBlock(bufferHigh, midi);

    // High drive should produce larger (more clipped) amplitude than low drive
    float ampLow = std::abs(bufferLow.getSample(0, 511));
    float ampHigh = std::abs(bufferHigh.getSample(0, 511));
    EXPECT_GT(ampHigh, ampLow);
}

TEST_F(DistortionModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(DistortionModuleTest, MinDriveIsTransparent) {
    // At drive=1 (minimum) with mix=1 and oversampling=Off, output should equal input
    auto* driveP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "drive"));
    auto* mixP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "mix"));
    auto* osP = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    driveP->setValueNotifyingHost(0.0f); // normalized 0 = drive 1.0
    mixP->setValueNotifyingHost(1.0f);   // fully wet
    *osP = 0;                            // Off — isolate waveshaper from oversampling filters

    module->prepareToPlay(44100.0, 512); // re-init smoothers with new param values

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // With drive at minimum, every sample should be unchanged (transparent)
    for (int i = 0; i < 512; ++i)
        EXPECT_NEAR(buffer.getSample(0, i), 0.5f, 1e-5f) << "Sample " << i << " not transparent";
}

TEST_F(DistortionModuleTest, OversamplingOffProducesOutput) {
    // Set oversampling to Off (index 0)
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    ASSERT_NE(param, nullptr);
    *param = 0; // Off

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i)
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    EXPECT_TRUE(anyNonZero);

    // CV channels must be cleared
    for (int ch = 2; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f);
}

TEST_F(DistortionModuleTest, Oversampling4xProducesOutput) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    ASSERT_NE(param, nullptr);
    *param = 2; // 4x

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i)
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(DistortionModuleTest, OversamplingParameterProperties) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(param->choices.size(), 3);
    EXPECT_EQ(param->choices[0], juce::String("Off"));
    EXPECT_EQ(param->choices[1], juce::String("2x"));
    EXPECT_EQ(param->choices[2], juce::String("4x"));
    // The default oversampling is 2x, which is index 1.
    EXPECT_EQ(param->getIndex(), 1);
}

TEST_F(DistortionModuleTest, OversamplingNotInModulationTargets) {
    auto targets = module->getModulationTargets();
    for (const auto& t : targets)
        EXPECT_NE(t.name, "Oversampling");
}

TEST_F(DistortionModuleTest, LatencyZeroWhenOff) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    ASSERT_NE(param, nullptr);
    *param = 0; // Off
    EXPECT_DOUBLE_EQ(module->getLatencyInSamples(), 0.0);
}

TEST_F(DistortionModuleTest, LatencyNonZeroWhenEnabled) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    ASSERT_NE(param, nullptr);
    *param = 1; // 2x
    EXPECT_GT(module->getLatencyInSamples(), 0.0);
    *param = 2; // 4x
    EXPECT_GT(module->getLatencyInSamples(), 0.0);
}

TEST_F(DistortionModuleTest, SwitchModesDuringPlayback) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module.get(), "oversampling"));
    ASSERT_NE(param, nullptr);

    juce::MidiBuffer midi;

    // Start at 2x (default), process a block
    juce::AudioBuffer<float> buf1(4, 512);
    buf1.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buf1.setSample(ch, i, 0.5f);
    EXPECT_NO_THROW(module->processBlock(buf1, midi));

    // Switch to Off
    *param = 0;
    juce::AudioBuffer<float> buf2(4, 512);
    buf2.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buf2.setSample(ch, i, 0.5f);
    EXPECT_NO_THROW(module->processBlock(buf2, midi));

    // Switch to 4x
    *param = 2;
    juce::AudioBuffer<float> buf3(4, 512);
    buf3.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buf3.setSample(ch, i, 0.5f);
    EXPECT_NO_THROW(module->processBlock(buf3, midi));
}

TEST_F(DistortionModuleTest, HighDriveClipsSignalAllModes) {
    // Test that high drive produces stronger distortion than low drive in all modes
    for (int mode = 0; mode < 3; ++mode) {
        DistortionModule lowDrive, highDrive;
        lowDrive.prepareToPlay(44100.0, 512);
        highDrive.prepareToPlay(44100.0, 512);

        // Set oversampling mode
        auto* lowOsParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&lowDrive, "oversampling"));
        auto* highOsParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&highDrive, "oversampling"));
        *lowOsParam = mode;
        *highOsParam = mode;

        // Set drive: low=1 (normalized 0), high=20 (normalized 1)
        auto* lowDriveParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&lowDrive, "drive"));
        auto* highDriveParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&highDrive, "drive"));
        lowDriveParam->setValueNotifyingHost(0.0f);
        highDriveParam->setValueNotifyingHost(1.0f);

        // Set mix to fully wet
        auto* lowMixParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&lowDrive, "mix"));
        auto* highMixParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&highDrive, "mix"));
        lowMixParam->setValueNotifyingHost(1.0f);
        highMixParam->setValueNotifyingHost(1.0f);

        juce::AudioBuffer<float> bufferLow(4, 512), bufferHigh(4, 512);
        bufferLow.clear();
        bufferHigh.clear();
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 512; ++i) {
                bufferLow.setSample(ch, i, 0.5f);
                bufferHigh.setSample(ch, i, 0.5f);
            }
        }

        juce::MidiBuffer midi;
        lowDrive.processBlock(bufferLow, midi);
        highDrive.processBlock(bufferHigh, midi);

        // High drive should clip more (output closer to 1.0 due to saturation)
        float lowOut = std::abs(bufferLow.getSample(0, 511));
        float highOut = std::abs(bufferHigh.getSample(0, 511));
        EXPECT_GT(highOut, lowOut) << "Failed for oversampling mode " << mode;
    }
}
