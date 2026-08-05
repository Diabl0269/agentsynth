#include <gtest/gtest.h>
#include "../Source/Modules/NoiseModule.h"
#include "../Source/AudioEngine.h"
#include <cmath>

class NoiseModuleTest : public ::testing::Test {
protected:
    std::unique_ptr<NoiseModule> module;

    void SetUp() override {
        module = std::make_unique<NoiseModule>();
        module->prepareToPlay(44100.0, 512);
    }
};

TEST_F(NoiseModuleTest, FactoryInitialisation) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Noise);
    EXPECT_EQ(module->getName(), "Noise");
    EXPECT_EQ(module->getParameters().size(), 6); // Bypassed, NoiseType, Color, Level, Poly, Mute
}

TEST_F(NoiseModuleTest, InitialParameterValues) {
    auto* noiseType = module->getParameters()[1];
    auto* color = module->getParameters()[2];
    auto* level = module->getParameters()[3];

    EXPECT_EQ(noiseType->getValue(), 0.0f); // White noise
    EXPECT_EQ(color->getValue(), 0.5f); // Color = 0 (Center)
    EXPECT_FLOAT_EQ(level->getValue(), 1.0f); // Level = 1.0
}

TEST_F(NoiseModuleTest, ColorParameterMapping) {
    auto* colorParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[2]);
    
    // Test center
    colorParam->setValueNotifyingHost(0.5f);
    EXPECT_NEAR(colorParam->get(), 0.0f, 1e-5f);
    
    // Test min
    colorParam->setValueNotifyingHost(0.0f);
    EXPECT_FLOAT_EQ(colorParam->get(), -1.0f);
    
    // Test max
    colorParam->setValueNotifyingHost(1.0f);
    EXPECT_FLOAT_EQ(colorParam->get(), 1.0f);
}

TEST_F(NoiseModuleTest, NoiseTypeParameterMapping) {
    auto* noiseTypeParam = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    
    // Test White
    noiseTypeParam->setValueNotifyingHost(0.0f);
    EXPECT_EQ(noiseTypeParam->getIndex(), 0); // choice 0 is White
    
    // Test Pink
    noiseTypeParam->setValueNotifyingHost(0.5f);
    EXPECT_EQ(noiseTypeParam->getIndex(), 1); // choice 1 is Pink
    
    // Test Brown
    noiseTypeParam->setValueNotifyingHost(1.0f);
    EXPECT_EQ(noiseTypeParam->getIndex(), 2); // choice 2 is Brown
}

TEST_F(NoiseModuleTest, ProcessBlockProducesOutput) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midiMessages;

    // Ensure level is up
    module->getParameters()[3]->setValueNotifyingHost(1.0f); // Level max

    module->processBlock(buffer, midiMessages);

    // Output should not be entirely silent
    bool hasAudio = false;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        if (std::abs(buffer.getSample(0, i)) > 0.0001f || std::abs(buffer.getSample(1, i)) > 0.0001f) {
            hasAudio = true;
            break;
        }
    }
    
    EXPECT_TRUE(hasAudio);
}

TEST_F(NoiseModuleTest, MutedOutputIsSilent) {
    juce::AudioBuffer<float> buffer(2, 512);
    // Add some garbage to ensure it gets cleared
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 512; ++i) {
            buffer.setSample(ch, i, 0.5f);
        }
    }
    juce::MidiBuffer midiMessages;

    module->setMuted(true);
    module->processBlock(buffer, midiMessages);

    bool isSilent = true;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (buffer.getSample(ch, i) != 0.0f) {
                isSilent = false;
                break;
            }
        }
    }
    
    EXPECT_TRUE(isSilent);
}

TEST_F(NoiseModuleTest, BypassedOutputIsSilent) {
    // Noise module is a source, so bypass should mute it
    juce::AudioBuffer<float> buffer(2, 512);
    // Add some garbage to ensure it gets cleared
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 512; ++i) {
            buffer.setSample(ch, i, 0.5f);
        }
    }
    juce::MidiBuffer midiMessages;

    module->setBypassed(true);
    module->processBlock(buffer, midiMessages);

    bool isSilent = true;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (buffer.getSample(ch, i) != 0.0f) {
                isSilent = false;
                break;
            }
        }
    }
    
    EXPECT_TRUE(isSilent);
}
