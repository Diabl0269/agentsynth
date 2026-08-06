#include "../Source/Modules/FX/BitcrusherModule.h"
#include <gtest/gtest.h>

TEST(BitcrusherModuleTest, ModuleTypeAndCategoryAreCorrect) {
    BitcrusherModule module;
    EXPECT_EQ(module.getModuleType(), ModuleType::Bitcrusher);
    EXPECT_EQ(module.getModulationCategory(), ModulationCategory::FX);
    EXPECT_EQ(module.getName(), "Bitcrusher");
}

TEST(BitcrusherModuleTest, PortLabelsAreCorrect) {
    BitcrusherModule module;
    EXPECT_EQ(module.getInputPortLabel(0), "Left");
    EXPECT_EQ(module.getInputPortLabel(1), "Right");
    EXPECT_EQ(module.getInputPortLabel(2), "Rate");
    EXPECT_EQ(module.getInputPortLabel(3), "Depth");
    EXPECT_EQ(module.getInputPortLabel(4), "Mix");
    EXPECT_EQ(module.getOutputPortLabel(0), "Left");
    EXPECT_EQ(module.getOutputPortLabel(1), "Right");

    auto targets = module.getModulationTargets();
    ASSERT_EQ(targets.size(), 3u);
    EXPECT_EQ(targets[0].name, "Rate");
    EXPECT_EQ(targets[0].channelIndex, 2);
    EXPECT_EQ(targets[1].name, "Depth");
    EXPECT_EQ(targets[1].channelIndex, 3);
    EXPECT_EQ(targets[2].name, "Mix");
    EXPECT_EQ(targets[2].channelIndex, 4);
}

TEST(BitcrusherModuleTest, QuantizationAndRoundingWithoutBias) {
    BitcrusherModule module;
    module.prepareToPlay(44100.0, 512);

    // Set rate=1 (no rate reduction), depth=2 (steps = 2^2 = 4 levels: -1.0, -0.5, 0.0, 0.5, 1.0), mix=1.0
    auto params = module.getParameters();
    for (auto* p : params) {
        if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p)) {
            if (fp->paramID == "rate")
                fp->setValueNotifyingHost(0.0f); // 1.0
            else if (fp->paramID == "depth")
                fp->setValueNotifyingHost(1.0f / 23.0f); // 2.0 bits
            else if (fp->paramID == "mix")
                fp->setValueNotifyingHost(1.0f); // 1.0
            else if (fp->paramID == "dither")
                fp->setValueNotifyingHost(0.0f); // 0.0
        }
    }

    juce::AudioBuffer<float> buffer(5, 512);
    buffer.clear();
    // Fill input with 0.4f. 0.4 * 4 = 1.6, round(1.6) = 2.0, 2.0 / 4 = 0.5f.
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.4f);
        buffer.setSample(1, i, 0.4f);
    }

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // After smoothing catches up, output should be exactly 0.5f (nearest quantized level), NOT 0.25f (floor)
    EXPECT_NEAR(buffer.getSample(0, 500), 0.5f, 0.01f);
}

TEST(BitcrusherModuleTest, DownsamplingAndHold) {
    BitcrusherModule module;

    // Set rate=4 (hold each sample for 4 clocks), depth=24 (full depth), mix=1.0 BEFORE prepareToPlay
    auto params = module.getParameters();
    for (auto* p : params) {
        if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p)) {
            if (fp->paramID == "rate") fp->setValueNotifyingHost(3.0f / 49.0f); // rate = 4
            else if (fp->paramID == "depth") fp->setValueNotifyingHost(1.0f); // 24
            else if (fp->paramID == "mix") fp->setValueNotifyingHost(1.0f); // 1.0
        }
    }

    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(5, 64);
    buffer.clear();
    for (int i = 0; i < 64; ++i) {
        float val = static_cast<float>(i) / 64.0f;
        buffer.setSample(0, i, val);
        buffer.setSample(1, i, val);
    }

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // With rate=4 and phase starting at 0, update occurs at sample 3.
    // Samples 3, 4, 5, 6 should all hold the value sampled at index 3.
    float heldVal = buffer.getSample(0, 3);
    EXPECT_EQ(buffer.getSample(0, 4), heldVal);
    EXPECT_EQ(buffer.getSample(0, 5), heldVal);
    EXPECT_EQ(buffer.getSample(0, 6), heldVal);
}

TEST(BitcrusherModuleTest, RateCVChangeNoGlitch) {
    BitcrusherModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(5, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, static_cast<float>(i) / 512.0f);
        // Supply negative CV to drop rate rapidly
        buffer.setSample(2, i, (i > 200) ? -0.5f : 0.5f);
    }

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module.processBlock(buffer, midi));
}

TEST(BitcrusherModuleTest, DryWetMix) {
    BitcrusherModule module;
    module.prepareToPlay(44100.0, 512);

    // Set mix = 0.0 (100% dry)
    auto params = module.getParameters();
    for (auto* p : params) {
        if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p)) {
            if (fp->paramID == "mix")
                fp->setValueNotifyingHost(0.0f);
        }
    }

    juce::AudioBuffer<float> buffer(5, 128);
    buffer.clear();
    for (int i = 0; i < 128; ++i) {
        buffer.setSample(0, i, 0.333f);
        buffer.setSample(1, i, 0.333f);
    }

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // Dry signal passes unchanged
    EXPECT_NEAR(buffer.getSample(0, 100), 0.333f, 1e-4f);
}

TEST(BitcrusherModuleTest, DitherAddsNoise) {
    BitcrusherModule module;
    module.prepareToPlay(44100.0, 512);

    // Enable dither
    auto params = module.getParameters();
    for (auto* p : params) {
        if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p)) {
            if (fp->paramID == "dither")
                fp->setValueNotifyingHost(1.0f);
        }
    }

    juce::AudioBuffer<float> buffer(5, 512);
    buffer.clear();

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    float rms = buffer.getRMSLevel(0, 0, 512);
    EXPECT_GT(rms, 0.0f);
}

TEST(BitcrusherModuleTest, ZeroChannelsDoesNotCrash) {
    BitcrusherModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> emptyBuffer(0, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module.processBlock(emptyBuffer, midi));
}
