// SamplerModulatedMarkersTests.cpp
// The granular scan position the Sampler's waveform display reads includes the Position CV.

#include "Modules/SamplerModule.h"
#include "SamplerTestWavs.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

void processWithPositionCv(SamplerModule& module, float cv) {
    juce::AudioBuffer<float> block(SamplerModule::kNumChannels, kBlock);
    block.clear();
    for (int i = 0; i < kBlock; ++i)
        block.setSample(SamplerModule::kPositionCVCh, i, cv);
    juce::MidiBuffer midi;
    module.processBlock(block, midi);
}

class SamplerModulatedMarkersTest : public ::testing::Test {
protected:
    sampler_test::TempDir dir;
    SamplerModule module;

    void SetUp() override {
        module.prepareToPlay(kRate, kBlock);
        const auto file = dir.file("ramp.wav");
        ASSERT_TRUE(sampler_test::writeWav(file, 8192, 1, kRate, [](int, int i) { return 0.001f * (float)(i % 100); }));
        ASSERT_TRUE(module.loadSampleFile(file));
    }

    void useGranular() {
        auto* mode = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&module, "playMode"));
        ASSERT_NE(mode, nullptr);
        mode->setValueNotifyingHost(mode->convertTo0to1(1.0f));
    }

    juce::AudioParameterFloat* start() {
        return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, "start"));
    }
};

} // namespace

TEST_F(SamplerModulatedMarkersTest, GranularScanPositionFollowsPositionCvWhileKnobStaysPut) {
    // Regression test for FRO2443: the granular scan position showed the stored start knob, so a Position cable
    // moved the sound and not the display.
    useGranular();
    start()->setValueNotifyingHost(0.1f);
    const float knob = start()->get();

    processWithPositionCv(module, 0.4f);

    EXPECT_NEAR(module.getPlayheadPosition(), knob + 0.4f, 1e-4f);
    EXPECT_FLOAT_EQ(start()->get(), knob);
}

TEST_F(SamplerModulatedMarkersTest, GranularScanPositionEqualsKnobWithoutCable) {
    useGranular();
    start()->setValueNotifyingHost(0.3f);
    processWithPositionCv(module, 0.0f);
    EXPECT_NEAR(module.getPlayheadPosition(), start()->get(), 1e-4f);
}
