// The level maths behind `audioPump {levels: ...}`: RMS and peak of rendered blocks, and the verdict on "louder as
// velocity goes up" (docs/development/test-mode.md#audio-pump).

#include "TestMode/TestModeLevel.h"
#include "TestModeRig.h"
#include <gtest/gtest.h>

using namespace synth::testmode;
using testmode_rig::args;
using testmode_rig::run;

namespace {

juce::AudioBuffer<float> constantBlock(float value) {
    juce::AudioBuffer<float> block(2, 64);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill(block.getWritePointer(ch), value, 64);
    return block;
}

} // namespace

TEST(TestModeLevel, MeterReportsRmsAndPeakOverEveryBlockAndChannel) {
    LevelMeter meter;
    EXPECT_DOUBLE_EQ(meter.reading().rms, 0.0);
    meter.add(constantBlock(0.5f));
    meter.add(constantBlock(-0.5f));
    EXPECT_NEAR(meter.reading().rms, 0.5, 1e-9);
    EXPECT_NEAR(meter.reading().peak, 0.5, 1e-9);
    meter.add(constantBlock(0.0f));
    EXPECT_NEAR(meter.reading().rms, 0.5 * std::sqrt(2.0 / 3.0), 1e-9);
}

TEST(TestModeLevel, RisingVerdictAcceptsAStrictlyLouderRun) {
    EXPECT_TRUE(risingVerdict({1, 40, 63, 127}, {0.001, 0.02, 0.05, 0.2}, 1e-4).empty());
}

TEST(TestModeLevel, RisingVerdictNamesASilentNote) {
    const auto verdict = risingVerdict({1, 40}, {0.0, 0.2}, 1e-4);
    EXPECT_NE(verdict.find("velocity 1 is silent"), std::string::npos) << verdict;
}

TEST(TestModeLevel, RisingVerdictNamesANoteThatIsNotLouder) {
    const auto flat = risingVerdict({1, 40, 63}, {0.01, 0.2, 0.2}, 1e-4);
    EXPECT_NE(flat.find("velocity 63"), std::string::npos) << flat;
    const auto quieter = risingVerdict({1, 40}, {0.2, 0.1}, 1e-4);
    EXPECT_NE(quieter.find("velocity 40"), std::string::npos) << quieter;
}

TEST(TestModeLevel, LevelsRefuseWithoutTheAppsMainWindow) {
    const auto r = run("audioPump", args({{"levels", juce::var(new juce::DynamicObject())}}));
    EXPECT_EQ(r.state, StepResult::State::failed);
}
