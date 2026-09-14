// FXModuleBitcrusherTests.cpp — Bitcrusher module coverage
#include "Modules/FX/BitcrusherModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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
