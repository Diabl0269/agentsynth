// FXModuleCompressorTests.cpp — Compressor module coverage
#include "Modules/FX/CompressorModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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
