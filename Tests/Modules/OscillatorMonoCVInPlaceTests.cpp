// OscillatorMonoCVInPlaceTests.cpp
//
// Mono mode reads its mod-CV inputs (ch1-5, Pan on ch6) straight from the buffer and clears them only after the
// render. These pin what that must preserve: the CV still acts, the CV channels still leave the module silent, and a
// jack the 64-sample guard calls unpatched is still read as zeros.

#include "Modules/OscillatorModule.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr int kBlock = 512;
constexpr int kChannels = OscillatorModule::kNumOutputs;
constexpr int kPanCV = 6;

std::unique_ptr<OscillatorModule> playingOscillator() {
    auto osc = std::make_unique<OscillatorModule>();
    osc->prepareToPlay(48000.0, kBlock);
    juce::AudioBuffer<float> buffer(kChannels, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
    osc->processBlock(buffer, midi);
    return osc;
}

bool channelSilent(const juce::AudioBuffer<float>& buffer, int ch) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (buffer.getSample(ch, i) != 0.0f)
            return false;
    return true;
}

} // namespace

TEST(OscillatorMonoCVInPlaceTest, PatchedCVActsAndItsChannelsLeaveSilent) {
    auto osc = playingOscillator();
    juce::AudioBuffer<float> buffer(kChannels, kBlock);
    buffer.clear();
    for (int i = 0; i < kBlock; ++i) {
        buffer.setSample(2, i, 0.25f);     // Octave CV: one octave up
        buffer.setSample(kPanCV, i, 0.5f); // Pan CV: half right
    }
    juce::MidiBuffer midi;
    osc->processBlock(buffer, midi);

    for (int ch = 1; ch <= kPanCV; ++ch)
        EXPECT_TRUE(channelSilent(buffer, ch)) << "CV channel " << ch << " must not leak out as audio";
    float left = 0.0f, right = 0.0f;
    for (int i = 0; i < kBlock; ++i) {
        left += std::abs(buffer.getSample(0, i));
        right += std::abs(buffer.getSample(OscillatorModule::kRightBase, i));
    }
    EXPECT_GT(right, 0.0f);
    EXPECT_LT(left, 0.75f * right) << "the Pan CV moved the voice right";
}

TEST(OscillatorMonoCVInPlaceTest, CVThatStartsAfterTheGuardWindowReadsAsUnpatched) {
    auto osc = playingOscillator();
    juce::AudioBuffer<float> buffer(kChannels, kBlock);
    buffer.clear();
    for (int i = 100; i < kBlock; ++i) // silent through the 64-sample guard: treated as an empty jack
        buffer.setSample(kPanCV, i, 1.0f);
    juce::MidiBuffer midi;
    osc->processBlock(buffer, midi);

    EXPECT_TRUE(channelSilent(buffer, kPanCV));
    for (int i = 0; i < kBlock; ++i)
        ASSERT_EQ(buffer.getSample(OscillatorModule::kRightBase, i), buffer.getSample(0, i))
            << "centred and unpatched: Audio R is a bit-identical copy of Audio L (sample " << i << ")";
}
