// VCA channel-map and DSP tests for the L/R stereo split on voice modules (issue #219) — the last
// link in the default preset's stereo chain; see StereoVoiceModuleTestHelpers.h for the shared
// render/measurement helpers.

#include "Modules/VCAModule.h"
#include "StereoVoiceModuleTestHelpers.h"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// VCA — the last link in the default preset's stereo chain
// ---------------------------------------------------------------------------

TEST(VCAStereo, AudioRIsItsOwnBlockAndCVKeepsItsRawChannels) {
    EXPECT_EQ(VCAModule::kRightBase, 16);
    EXPECT_EQ(VCAModule::kNumChannels, 24);

    VCAModule vca;
    EXPECT_EQ(vca.getTotalNumInputChannels(), VCAModule::kNumChannels);
    EXPECT_EQ(vca.getTotalNumOutputChannels(), VCAModule::kNumChannels);
    EXPECT_EQ(vca.getVisibleInputPortCount(), 3);
    EXPECT_EQ(vca.getVisibleOutputPortCount(), 2);

    // The gain CV keeps raw ch1 (mono) / ch8 (poly) — only its visible slot moved.
    EXPECT_EQ(vca.getModulationTargets()[0].channelIndex, 1);
    EXPECT_EQ(vca.mapInputChannel(1).role, PortRole::ModCV);
    EXPECT_EQ(vca.mapInputChannel(1).visibleJackIndex, 2);

    // ch1 must never advertise itself as the Audio R output head.
    const auto out1 = vca.mapOutputChannel(1);
    EXPECT_FALSE(out1.isPolyGroupHead);
    EXPECT_EQ(out1.visibleJackIndex, 0);
    EXPECT_TRUE(vca.mapOutputChannel(VCAModule::kRightBase).isPolyGroupHead);
    EXPECT_EQ(vca.mapOutputChannel(VCAModule::kRightBase).visibleJackIndex, 1);
}

TEST(VCAStereo, MonoGatesBothLegsWithTheSameGainAndCV) {
    VCAModule vca;
    vca.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(vca, "gain", 1.0f);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block) {
        buffer.clear();
        fillTone(buffer, 0, 1000.0f);
        fillTone(buffer, VCAModule::kRightBase, 1000.0f);
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(1, i, 0.5f); // gain CV
        vca.processBlock(buffer, midi);
    }

    ASSERT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_LT(maxAbsDiff(buffer, 0, buffer, VCAModule::kRightBase), 1.0e-6f)
        << "both legs must ride one gain ramp, or the image drifts while Gain moves";
}

TEST(VCAStereo, SilentAudioRStaysSilent) {
    VCAModule vca;
    vca.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, 0, 1000.0f);
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(1, i, 1.0f);

    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_LT(rmsOf(buffer, VCAModule::kRightBase), 1.0e-9f);
}

TEST(VCAStereo, ClearsDoNotEraseAudioR) {
    VCAModule vca;
    vca.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(vca, "gain", 1.0f);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, VCAModule::kRightBase, 1000.0f);
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(1, i, 1.0f);

    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);
    EXPECT_GT(rmsOf(buffer, VCAModule::kRightBase), 1.0e-4f);

    // And on bypass the right leg passes through dry rather than being cleared with the CV block.
    VCAModule bypassed;
    bypassed.prepareToPlay(kSampleRate, kBlockSize);
    bypassed.setBypassed(true);
    juce::AudioBuffer<float> dry(VCAModule::kNumChannels, kBlockSize);
    dry.clear();
    fillTone(dry, 0, 1000.0f);
    fillTone(dry, VCAModule::kRightBase, 1000.0f);
    juce::AudioBuffer<float> expected(dry);
    bypassed.processBlock(dry, midi);

    EXPECT_LT(maxAbsDiff(dry, 0, expected, 0), 1.0e-9f);
    EXPECT_LT(maxAbsDiff(dry, VCAModule::kRightBase, expected, VCAModule::kRightBase), 1.0e-9f);
}

TEST(VCAStereo, PolySumsTheRightBlockToItsOwnHead) {
    VCAModule vca;
    setBoolParam(vca, "poly", true);
    vca.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(vca, "gain", 1.0f);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    buffer.clear();
    for (int v = 0; v < 3; ++v) {
        fillTone(buffer, v, 300.0f * (float)(v + 1));
        fillTone(buffer, VCAModule::kRightBase + v, 300.0f * (float)(v + 1));
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(8 + v, i, 1.0f); // per-voice gain CV
    }

    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_GT(rmsOf(buffer, VCAModule::kRightBase), 1.0e-4f);
    // Follower voices of the right block are zeroed, exactly like the left block's.
    for (int v = 1; v < 8; ++v)
        EXPECT_LT(rmsOf(buffer, VCAModule::kRightBase + v), 1.0e-9f) << "right follower voice " << v;
}
