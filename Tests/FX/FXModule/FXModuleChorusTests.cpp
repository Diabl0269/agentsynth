// FXModuleChorusTests.cpp — Chorus module coverage
#include "../../ClickBoundHelpers.h"
#include "Modules/FX/ChorusModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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
// Click bound: a jump of Centre Delay
// ---------------------------------------------------------------------------

using namespace clickbound;

// Regression test for FRO1022: Centre Delay reached the delay line once per block, so a jump of the knob
// moved the read head by a whole block's share of the glide at every block edge.
TEST(ChorusClickBound, CentreDelayJumpGlidesInsteadOfStepping) {
    for (const bool up : {true, false}) {
        ChorusModule m;
        setActual(m, "centreDelay", up ? 1.0f : 30.0f);
        const auto steps = measureEdge(m, {}, {0, 1}, [&] { setActual(m, "centreDelay", up ? 30.0f : 1.0f); });
        EXPECT_NO_CLICK(steps) << (up ? "minimum to maximum" : "maximum to minimum");
    }
}
