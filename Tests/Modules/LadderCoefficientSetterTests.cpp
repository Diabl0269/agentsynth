// LadderCoefficientSetterTests.cpp
//
// LadderCoefficientSetter skips a LadderFilter setter whose argument has not changed. That must be invisible in the
// sound: a ladder driven through it renders bit-identically to one whose setters are called on every sample, through
// static stretches, ramps, steps, and a re-prepare.

#include "Modules/LadderCoefficientSetter.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

struct Coefficients {
    float cutoffHz, resonance, drive;
};

// Per-sample coefficients with every shape a Filter feeds its ladder: held, ramped, stepped and held again.
Coefficients coefficientsAt(int i) {
    if (i < 300)
        return {800.0f, 0.3f, 1.0f};
    if (i < 600)
        return {800.0f + 4.0f * (float)(i - 300), 0.3f + 0.001f * (float)(i - 300), 1.0f};
    if (i < 900)
        return {2000.0f, 0.6f, 3.5f};
    return {150.0f, 0.95f, 7.0f};
}

void prepare(juce::dsp::LadderFilter<float>& ladder) {
    ladder.prepare({48000.0, 512, 1});
    ladder.setEnabled(true);
    ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
}

float renderSample(juce::dsp::LadderFilter<float>& ladder, float input) {
    float sample = input;
    float* channels[] = {&sample};
    juce::dsp::AudioBlock<float> block(channels, 1, 1);
    juce::dsp::ProcessContextReplacing<float> context(block);
    ladder.process(context);
    return sample;
}

} // namespace

TEST(LadderCoefficientSetterTest, OutputIsBitIdenticalToSettingEverySample) {
    juce::dsp::LadderFilter<float> direct, cached;
    prepare(direct);
    prepare(cached);
    synth::LadderCoefficientSetter setter;

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 1200; ++i) {
            const auto c = coefficientsAt(i);
            direct.setCutoffFrequencyHz(c.cutoffHz);
            direct.setResonance(c.resonance);
            direct.setDrive(c.drive);
            setter.setCutoff(cached, c.cutoffHz);
            setter.setResonance(cached, c.resonance);
            setter.setDrive(cached, c.drive);

            const float input = 0.8f * std::sin(0.031f * (float)i) + 0.2f * std::sin(0.43f * (float)i);
            ASSERT_EQ(renderSample(direct, input), renderSample(cached, input)) << "pass " << pass << " sample " << i;
        }
        // A re-prepare resets the ladder's sample-rate scaling; the owner resets its setter alongside it.
        prepare(direct);
        prepare(cached);
        setter = {};
    }
}

TEST(LadderCoefficientSetterTest, FirstCallAlwaysApplies) {
    juce::dsp::LadderFilter<float> direct, cached;
    prepare(direct);
    prepare(cached);
    direct.setDrive(1.0f); // LadderFilter's own default drive is 1.2: a first call of 1.0 must not be skipped
    synth::LadderCoefficientSetter setter;
    setter.setDrive(cached, 1.0f);
    for (int i = 0; i < 64; ++i)
        ASSERT_EQ(renderSample(direct, 0.9f), renderSample(cached, 0.9f)) << "sample " << i;
}
