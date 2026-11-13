// FXModuleCompressorTests.cpp — Compressor module coverage
#include "../../ClickBoundHelpers.h"
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

// ---------------------------------------------------------------------------
// Click bound: jumps of Threshold, Ratio and Attack
// ---------------------------------------------------------------------------

namespace {

using namespace clickbound;

/** Renders a compressor with `id` jumped between its extremes and checks the output stays within the bound. */
void expectJumpWithoutClick(const char* id, bool up) {
    CompressorModule m;
    setNormalised(m, id, up ? 0.0f : 1.0f);
    const auto steps = measureEdge(m, {}, {0, 1}, [&] { setNormalised(m, id, up ? 1.0f : 0.0f); });
    EXPECT_NO_CLICK(steps) << id << (up ? " minimum to maximum" : " maximum to minimum");
}

} // namespace

// Regression test for FRO1022: Threshold and Ratio reached the gain law once per block, so the gain stepped.
TEST(CompressorClickBound, ThresholdJumpRampsTheGainPerSample) {
    expectJumpWithoutClick("threshold", true);
    expectJumpWithoutClick("threshold", false);
}

TEST(CompressorClickBound, RatioJumpRampsTheGainPerSample) {
    expectJumpWithoutClick("ratio", true);
    expectJumpWithoutClick("ratio", false);
}

// Regression test for FRO1022: Attack stepped the detector coefficient in one sample.
TEST(CompressorClickBound, AttackJumpRetunesTheDetectorGradually) {
    expectJumpWithoutClick("attack", true);
    expectJumpWithoutClick("attack", false);
    expectJumpWithoutClick("release", true);
    expectJumpWithoutClick("release", false);
}
