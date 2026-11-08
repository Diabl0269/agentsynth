// MacroKnobCableTests.cpp
// Every macro knob takes a cable on the input channel of the same number (no jack on the card): the cable moves the
// knob within its own range, live, and the output follows.

#include "Modules/MacroControlModule.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

juce::AudioBuffer<float> run(MacroControlModule& m, int cableChannel, float cv) {
    juce::AudioBuffer<float> buffer(MacroControlModule::kMaxMacros, kBlock);
    juce::MidiBuffer midi;
    for (int block = 0; block < 6; ++block) {
        buffer.clear();
        if (cableChannel >= 0)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(cableChannel), cv, kBlock);
        m.processBlock(buffer, midi);
    }
    return buffer;
}

} // namespace

TEST(MacroKnobCable, EveryKnobIsAKnobOnlyTargetOnTheChannelOfItsNumber) {
    MacroControlModule m;
    const auto targets = m.getModulationTargets();
    ASSERT_EQ((int)targets.size(), MacroControlModule::kMaxMacros);
    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i) {
        EXPECT_EQ(targets[(size_t)i].paramId, "macro" + juce::String(i + 1));
        EXPECT_EQ(targets[(size_t)i].channelIndex, i);
        EXPECT_TRUE(targets[(size_t)i].knobOnly);
    }
}

// Regression test for FRO1556: the macro knobs took no cable.
TEST(MacroKnobCable, ACableMovesItsKnobsOutputAndNoOther) {
    MacroControlModule m;
    m.prepareToPlay(kRate, kBlock);
    const auto still = run(m, -1, 0.0f);
    const float rest = still.getSample(2, kBlock - 1);

    const auto raised = run(m, 2, 0.25f);
    EXPECT_NEAR(raised.getSample(2, kBlock - 1), juce::jlimit(0.0f, 1.0f, rest + 0.25f), 1e-3f);
    EXPECT_GT(raised.getSample(2, kBlock - 1), rest);
    EXPECT_NEAR(raised.getSample(1, kBlock - 1), still.getSample(1, kBlock - 1), 1e-5f);
    EXPECT_NEAR(raised.getSample(3, kBlock - 1), still.getSample(3, kBlock - 1), 1e-5f);

    // The cable never writes the knob: with it gone the output settles back.
    const auto released = run(m, -1, 0.0f);
    EXPECT_NEAR(released.getSample(2, kBlock - 1), rest, 1e-3f);
}
