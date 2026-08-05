#include "../Source/Modules/NoiseModule.h"
#include <cmath>
#include <gtest/gtest.h>

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

    EXPECT_EQ(noiseType->getValue(), 0.0f);   // White noise
    EXPECT_EQ(color->getValue(), 0.5f);       // Color = 0 (Center)
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

TEST_F(NoiseModuleTest, ZeroChannelsDoesNotCrash) {
    juce::AudioBuffer<float> emptyBuffer(0, 512);
    juce::MidiBuffer midiMessages;
    EXPECT_NO_THROW(module->processBlock(emptyBuffer, midiMessages));
}

TEST_F(NoiseModuleTest, ProcessBlockProducesOutputWhitePinkBrown) {
    juce::MidiBuffer midiMessages;
    auto* noiseTypeParam = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);

    for (int typeIndex = 0; typeIndex < 3; ++typeIndex) {
        noiseTypeParam->setValueNotifyingHost(typeIndex / 2.0f);
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        module->processBlock(buffer, midiMessages);

        bool hasAudio = false;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (std::abs(buffer.getSample(0, i)) > 0.0001f) {
                hasAudio = true;
                break;
            }
        }
        EXPECT_TRUE(hasAudio) << "Type index " << typeIndex << " produced silence";
    }
}

TEST_F(NoiseModuleTest, ColorFiltersLowPassAndHighPass) {
    juce::MidiBuffer midiMessages;
    auto* colorParam = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[2]);

    // Low pass (color = -1.0)
    colorParam->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> lpBuffer(2, 512);
    lpBuffer.clear();
    module->processBlock(lpBuffer, midiMessages);

    bool hasLPAudio = false;
    for (int i = 0; i < lpBuffer.getNumSamples(); ++i) {
        if (std::abs(lpBuffer.getSample(0, i)) > 0.0001f) {
            hasLPAudio = true;
            break;
        }
    }
    EXPECT_TRUE(hasLPAudio);

    // High pass (color = +1.0)
    colorParam->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> hpBuffer(2, 512);
    hpBuffer.clear();
    module->processBlock(hpBuffer, midiMessages);

    bool hasHPAudio = false;
    for (int i = 0; i < hpBuffer.getNumSamples(); ++i) {
        if (std::abs(hpBuffer.getSample(0, i)) > 0.0001f) {
            hasHPAudio = true;
            break;
        }
    }
    EXPECT_TRUE(hasHPAudio);
}

TEST_F(NoiseModuleTest, PolyModeProducesMultiChannelOutput) {
    auto* polyParam = dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[4]);
    polyParam->setValueNotifyingHost(1.0f); // Enable poly

    juce::AudioBuffer<float> buffer(8, 512);
    buffer.clear();
    juce::MidiBuffer midiMessages;

    module->processBlock(buffer, midiMessages);

    for (int ch = 0; ch < 8; ++ch) {
        bool voiceHasAudio = false;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (std::abs(buffer.getSample(ch, i)) > 0.0001f) {
                voiceHasAudio = true;
                break;
            }
        }
        EXPECT_TRUE(voiceHasAudio) << "Voice channel " << ch << " was silent in Poly mode";
    }
}

TEST_F(NoiseModuleTest, CVModulationMonoAndPoly) {
    juce::MidiBuffer midiMessages;

    // Mono CV on channel 8
    juce::AudioBuffer<float> monoBuffer(10, 512);
    monoBuffer.clear();
    for (int i = 0; i < 512; ++i)
        monoBuffer.setSample(8, i, 0.5f); // Color CV = +0.5

    module->processBlock(monoBuffer, midiMessages);
    EXPECT_TRUE(std::abs(monoBuffer.getSample(0, 0)) > 0.0001f);

    // Poly CV on channel 8
    auto* polyParam = dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[4]);
    polyParam->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> polyBuffer(10, 512);
    polyBuffer.clear();
    for (int i = 0; i < 512; ++i)
        polyBuffer.setSample(8, i, -0.5f); // Color CV = -0.5

    module->processBlock(polyBuffer, midiMessages);
    EXPECT_TRUE(std::abs(polyBuffer.getSample(0, 0)) > 0.0001f);
}

class NoiseModuleMuteBypassTest
    : public NoiseModuleTest
    , public ::testing::WithParamInterface<bool> {};

TEST_P(NoiseModuleMuteBypassTest, OutputIsSilentWhenMutedOrBypassed) {
    juce::AudioBuffer<float> buffer(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midiMessages;
    bool isMuteTest = GetParam();
    if (isMuteTest)
        module->setMuted(true);
    else
        module->setBypassed(true);

    module->processBlock(buffer, midiMessages);

    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            EXPECT_EQ(buffer.getSample(ch, i), 0.0f);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(MuteAndBypass, NoiseModuleMuteBypassTest, ::testing::Values(true, false));
