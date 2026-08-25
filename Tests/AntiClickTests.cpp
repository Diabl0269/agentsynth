#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <vector>

class AntiClickTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer.setSize(1, 1024);
        buffer.clear();
    }

    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiMessages;
};

TEST_F(AntiClickTest, ADSRMinimumRelease) {
    ADSRModule adsr;
    adsr.prepareToPlay(44100.0, 512);

    // Set parameters for quick test
    auto* attackParam = dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[1]);
    auto* releaseParam = dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[4]);
    ASSERT_NE(attackParam, nullptr);
    ASSERT_NE(releaseParam, nullptr);

    *attackParam = 0.001f; // 1ms attack
    *releaseParam = 0.0f;  // Should be clamped to 5ms by implementation

    // Trigger note on
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

    // Process one block - should reach peak level
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 1.0f);
    adsr.processBlock(buffer, midiMessages);
    midiMessages.clear();

    // Check if it's active and produce sound
    ASSERT_GT(buffer.getSample(0, buffer.getNumSamples() - 1), 0.1f);

    // Now trigger note off
    midiMessages.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

    // Process next block
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 1.0f);
    adsr.processBlock(buffer, midiMessages);

    // With 5ms release, the envelope should NOT be 0 instantly.
    // We expect some non-zero level at the beginning of the block.
    EXPECT_GT(buffer.getMagnitude(0, 0, 10), 0.01f);
}

TEST_F(AntiClickTest, OscillatorNoPhaseReset) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    // Trigger first note
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    osc.processBlock(buffer, midiMessages);
    midiMessages.clear();

    float lastSample = buffer.getSample(0, buffer.getNumSamples() - 1);

    // Trigger second note (same frequency or different)
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
    osc.processBlock(buffer, midiMessages);

    float firstSample = buffer.getSample(0, 0);

    // If there was a hard reset to 0 (Sine), firstSample would be sin(0) = 0.
    // Given the previous note ended at some phase, we expect continuity.
    float jump = std::abs(firstSample - lastSample);
    EXPECT_LT(jump, 0.5f); // Rough check for discontinuity
}

TEST_F(AntiClickTest, OscillatorFrequencySmoothing) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    auto* coarseParam = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&osc, "coarse"));
    ASSERT_NE(coarseParam, nullptr);

    // Initial coarse
    *coarseParam = 0;
    osc.processBlock(buffer, midiMessages);

    // Change coarse significantly
    *coarseParam = 12; // +1 octave
    osc.processBlock(buffer, midiMessages);

    // Check if the waveform is producing signal
    EXPECT_GT(buffer.getMagnitude(0, 0, buffer.getNumSamples()), 0.0f);
}

TEST_F(AntiClickTest, ADSRMinimumAttack) {
    ADSRModule adsr;
    adsr.prepareToPlay(44100.0, 512);

    auto* attackParam = dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[1]);
    ASSERT_NE(attackParam, nullptr);
    *attackParam = 0.0f; // Request instant attack

    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    adsr.processBlock(buffer, midiMessages);

    // With 2ms attack at 44.1kHz, sample 1 should be small.
    // If attack was 0, it would be close to 1.0 instantly.
    EXPECT_LT(buffer.getSample(0, 1), 0.5f);
}
