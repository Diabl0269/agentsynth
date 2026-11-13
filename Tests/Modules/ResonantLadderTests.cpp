// ResonantLadderTests.cpp
//
// synth::ResonantLadder is juce::dsp::LadderFilter's structure with the Resonance knob's own curve and headroom at
// Drive 1. At Drive 10 the headroom is 1, so with the feedback matched it must render what the JUCE ladder renders.

#include "Modules/LadderCoefficientSetter.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_dsp/juce_dsp.h>

TEST(ResonantLadderTest, FeedbackCurveStartsAtZeroAndStopsShortOfSelfOscillation) {
    EXPECT_EQ(synth::ladderFeedbackForResonance(0.0f), 0.0f);
    EXPECT_EQ(synth::ladderFeedbackForResonance(std::nanf("")), 0.0f);
    float last = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float k = synth::ladderFeedbackForResonance((float)i / 100.0f);
        EXPECT_GE(k, last);
        last = k;
    }
    EXPECT_GT(last, 0.97f);
    EXPECT_LT(last, 1.0f);
    EXPECT_LT(synth::ladderFeedbackForResonance(3.0f), 1.0f);
}

TEST(ResonantLadderTest, HeadroomIsFullAtDriveOneAndNoneAtDriveTen) {
    EXPECT_LT(synth::ladderHeadroomForDrive(1.0f), 0.1f);
    EXPECT_FLOAT_EQ(synth::ladderHeadroomForDrive(10.0f), 1.0f);
    EXPECT_FLOAT_EQ(synth::ladderHeadroomForDrive(50.0f), 1.0f);
}

TEST(ResonantLadderTest, AtDriveTenItRendersWhatTheJuceLadderRenders) {
    constexpr double sr = 48000.0;
    for (const auto mode :
         {juce::dsp::LadderFilterMode::LPF24, juce::dsp::LadderFilterMode::HPF12, juce::dsp::LadderFilterMode::BPF24}) {
        auto mine = std::make_unique<synth::ResonantLadder>();
        auto theirs = std::make_unique<juce::dsp::LadderFilter<float>>();
        mine->prepare({sr, 512, 1});
        theirs->prepare({sr, 512, 1});
        mine->setMode(mode);
        theirs->setMode(mode);
        mine->setCutoffFrequencyHz(900.0f);
        theirs->setCutoffFrequencyHz(900.0f);
        const float knob = 0.6f;
        const float k = synth::ladderFeedbackForResonance(knob);
        mine->setResonance(knob);
        theirs->setResonance((k - 0.1f) / 0.9f); // JUCE's feedback is 0.1 + 0.9 x its argument
        mine->setDrive(10.0f);
        theirs->setDrive(10.0f);
        mine->reset();
        theirs->reset();
        juce::Random rng(3);
        float worst = 0.0f;
        for (int i = 0; i < 6000; ++i) {
            float a = 0.3f * (rng.nextFloat() * 2.0f - 1.0f), b = a;
            float* pa = &a;
            float* pb = &b;
            juce::dsp::AudioBlock<float> ba(&pa, 1, 1), bb(&pb, 1, 1);
            mine->process(juce::dsp::ProcessContextReplacing<float>(ba));
            theirs->process(juce::dsp::ProcessContextReplacing<float>(bb));
            worst = std::max(worst, std::abs(a - b));
        }
        EXPECT_LT(worst, 1.0e-4f) << "mode " << (int)mode;
    }
}
