// FXModuleFlangerTests.cpp — Flanger module coverage
#include "../../ClickBoundHelpers.h"
#include "Modules/FX/FlangerModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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
// Click bound: a jump of Centre Delay
// ---------------------------------------------------------------------------

using namespace clickbound;

// Regression test for FRO1022: Centre Delay reached the delay line once per block, so a jump of the knob
// moved the read head by a whole block's share of the glide at every block edge.
TEST(FlangerClickBound, CentreDelayJumpGlidesInsteadOfStepping) {
    for (const bool up : {true, false}) {
        FlangerModule m;
        setActual(m, "centreDelay", up ? 1.0f : 5.0f);
        const auto steps = measureEdge(m, {}, {0, 1}, [&] { setActual(m, "centreDelay", up ? 5.0f : 1.0f); });
        EXPECT_NO_CLICK(steps) << (up ? "minimum to maximum" : "maximum to minimum");
    }
}

// Regression test for FRO1022: the delay line is not run while muted or bypassed, so coming back played
// echoes of what it heard before, stepping against the live signal.
TEST(FlangerClickBound, ComingBackFromBypassOrMuteStartsFromAnEmptyLine) {
    for (const char* flag : {"bypassed", "muted"}) {
        FlangerModule m;
        setNormalised(m, flag, 1.0f);
        const auto steps = measureEdge(m, {}, {0, 1}, [&] { setNormalised(m, flag, 0.0f); });
        EXPECT_NO_CLICK(steps) << flag;
    }
}
