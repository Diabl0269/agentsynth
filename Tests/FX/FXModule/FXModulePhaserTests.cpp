// FXModulePhaserTests.cpp — Phaser module coverage
#include "Modules/FX/PhaserModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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

TEST(PhaserCentreFreqKnob, MovesEqualOctavesPerEqualTravelAndKeepsItsHertz) {
    PhaserModule phaser;
    juce::AudioParameterFloat* centre = nullptr;
    for (auto* p : phaser.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p); f != nullptr && f->getParameterID() == "centreFreq")
            centre = f;
    ASSERT_NE(centre, nullptr);
    EXPECT_FLOAT_EQ(centre->get(), 1300.0f) << "the default sound does not move";
    EXPECT_FLOAT_EQ(centre->convertFrom0to1(0.0f), 200.0f);
    EXPECT_FLOAT_EQ(centre->convertFrom0to1(1.0f), 10000.0f);
    for (int step = 0; step < 10; ++step) {
        const double low = centre->convertFrom0to1((float)step / 10.0f);
        const double high = centre->convertFrom0to1((float)(step + 1) / 10.0f);
        EXPECT_NEAR(std::log2(high / low), std::log2(50.0) / 10.0, 0.01) << "step " << step;
    }
    centre->setValueNotifyingHost(centre->convertTo0to1(1234.0f));
    EXPECT_NEAR(centre->get(), 1234.0f, 0.01f);
}
