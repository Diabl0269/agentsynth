#include "Modules/AttenuverterModule.h"
#include <gtest/gtest.h>

class AttenuverterTests : public ::testing::Test {
protected:
    std::unique_ptr<AttenuverterModule> module;

    void SetUp() override {
        module = std::make_unique<AttenuverterModule>();
        module->prepareToPlay(44100.0, 512); // Initialize module
    }

    void TearDown() override { module.reset(); }
};

TEST_F(AttenuverterTests, InitializationIsCorrect) {
    EXPECT_EQ(module->getName(), "Attenuverter");
    EXPECT_EQ(module->getTotalNumInputChannels(), 1);
    EXPECT_EQ(module->getTotalNumOutputChannels(), 1);

    auto& params = module->getParameters();
    ASSERT_EQ(params.size(), 2); // Bypassed (base), Amount (local)
    EXPECT_EQ(params[0]->getName(100), "Bypassed");
    EXPECT_EQ(params[1]->getName(100), "Amount");
}

TEST_F(AttenuverterTests, ProcessingWithPositiveGain) {
    juce::AudioBuffer<float> buffer(1, 128);
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f); // Input DC 1.0
    }
    juce::MidiBuffer midi;

    // Set gain parameter to 0.5 manually
    // Index 1 is "Amount"
    if (auto* amt = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1])) {
        amt->setValueNotifyingHost(amt->convertTo0to1(0.5f));
    }

    module->processBlock(buffer, midi);

    // After smoothing, the last sample should be close to 0.5
    EXPECT_NEAR(buffer.getSample(0, 127), 0.5f, 0.05f);
}

TEST_F(AttenuverterTests, ProcessingWithNegativeGain) {
    juce::AudioBuffer<float> buffer(1, 128);
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f); // Input DC 1.0
    }
    juce::MidiBuffer midi;

    // Index 1 is "Amount"
    if (auto* amt = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1])) {
        amt->setValueNotifyingHost(amt->convertTo0to1(-0.5f));
    }

    module->processBlock(buffer, midi);

    EXPECT_NEAR(buffer.getSample(0, 127), -0.5f, 0.05f);
}

TEST_F(AttenuverterTests, StateSerialization) {
    // Index 1 is "Amount"
    if (auto* amt = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1])) {
        amt->setValueNotifyingHost(amt->convertTo0to1(0.75f));
    }

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto newModule = std::make_unique<AttenuverterModule>();
    newModule->setStateInformation(state.getData(), (int)state.getSize());

    if (auto* amt = dynamic_cast<juce::AudioParameterFloat*>(newModule->getParameters()[1])) {
        EXPECT_NEAR(amt->get(), 0.75f, 0.001f);
    } else {
        FAIL() << "Parameter not found or of wrong type.";
    }
}
