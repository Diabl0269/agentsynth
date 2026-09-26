// Oscillator channel-map and DSP tests for the L/R stereo split on voice modules (issue #219);
// see StereoVoiceModuleTestHelpers.h for the shared render/measurement helpers.

#include "Modules/OscillatorModule.h"
#include "StereoVoiceModuleTestHelpers.h"
#include <gtest/gtest.h>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Oscillator — channel map
// ---------------------------------------------------------------------------

TEST(OscillatorStereo, AudioRLivesAboveTheCVBlockNotOnChannelOne) {
    EXPECT_EQ(OscillatorModule::kRightBase, 14);
    EXPECT_EQ(OscillatorModule::kNumOutputs, 22);

    OscillatorModule osc;
    // FRO314: grew 14 -> 16 (Unison/Detune CV, appended). kRightBase/kNumOutputs above are
    // unaffected -- they are a LITERAL, not derived from the input count (see the class-level
    // channel-map comment on OscillatorModule::kRightBase) -- so existing routings into/out of
    // Audio R still key off the same raw channel.
    EXPECT_EQ(osc.getTotalNumInputChannels(), OscillatorModule::kNumInputs);
    EXPECT_EQ(osc.getTotalNumOutputChannels(), OscillatorModule::kNumOutputs);
    EXPECT_EQ(osc.getVisibleOutputPortCount(), 2);
}

TEST(OscillatorStereo, ChannelOneStaysWaveformCVAndNeverAdvertisesItselfAsAudioR) {
    OscillatorModule osc;

    const auto in1 = osc.mapInputChannel(1);
    EXPECT_EQ(in1.role, PortRole::ModCV);
    EXPECT_EQ(in1.visibleJackIndex, 1) << "jack 1 is Waveform";

    // The dangerous case: ModuleBase's default output map clamps a raw channel onto a visible jack
    // index, which would report ch1 as the head of the Audio R jack and let a wire be drawn off the
    // Waveform CV channel.
    const auto out1 = osc.mapOutputChannel(1);
    EXPECT_FALSE(out1.isPolyGroupHead);
    EXPECT_EQ(out1.visibleJackIndex, 0);
}

TEST(OscillatorStereo, ExistingCVTargetChannelsAreUnchangedAndPanIsAppended) {
    OscillatorModule osc;

    // Mono: every pre-#219 target keeps its raw channel; Pan takes ch6, which was already declared
    // and unused.
    // FRO314 appended Unison/Detune (ch14/15, same raw channel in both voice modes).
    const std::vector<std::pair<juce::String, int>> expectedMono = {{"Pitch", 0},  {"Waveform", 1}, {"Octave", 2},
                                                                    {"Coarse", 3}, {"Fine", 4},     {"Level", 5},
                                                                    {"Pan", 6},    {"Unison", 14},  {"Detune", 15}};
    auto mono = osc.getModulationTargets();
    ASSERT_EQ(mono.size(), expectedMono.size());
    for (size_t i = 0; i < mono.size(); ++i) {
        EXPECT_EQ(mono[i].name, expectedMono[i].first);
        EXPECT_EQ(mono[i].channelIndex, expectedMono[i].second);
    }

    setBoolParam(osc, "poly", true);
    const std::vector<std::pair<juce::String, int>> expectedPoly = {{"Waveform", 8}, {"Octave", 9}, {"Coarse", 10},
                                                                    {"Fine", 11},    {"Level", 12}, {"Pan", 13},
                                                                    {"Unison", 14},  {"Detune", 15}};
    auto poly = osc.getModulationTargets();
    ASSERT_EQ(poly.size(), expectedPoly.size());
    for (size_t i = 0; i < poly.size(); ++i) {
        EXPECT_EQ(poly[i].name, expectedPoly[i].first);
        EXPECT_EQ(poly[i].channelIndex, expectedPoly[i].second);
    }
}

TEST(OscillatorStereo, PolyFansBothLegsEightWideFromTheirOwnHeads) {
    OscillatorModule osc;
    setBoolParam(osc, "poly", true);

    const auto left = osc.mapOutputChannel(0);
    EXPECT_EQ(left.visibleJackIndex, 0);
    EXPECT_TRUE(left.isPolyGroupHead);
    EXPECT_EQ(left.polyVoiceSpan, 8);

    const auto right = osc.mapOutputChannel(OscillatorModule::kRightBase);
    EXPECT_EQ(right.visibleJackIndex, 1);
    EXPECT_TRUE(right.isPolyGroupHead);
    EXPECT_EQ(right.polyVoiceSpan, 8) << "Audio R is its own poly head, not voice 1 relabelled";

    // A follower channel inside either fan is not a head.
    EXPECT_FALSE(osc.mapOutputChannel(OscillatorModule::kRightBase + 3).isPolyGroupHead);
}

// ---------------------------------------------------------------------------
// Oscillator — DSP
// ---------------------------------------------------------------------------

TEST(OscillatorStereo, DefaultPanKeepsAudioRIdenticalToAudioL) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderOscillator(osc);
    const float left = rmsOf(out, 0);
    ASSERT_GT(left, 0.05f) << "nothing was rendered, so the comparison below proves nothing";
    EXPECT_LT(maxAbsDiff(out, 0, out, OscillatorModule::kRightBase), 1.0e-7f)
        << "at Pan 0 a mono patch that only cables Audio L must sound exactly as it did before #219";
}

TEST(OscillatorStereo, CentrePanDoesNotAttenuateAudioL) {
    // Guards against someone "fixing" the balance law into an equal-power one, which would drop
    // every existing mono patch by 3 dB.
    OscillatorModule centred;
    centred.prepareToPlay(kSampleRate, kBlockSize);
    const float centredLeft = rmsOf(renderOscillator(centred), 0);

    OscillatorModule hardLeft;
    hardLeft.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(hardLeft, "pan", -1.0f);
    const float hardLeftLeft = rmsOf(renderOscillator(hardLeft), 0);

    ASSERT_GT(centredLeft, 0.05f);
    EXPECT_NEAR(centredLeft, hardLeftLeft, 1.0e-4f);
}

TEST(OscillatorStereo, HardPanSilencesTheOppositeLeg) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(osc, "pan", 1.0f);

    const auto out = renderOscillator(osc);
    EXPECT_LT(rmsOf(out, 0), 1.0e-6f);
    EXPECT_GT(rmsOf(out, OscillatorModule::kRightBase), 0.05f);

    setFloatParam(osc, "pan", -1.0f);
    const auto flipped = renderOscillator(osc);
    EXPECT_GT(rmsOf(flipped, 0), 0.05f);
    EXPECT_LT(rmsOf(flipped, OscillatorModule::kRightBase), 1.0e-6f);
}

TEST(OscillatorStereo, PanCVMovesTheImage) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);

    const int panCV = 6; // mono Pan jack
    const auto out = renderOscillator(osc, 2, panCV, 1.0f);
    EXPECT_LT(rmsOf(out, 0), 1.0e-6f) << "a full-scale Pan CV should push the image hard right";
    EXPECT_GT(rmsOf(out, OscillatorModule::kRightBase), 0.05f);
}

TEST(OscillatorStereo, PolyRendersBothLegsPerVoice) {
    OscillatorModule osc;
    setBoolParam(osc, "poly", true);
    osc.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(OscillatorModule::kNumOutputs, kBlockSize);
    juce::MidiBuffer midi;

    // Pitch CV in Hz on the per-voice fan.
    for (int block = 0; block < 2; ++block) {
        buffer.clear();
        for (int v = 0; v < 3; ++v)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(v, i, 220.0f * (float)(v + 1));
        osc.processBlock(buffer, midi);
    }

    for (int v = 0; v < 3; ++v) {
        EXPECT_GT(rmsOf(buffer, v), 0.01f) << "voice " << v << " left leg";
        EXPECT_LT(maxAbsDiff(buffer, v, buffer, OscillatorModule::kRightBase + v), 1.0e-7f)
            << "voice " << v << " should be centred by default";
    }
}

TEST(OscillatorStereo, PolyLeavesAudioRIntactAndTheCVBlockSilent) {
    // Pins the end state of a poly block: the CV channels between the two audio blocks must not
    // leak downstream as audio, and the right leg must survive to the end of the block. (The clear
    // runs before the stereo pass, so this does not by itself catch an unbounded clear — the
    // equivalent Filter test does, because there the clear runs last.)
    OscillatorModule osc;
    setBoolParam(osc, "poly", true);
    osc.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(OscillatorModule::kNumOutputs, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 2; ++block) {
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(0, i, 440.0f);
        osc.processBlock(buffer, midi);
    }

    EXPECT_GT(rmsOf(buffer, OscillatorModule::kRightBase), 0.01f);
    // The CV channels between the two audio blocks must still be silent.
    for (int ch = 8; ch < OscillatorModule::kRightBase; ++ch)
        EXPECT_LT(rmsOf(buffer, ch), 1.0e-6f) << "CV channel " << ch << " leaked";
}
