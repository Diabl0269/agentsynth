// OscillatorRestartCableTests.cpp
// The Oscillator's Phase and Rand knobs take cables (two channels after Fixed rate, no jacks). Each is read on the
// sample a note starts, so a cable moving mid-note changes nothing until the next note.

#include "Modules/OscillatorModule.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct Rig {
    OscillatorModule osc;
    explicit Rig(float randomPhase = 0.0f) {
        setActual(osc, "randomPhase", randomPhase);
        setActual(osc, "unison", 3.0f);
        osc.prepareToPlay(kSampleRate, kBlock);
    }

    // One block with a Gate edge at `edgeAt` (-1: none) and `cv` on the cable channel of `paramId`.
    std::vector<float> block(int edgeAt, const char* paramId = nullptr, float cv = 0.0f) {
        juce::AudioBuffer<float> buf(osc.getTotalNumInputChannels(), kBlock);
        buf.clear();
        if (edgeAt >= 0)
            for (int s = edgeAt; s < kBlock; ++s)
                buf.setSample(OscillatorModule::kGateBase, s, 1.0f);
        if (paramId != nullptr)
            juce::FloatVectorOperations::fill(buf.getWritePointer(osc.modulationChannelForParam(paramId)), cv, kBlock);
        juce::MidiBuffer midi;
        osc.processBlock(buf, midi);
        return {buf.getReadPointer(0), buf.getReadPointer(0) + kBlock};
    }
};

float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

} // namespace

TEST(OscillatorRestartCable, PhaseAndRandAreKnobOnlyTargetsOnChannelsAfterFixedRate) {
    OscillatorModule osc;
    EXPECT_EQ(OscillatorModule::kFixedRateCVChannel, 28);
    EXPECT_EQ(OscillatorModule::kNumInputs, 31);
    EXPECT_EQ(osc.getTotalNumInputChannels(), 31);
    EXPECT_EQ(osc.modulationChannelForParam("phase"), 29);
    EXPECT_EQ(osc.modulationChannelForParam("randomPhase"), 30);
    EXPECT_EQ(osc.getInputPortLabel(29), "Phase");
    EXPECT_EQ(osc.getInputPortLabel(30), "Rand");
    EXPECT_EQ(osc.getVisibleInputPortCount(), OscillatorModule::kNumJacks) << "no jack on the card";
    for (const auto& t : osc.getModulationTargets())
        if (t.paramId == "phase" || t.paramId == "randomPhase")
            EXPECT_TRUE(t.knobOnly) << t.paramId;
    // The channels the module had before keep their place.
    EXPECT_EQ(OscillatorModule::kRightBase, 14);
    EXPECT_EQ(OscillatorModule::kGateBase, 20);
    EXPECT_EQ(OscillatorModule::kUnisonCVChannel, 14);
    EXPECT_EQ(osc.modulationChannelForParam("fixedRate"), 28);
    EXPECT_EQ(osc.modulationChannelForParam("level"), 5);
}

// Regression test for FRO1556: the Oscillator's Phase and Rand took no cable.
TEST(OscillatorRestartCable, ACableOnPhaseSetsWhereTheNoteStartsAndMovesNothingMidNote) {
    Rig cabled;
    const auto a0 = cabled.block(100, "phase", 0.25f); // 0.25 of 0..360 is 90 degrees
    const auto a1 = cabled.block(300, "phase", 0.5f);  // the next note: 180 degrees
    const auto a2 = cabled.block(-1, "phase", 0.0f);

    Rig knob;
    setActual(knob.osc, "phase", 90.0f);
    const auto b0 = knob.block(100);
    // The cable moved to 0.5 at the start of block 1, but the first note's phases are already running: block 1 up to
    // its own note start (sample 300) is the first note, untouched.
    setActual(knob.osc, "phase", 180.0f);
    const auto b1 = knob.block(300);
    const auto b2 = knob.block(-1);

    EXPECT_EQ(maxDiff(a0, b0), 0.0f) << "the note at sample 100 starts at 90 degrees";
    EXPECT_EQ(maxDiff(a1, b1), 0.0f) << "the note at sample 300 starts at 180 degrees";
    EXPECT_EQ(maxDiff(a2, b2), 0.0f);

    Rig plain;
    const auto c0 = plain.block(100);
    EXPECT_GT(maxDiff(a0, c0), 0.1f) << "without the cable the note starts at 0 degrees";
}

TEST(OscillatorRestartCable, ACableOnRandomPhaseReachesTheUnisonVoicesAtTheNote) {
    Rig cabled(0.0f);
    const auto a = cabled.block(100, "randomPhase", 0.5f); // 0.5 of 0..100 is 50 percent
    Rig knob(50.0f);
    const auto b = knob.block(100);
    Rig plain(0.0f);
    const auto c = plain.block(100);
    EXPECT_EQ(maxDiff(a, b), 0.0f);
    EXPECT_GT(maxDiff(a, c), 0.01f);
}
