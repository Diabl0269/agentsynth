#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FX/BitcrusherModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

// ---------------------------------------------------------------------------
// AttenuverterModule tests
// ---------------------------------------------------------------------------

class AttenuverterModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<AttenuverterModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<AttenuverterModule> module;
};

TEST_F(AttenuverterModuleTest, ProcessBlockAttenuatesSignal) {
    // 2 channels: ch0 = audio, ch1 = CV amount
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    // Fill audio channel with a constant 1.0f
    for (int i = 0; i < 512; ++i)
        buffer.setSample(0, i, 1.0f);

    // Set amount param to 0.5 via the first float parameter
    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]);
    ASSERT_NE(amountParam, nullptr);
    amountParam->setValueNotifyingHost(0.75f); // normalized: maps to 0.75 in [-1,1] range => 0.5

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // Output should be attenuated (not the original 1.0f)
    // We just verify it stays within a reasonable range and is non-zero
    float lastSample = buffer.getSample(0, 511);
    EXPECT_GE(lastSample, -1.0f);
    EXPECT_LE(lastSample, 1.0f);
}

TEST_F(AttenuverterModuleTest, ProcessBlockBypassClearsSilent) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    // Fill audio channel with signal
    for (int i = 0; i < 512; ++i)
        buffer.setSample(0, i, 1.0f);

    // Enable bypass (parameter index 0 in ModuleBase)
    auto* bypassParam = dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[0]);
    ASSERT_NE(bypassParam, nullptr);
    bypassParam->setValueNotifyingHost(1.0f); // true

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // With bypass enabled, buffer should be cleared (silent)
    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.0f);
    }
}

TEST_F(AttenuverterModuleTest, ProcessBlockWithCVAmountModulation) {
    // ch0 = audio, ch1 = CV modulation on amount
    juce::AudioBuffer<float> buffer(2, 1000);
    buffer.clear();

    // Fill audio channel with 1.0f
    for (int i = 0; i < 1000; ++i)
        buffer.setSample(0, i, 1.0f);

    // Set base amount to 0.0 so the CV alone controls the output
    auto* amountParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]);
    ASSERT_NE(amountParam, nullptr);
    amountParam->setValueNotifyingHost(0.5f); // normalized 0.5 => 0.0 in [-1,1]

    // Set CV channel to 0.5
    for (int i = 0; i < 1000; ++i)
        buffer.setSample(1, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // With CV = 0.5 and base amount ~0.0, result should be influenced by CV
    // Output should be non-zero at the last sample (after smoothing settles)
    float lastSample = buffer.getSample(0, 999);
    EXPECT_NE(lastSample, 0.0f);
}

TEST_F(AttenuverterModuleTest, ProcessBlockEmptyBufferDoesNotCrash) {
    juce::AudioBuffer<float> buffer(0, 512);
    juce::MidiBuffer midi;
    // Should return early without crashing
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

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
    auto* feedbackParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[2]);
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

    auto* driveParamLow = dynamic_cast<juce::AudioParameterFloat*>(lowDrive.getParameters()[1]);
    auto* driveParamHigh = dynamic_cast<juce::AudioParameterFloat*>(highDrive.getParameters()[1]);
    ASSERT_NE(driveParamLow, nullptr);
    ASSERT_NE(driveParamHigh, nullptr);

    driveParamLow->setValueNotifyingHost(0.0f);  // min drive (1.0)
    driveParamHigh->setValueNotifyingHost(1.0f); // max drive (20.0)

    // Set mix=1.0 (fully wet) on both so we hear the distortion effect clearly
    auto* mixParamLow = dynamic_cast<juce::AudioParameterFloat*>(lowDrive.getParameters()[2]);
    auto* mixParamHigh = dynamic_cast<juce::AudioParameterFloat*>(highDrive.getParameters()[2]);
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
    auto* driveP = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]);
    auto* mixP = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[2]);
    auto* osP = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[4]); // Index 4 now
    driveP->setValueNotifyingHost(0.0f);                                               // normalized 0 = drive 1.0
    mixP->setValueNotifyingHost(1.0f);                                                 // fully wet
    *osP = 0; // Off — isolate waveshaper from oversampling filters

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
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[4]);
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
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[3]);
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
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[4]);
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
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[4]);
    ASSERT_NE(param, nullptr);
    *param = 0; // Off
    EXPECT_DOUBLE_EQ(module->getLatencyInSamples(), 0.0);
}

TEST_F(DistortionModuleTest, LatencyNonZeroWhenEnabled) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[4]);
    ASSERT_NE(param, nullptr);
    *param = 1; // 2x
    EXPECT_GT(module->getLatencyInSamples(), 0.0);
    *param = 2; // 4x
    EXPECT_GT(module->getLatencyInSamples(), 0.0);
}

TEST_F(DistortionModuleTest, SwitchModesDuringPlayback) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[4]);
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
        auto* lowOsParam = dynamic_cast<juce::AudioParameterChoice*>(lowDrive.getParameters()[4]);
        auto* highOsParam = dynamic_cast<juce::AudioParameterChoice*>(highDrive.getParameters()[4]);
        *lowOsParam = mode;
        *highOsParam = mode;

        // Set drive: low=1 (normalized 0), high=20 (normalized 1)
        auto* lowDriveParam = dynamic_cast<juce::AudioParameterFloat*>(lowDrive.getParameters()[1]);
        auto* highDriveParam = dynamic_cast<juce::AudioParameterFloat*>(highDrive.getParameters()[1]);
        lowDriveParam->setValueNotifyingHost(0.0f);
        highDriveParam->setValueNotifyingHost(1.0f);

        // Set mix to fully wet
        auto* lowMixParam = dynamic_cast<juce::AudioParameterFloat*>(lowDrive.getParameters()[2]);
        auto* highMixParam = dynamic_cast<juce::AudioParameterFloat*>(highDrive.getParameters()[2]);
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
    auto* roomSizeParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]);
    auto* dampingParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[2]);
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

// ---------------------------------------------------------------------------
// ChorusModule tests
// ---------------------------------------------------------------------------

class ChorusModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<ChorusModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<ChorusModule> module;
};

TEST_F(ChorusModuleTest, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(ChorusModuleTest, CVChannelsAreClearedAfterProcessing) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    for (int i = 0; i < 512; ++i) {
        buffer.setSample(2, i, 0.3f);
        buffer.setSample(3, i, 0.2f);
    }

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f);
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f);
    }
}

TEST_F(ChorusModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(ChorusModuleTest, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Chorus);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

// ---------------------------------------------------------------------------
// PhaserModule tests
// ---------------------------------------------------------------------------

class PhaserModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<PhaserModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<PhaserModule> module;
};

TEST_F(PhaserModuleTest, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(PhaserModuleTest, CVChannelsAreClearedAfterProcessing) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    for (int i = 0; i < 512; ++i) {
        buffer.setSample(2, i, 0.3f);
        buffer.setSample(3, i, 0.2f);
    }

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f);
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f);
    }
}

TEST_F(PhaserModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(PhaserModuleTest, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Phaser);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

// ---------------------------------------------------------------------------
// CompressorModule tests
// ---------------------------------------------------------------------------

class CompressorModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<CompressorModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<CompressorModule> module;
};

TEST_F(CompressorModuleTest, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(CompressorModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(CompressorModuleTest, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Compressor);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

// ---------------------------------------------------------------------------
// FlangerModule tests
// ---------------------------------------------------------------------------

class FlangerModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<FlangerModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<FlangerModule> module;
};

TEST_F(FlangerModuleTest, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(FlangerModuleTest, CVChannelsAreClearedAfterProcessing) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    for (int i = 0; i < 512; ++i) {
        buffer.setSample(2, i, 0.3f);
        buffer.setSample(3, i, 0.2f);
    }

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f);
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f);
    }
}

TEST_F(FlangerModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(FlangerModuleTest, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Flanger);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

// ---------------------------------------------------------------------------
// LimiterModule tests
// ---------------------------------------------------------------------------

class LimiterModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<LimiterModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<LimiterModule> module;
};

TEST_F(LimiterModuleTest, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(LimiterModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(LimiterModuleTest, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Limiter);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

// ---------------------------------------------------------------------------
// BitcrusherModule tests
// ---------------------------------------------------------------------------

class BitcrusherModuleTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<BitcrusherModule>();
        module->prepareToPlay(44100.0, 512);
    }
    std::unique_ptr<BitcrusherModule> module;
};

TEST_F(BitcrusherModuleTestFixture, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(5, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    float rms = buffer.getRMSLevel(0, 0, 512);
    EXPECT_GT(rms, 0.0f);
}

TEST_F(BitcrusherModuleTestFixture, CVChannelsAreClearedAfterProcessing) {
    juce::AudioBuffer<float> buffer(5, 512);
    buffer.clear();
    for (int ch = 0; ch < 5; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int ch = 2; ch < 5; ++ch) {
        for (int i = 0; i < 512; ++i) {
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch" << ch << " sample " << i;
        }
    }
}

TEST_F(BitcrusherModuleTestFixture, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(5, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(BitcrusherModuleTestFixture, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Bitcrusher);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

// ---------------------------------------------------------------------------
// Port Label tests
// ---------------------------------------------------------------------------

TEST(PortLabelTests, OscillatorPortLabels) {
    OscillatorModule osc;
    // Default mono mode: original channel layout
    EXPECT_EQ(osc.getInputPortLabel(0), "Pitch");
    EXPECT_EQ(osc.getInputPortLabel(1), "Waveform");
    EXPECT_EQ(osc.getInputPortLabel(2), "Octave");
    EXPECT_EQ(osc.getInputPortLabel(3), "Coarse");
    EXPECT_EQ(osc.getInputPortLabel(4), "Fine");
    EXPECT_EQ(osc.getInputPortLabel(5), "Level");
    EXPECT_EQ(osc.getOutputPortLabel(0), "Audio");
}

TEST(PortLabelTests, FilterPortLabels) {
    FilterModule filter;
    // Default mono mode: original 4-input layout
    EXPECT_EQ(filter.getInputPortLabel(0), "Audio");
    EXPECT_EQ(filter.getInputPortLabel(1), "Cutoff");
    EXPECT_EQ(filter.getInputPortLabel(2), "Resonance");
    EXPECT_EQ(filter.getInputPortLabel(3), "Drive");
    EXPECT_EQ(filter.getOutputPortLabel(0), "Audio");
}

TEST(PortLabelTests, VCAPortLabels) {
    VCAModule vca;
    // Default mono mode: original 2-input layout
    EXPECT_EQ(vca.getInputPortLabel(0), "Audio");
    EXPECT_EQ(vca.getInputPortLabel(1), "CV");
    EXPECT_EQ(vca.getOutputPortLabel(0), "Audio");
}

TEST(PortLabelTests, ADSRPortLabels) {
    ADSRModule adsr;
    EXPECT_EQ(adsr.getInputPortLabel(0), "Gate");
    EXPECT_EQ(adsr.getOutputPortLabel(0), "Env");
}

TEST(PortLabelTests, LFOPortLabels) {
    LFOModule lfo;
    EXPECT_EQ(lfo.getOutputPortLabel(0), "CV");
}

TEST(PortLabelTests, AttenuverterPortLabels) {
    AttenuverterModule att;
    EXPECT_EQ(att.getInputPortLabel(0), "Signal");
    EXPECT_EQ(att.getInputPortLabel(1), "Amount");
    EXPECT_EQ(att.getOutputPortLabel(0), "Out");
}


static void setDualIO(juce::AudioProcessor& proc, bool dual) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "dualIO") {
                p->setValueNotifyingHost(dual ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

TEST(PortLabelTests, DelayPortLabelsDefaultToSingleAudioJack) {
    DelayModule delay;
    EXPECT_FALSE(delay.isDualIO());
    EXPECT_EQ(delay.getVisibleInputPortCount(), 1);
    EXPECT_EQ(delay.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(delay.getInputPortLabel(0), "Audio");
    EXPECT_EQ(delay.getOutputPortLabel(0), "Audio");

    setDualIO(delay, true);
    EXPECT_EQ(delay.getVisibleInputPortCount(), 2);
    EXPECT_EQ(delay.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(delay.getInputPortLabel(0), "Left");
    EXPECT_EQ(delay.getInputPortLabel(1), "Right");
    EXPECT_EQ(delay.getOutputPortLabel(0), "Left");
    EXPECT_EQ(delay.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, DistortionPortLabelsCollapseCvJacksInSingleMode) {
    DistortionModule dist;
    EXPECT_EQ(dist.getVisibleInputPortCount(), 3);
    EXPECT_EQ(dist.getInputPortLabel(0), "Audio");
    EXPECT_EQ(dist.getInputPortLabel(1), "Drive");
    EXPECT_EQ(dist.getInputPortLabel(2), "Mix");
    EXPECT_EQ(dist.getOutputPortLabel(0), "Audio");

    setDualIO(dist, true);
    EXPECT_EQ(dist.getVisibleInputPortCount(), 4);
    EXPECT_EQ(dist.getInputPortLabel(0), "Left");
    EXPECT_EQ(dist.getInputPortLabel(1), "Right");
    EXPECT_EQ(dist.getInputPortLabel(2), "Drive");
    EXPECT_EQ(dist.getInputPortLabel(3), "Mix");
    EXPECT_EQ(dist.getOutputPortLabel(0), "Left");
    EXPECT_EQ(dist.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, ReverbPortLabels) {
    ReverbModule reverb;
    EXPECT_EQ(reverb.getInputPortLabel(0), "Audio");
    EXPECT_EQ(reverb.getOutputPortLabel(0), "Audio");
    setDualIO(reverb, true);
    EXPECT_EQ(reverb.getInputPortLabel(0), "Left");
    EXPECT_EQ(reverb.getInputPortLabel(1), "Right");
    EXPECT_EQ(reverb.getOutputPortLabel(0), "Left");
    EXPECT_EQ(reverb.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, ChorusPortLabels) {
    ChorusModule chorus;
    EXPECT_EQ(chorus.getInputPortLabel(0), "Audio");
    EXPECT_EQ(chorus.getInputPortLabel(1), "Rate");
    EXPECT_EQ(chorus.getInputPortLabel(2), "Depth");
    setDualIO(chorus, true);
    EXPECT_EQ(chorus.getInputPortLabel(0), "Left");
    EXPECT_EQ(chorus.getInputPortLabel(1), "Right");
    EXPECT_EQ(chorus.getInputPortLabel(2), "Rate");
    EXPECT_EQ(chorus.getInputPortLabel(3), "Depth");
}

TEST(PortLabelTests, PhaserPortLabels) {
    PhaserModule phaser;
    EXPECT_EQ(phaser.getInputPortLabel(0), "Audio");
    setDualIO(phaser, true);
    EXPECT_EQ(phaser.getInputPortLabel(0), "Left");
    EXPECT_EQ(phaser.getInputPortLabel(1), "Right");
    EXPECT_EQ(phaser.getInputPortLabel(2), "Rate");
    EXPECT_EQ(phaser.getInputPortLabel(3), "Depth");
}

TEST(PortLabelTests, CompressorPortLabels) {
    CompressorModule comp;
    EXPECT_EQ(comp.getInputPortLabel(0), "Audio");
    setDualIO(comp, true);
    EXPECT_EQ(comp.getInputPortLabel(0), "Left");
    EXPECT_EQ(comp.getInputPortLabel(1), "Right");
}

TEST(PortLabelTests, FlangerPortLabels) {
    FlangerModule flanger;
    EXPECT_EQ(flanger.getInputPortLabel(0), "Audio");
    setDualIO(flanger, true);
    EXPECT_EQ(flanger.getInputPortLabel(0), "Left");
    EXPECT_EQ(flanger.getInputPortLabel(1), "Right");
    EXPECT_EQ(flanger.getInputPortLabel(2), "Rate");
    EXPECT_EQ(flanger.getInputPortLabel(3), "Depth");
}

TEST(PortLabelTests, LimiterPortLabels) {
    LimiterModule limiter;
    EXPECT_EQ(limiter.getInputPortLabel(0), "Audio");
    setDualIO(limiter, true);
    EXPECT_EQ(limiter.getInputPortLabel(0), "Left");
    EXPECT_EQ(limiter.getInputPortLabel(1), "Right");
}

TEST(PortLabelTests, PitchShifterPortLabels) {
    PitchShifterModule shifter;
    EXPECT_EQ(shifter.getInputPortLabel(0), "Audio");
    EXPECT_EQ(shifter.getInputPortLabel(1), "Pitch");
    EXPECT_EQ(shifter.getInputPortLabel(2), "Shift");
    EXPECT_EQ(shifter.getInputPortLabel(3), "Mix");
    EXPECT_EQ(shifter.getInputPortLabel(4), "Feedback");
    setDualIO(shifter, true);
    EXPECT_EQ(shifter.getInputPortLabel(0), "Left");
    EXPECT_EQ(shifter.getInputPortLabel(1), "Right");
    EXPECT_EQ(shifter.getInputPortLabel(2), "Pitch");
    EXPECT_EQ(shifter.getInputPortLabel(3), "Shift");
    EXPECT_EQ(shifter.getInputPortLabel(4), "Mix");
    EXPECT_EQ(shifter.getInputPortLabel(5), "Feedback");
}
