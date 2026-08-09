#include "../Source/Modules/MathModule.h"
#include <cmath>
#include <gtest/gtest.h>

class MathModuleTest : public ::testing::Test {
protected:
    std::unique_ptr<MathModule> module;

    void SetUp() override {
        module = std::make_unique<MathModule>();
        module->prepareToPlay(44100.0, 512);
    }
};

// ============================================================================
// Basic identity / metadata
// ============================================================================

TEST_F(MathModuleTest, InitialState) {
    EXPECT_EQ(module->getName(), "Math");
    EXPECT_EQ(module->getModuleType(), ModuleType::Math);
    EXPECT_EQ(module->getTotalNumInputChannels(), 2);
    EXPECT_EQ(module->getTotalNumOutputChannels(), 5);
    EXPECT_FALSE(module->isBypassed());

    ASSERT_EQ(module->getParameters().size(), 3); // Bypassed (base), Clip (local), Muted (local)
    auto* clip = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->getIndex(), 0); // Default = "Off"
}

// ============================================================================
// Core arithmetic (clip Off, constant inputs)
// ============================================================================

TEST_F(MathModuleTest, ComputesAllFourOperations) {
    juce::AudioBuffer<float> buffer(5, 4);
    buffer.clear();
    for (int i = 0; i < 4; ++i) {
        buffer.setSample(0, i, 0.6f); // A
        buffer.setSample(1, i, 0.2f); // B
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), 0.8f, 1e-5f) << "Sum sample " << i;
        EXPECT_NEAR(buffer.getSample(1, i), 0.4f, 1e-5f) << "Diff sample " << i;
        EXPECT_NEAR(buffer.getSample(2, i), 0.2f, 1e-5f) << "Min sample " << i;
        EXPECT_NEAR(buffer.getSample(3, i), 0.6f, 1e-5f) << "Max sample " << i;
        EXPECT_NEAR(buffer.getSample(4, i), 0.12f, 1e-5f) << "Product sample " << i;
    }
}

TEST_F(MathModuleTest, NegativeInputsMinMaxOrdering) {
    juce::AudioBuffer<float> buffer(5, 4);
    buffer.clear();
    for (int i = 0; i < 4; ++i) {
        buffer.setSample(0, i, -0.5f); // A
        buffer.setSample(1, i, 0.25f); // B
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), -0.25f, 1e-5f) << "Sum sample " << i;
        EXPECT_NEAR(buffer.getSample(1, i), -0.75f, 1e-5f) << "Diff sample " << i;
        EXPECT_NEAR(buffer.getSample(2, i), -0.5f, 1e-5f) << "Min sample " << i;
        EXPECT_NEAR(buffer.getSample(3, i), 0.25f, 1e-5f) << "Max sample " << i;
        EXPECT_NEAR(buffer.getSample(4, i), -0.125f, 1e-5f) << "Product sample " << i;
    }
}

TEST_F(MathModuleTest, PerSampleVaryingInputs) {
    // Deliberately varying per-sample values (not constants) so that a bug where the
    // implementation writes the Sum into ch0 (aliased with input A) before other outputs
    // read the original A value would be caught: with constant inputs some such bugs could
    // still coincidentally pass, but per-sample variation exposes ordering mistakes clearly.
    const int numSamples = 8;
    const float aVals[numSamples] = {0.5f, -0.3f, 0.0f, 0.9f, -0.9f, 0.25f, -0.6f, 0.1f};
    const float bVals[numSamples] = {0.2f, -0.7f, 0.4f, -0.1f, -0.2f, 0.25f, 0.6f, -0.1f};
    const float expSum[numSamples] = {0.7f, -1.0f, 0.4f, 0.8f, -1.1f, 0.5f, 0.0f, 0.0f};
    const float expDiff[numSamples] = {0.3f, 0.4f, -0.4f, 1.0f, -0.7f, 0.0f, -1.2f, 0.2f};
    const float expMin[numSamples] = {0.2f, -0.7f, 0.0f, -0.1f, -0.9f, 0.25f, -0.6f, -0.1f};
    const float expMax[numSamples] = {0.5f, -0.3f, 0.4f, 0.9f, -0.2f, 0.25f, 0.6f, 0.1f};
    const float expMult[numSamples] = {0.10f, 0.21f, 0.0f, -0.09f, 0.18f, 0.0625f, -0.36f, -0.01f};

    juce::AudioBuffer<float> buffer(5, numSamples);
    buffer.clear();
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, aVals[i]);
        buffer.setSample(1, i, bVals[i]);
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < numSamples; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), expSum[i], 1e-5f) << "Sum sample " << i;
        EXPECT_NEAR(buffer.getSample(1, i), expDiff[i], 1e-5f) << "Diff sample " << i;
        EXPECT_NEAR(buffer.getSample(2, i), expMin[i], 1e-5f) << "Min sample " << i;
        EXPECT_NEAR(buffer.getSample(3, i), expMax[i], 1e-5f) << "Max sample " << i;
        EXPECT_NEAR(buffer.getSample(4, i), expMult[i], 1e-5f) << "Product sample " << i;
    }
}

// ============================================================================
// Clip modes
// ============================================================================

TEST_F(MathModuleTest, ClipOffAllowsOutOfRangeSum) {
    auto* clip = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    ASSERT_NE(clip, nullptr);
    clip->setValueNotifyingHost(0.0f); // index 0 = "Off"

    juce::AudioBuffer<float> buffer(5, 4);
    buffer.clear();
    for (int i = 0; i < 4; ++i) {
        buffer.setSample(0, i, 0.9f);
        buffer.setSample(1, i, 0.9f);
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(buffer.getSample(0, i), 1.8f, 1e-5f) << "Sum sample " << i << " should not be clamped";
}

TEST_F(MathModuleTest, ClipHardLimitsToUnitRange) {
    auto* clip = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    ASSERT_NE(clip, nullptr);
    clip->setValueNotifyingHost(0.5f); // index 1 = "Hard" (3 choices -> 0, 0.5, 1.0)
    ASSERT_EQ(clip->getIndex(), 1);

    juce::AudioBuffer<float> buffer(5, 4);
    buffer.clear();
    for (int i = 0; i < 4; ++i) {
        buffer.setSample(0, i, 0.9f);
        buffer.setSample(1, i, 0.9f);
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(buffer.getSample(0, i), 1.0f, 1e-5f) << "Positive sum sample " << i;

    // Negative case
    juce::AudioBuffer<float> negBuffer(5, 4);
    negBuffer.clear();
    for (int i = 0; i < 4; ++i) {
        negBuffer.setSample(0, i, -0.9f);
        negBuffer.setSample(1, i, -0.9f);
    }
    module->processBlock(negBuffer, midi);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(negBuffer.getSample(0, i), -1.0f, 1e-5f) << "Negative sum sample " << i;
}

TEST_F(MathModuleTest, ClipSoftSaturatesWithTanh) {
    auto* clip = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    ASSERT_NE(clip, nullptr);
    clip->setValueNotifyingHost(1.0f); // index 2 = "Soft"
    ASSERT_EQ(clip->getIndex(), 2);

    juce::AudioBuffer<float> buffer(5, 4);
    buffer.clear();
    for (int i = 0; i < 4; ++i) {
        buffer.setSample(0, i, 0.9f);
        buffer.setSample(1, i, 0.9f);
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    const float expected = std::tanh(1.8f);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), expected, 1e-4f) << "Sum sample " << i;
        EXPECT_LT(std::abs(buffer.getSample(0, i)), 1.0f) << "Soft clip must not reach +/-1 sample " << i;
    }
}

// ============================================================================
// Unconnected second input (B == 0)
// ============================================================================

TEST_F(MathModuleTest, UnconnectedSecondInputTreatsBAsZero) {
    {
        juce::AudioBuffer<float> buffer(5, 4);
        buffer.clear();
        for (int i = 0; i < 4; ++i)
            buffer.setSample(0, i, 0.5f); // A only, B (ch1) left at 0
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);

        for (int i = 0; i < 4; ++i) {
            EXPECT_NEAR(buffer.getSample(0, i), 0.5f, 1e-5f) << "Sum == A, sample " << i;
            EXPECT_NEAR(buffer.getSample(1, i), 0.5f, 1e-5f) << "Diff == A, sample " << i;
            EXPECT_NEAR(buffer.getSample(2, i), 0.0f, 1e-5f) << "Min(A,0) sample " << i;
            EXPECT_NEAR(buffer.getSample(3, i), 0.5f, 1e-5f) << "Max(A,0) sample " << i;
            EXPECT_NEAR(buffer.getSample(4, i), 0.0f, 1e-5f) << "Product == 0, sample " << i;
        }
    }
    {
        juce::AudioBuffer<float> buffer(5, 4);
        buffer.clear();
        for (int i = 0; i < 4; ++i)
            buffer.setSample(0, i, -0.5f); // A only, B (ch1) left at 0
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);

        for (int i = 0; i < 4; ++i) {
            EXPECT_NEAR(buffer.getSample(0, i), -0.5f, 1e-5f) << "Sum == A, sample " << i;
            EXPECT_NEAR(buffer.getSample(1, i), -0.5f, 1e-5f) << "Diff == A, sample " << i;
            EXPECT_NEAR(buffer.getSample(2, i), -0.5f, 1e-5f) << "Min(A,0) sample " << i;
            EXPECT_NEAR(buffer.getSample(3, i), 0.0f, 1e-5f) << "Max(A,0) sample " << i;
            EXPECT_NEAR(buffer.getSample(4, i), 0.0f, 1e-5f) << "Product == 0, sample " << i;
        }
    }
}

// ============================================================================
// Metadata: port labels, visibility, modulation
// ============================================================================

TEST_F(MathModuleTest, PortLabels) {
    EXPECT_EQ(module->getInputPortLabel(0), "A");
    EXPECT_EQ(module->getInputPortLabel(1), "B");
    EXPECT_EQ(module->getOutputPortLabel(0), "Sum");
    EXPECT_EQ(module->getOutputPortLabel(1), "Diff");
    EXPECT_EQ(module->getOutputPortLabel(2), "Min");
    EXPECT_EQ(module->getOutputPortLabel(3), "Max");
    EXPECT_EQ(module->getOutputPortLabel(4), "Mult");
}

TEST_F(MathModuleTest, VisibleAndModulationMetadata) {
    EXPECT_EQ(module->getVisibleInputPortCount(), 2);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 5);
    EXPECT_TRUE(module->getModulationTargets().empty());
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::Other);
}

// ============================================================================
// Edge cases: empty buffers, fewer channels, large blocks
// ============================================================================

TEST_F(MathModuleTest, HandlesEmptyBuffer) {
    juce::MidiBuffer midi;

    juce::AudioBuffer<float> zeroSamples(5, 0);
    EXPECT_NO_THROW(module->processBlock(zeroSamples, midi));

    juce::AudioBuffer<float> zeroChannels(0, 512);
    EXPECT_NO_THROW(module->processBlock(zeroChannels, midi));
}

TEST_F(MathModuleTest, HandlesFewerChannelsThanOutputs) {
    // Only 2 channels present (A/B), fewer than the 5 declared outputs.
    juce::AudioBuffer<float> smallBuffer(2, 512);
    smallBuffer.clear();
    for (int i = 0; i < 512; ++i) {
        smallBuffer.setSample(0, i, 0.4f);
        smallBuffer.setSample(1, i, 0.1f);
    }
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(smallBuffer, midi));

    for (int i = 0; i < 512; ++i) {
        EXPECT_NEAR(smallBuffer.getSample(0, i), 0.5f, 1e-5f) << "Sum sample " << i;
        EXPECT_NEAR(smallBuffer.getSample(1, i), 0.3f, 1e-5f) << "Diff sample " << i;
    }

    // Large block: confirm there is no internal fixed-size scratch buffer limitation.
    const int largeNumSamples = 8192;
    juce::AudioBuffer<float> largeBuffer(5, largeNumSamples);
    largeBuffer.clear();
    for (int i = 0; i < largeNumSamples; ++i) {
        largeBuffer.setSample(0, i, 0.3f);
        largeBuffer.setSample(1, i, 0.2f);
    }
    EXPECT_NO_THROW(module->processBlock(largeBuffer, midi));

    EXPECT_NEAR(largeBuffer.getSample(0, 0), 0.5f, 1e-5f) << "Sum first sample";
    EXPECT_NEAR(largeBuffer.getSample(0, largeNumSamples - 1), 0.5f, 1e-5f) << "Sum last sample";
    EXPECT_NEAR(largeBuffer.getSample(4, largeNumSamples - 1), 0.06f, 1e-5f) << "Product last sample";
}

// ============================================================================
// State serialization
// ============================================================================

TEST_F(MathModuleTest, ClipParamSerializesInState) {
    auto* clip = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    ASSERT_NE(clip, nullptr);
    clip->setValueNotifyingHost(1.0f); // index 2 = "Soft"
    ASSERT_EQ(clip->getIndex(), 2);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto newModule = std::make_unique<MathModule>();
    EXPECT_EQ(dynamic_cast<juce::AudioParameterChoice*>(newModule->getParameters()[1])->getIndex(), 0)
        << "New module should start at default Off before restoring state";

    newModule->setStateInformation(state.getData(), (int)state.getSize());

    auto* newClip = dynamic_cast<juce::AudioParameterChoice*>(newModule->getParameters()[1]);
    ASSERT_NE(newClip, nullptr);
    EXPECT_EQ(newClip->getIndex(), 2) << "Clip choice should round-trip through state serialization";
}
