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

// FRO110 retired the old minimum-time clamps (2ms attack / 5ms release) on purpose: 0 ms is a
// real, reachable value now, and click avoidance is the user's own choice. These two tests
// assert the new contract instead of the retired clamp: the 1 ms default is still click-safe,
// and an explicit 0 ms is honoured exactly, with no clamp silently overriding it.

TEST_F(AntiClickTest, ADSRReleaseAntiClickContract) {
    // Channel 0 doubles as the Gate CV input, so it must stay LOW (cleared) throughout --
    // driving the envelope through MIDI note-on/off only. Leaving it high would OR the Gate
    // CV Schmitt trigger into "active" forever and note-off would never actually release.
    ADSRModule adsr;
    adsr.prepareToPlay(44100.0, 512);

    // Leave release at its 1 ms default -- the click-safe default -- and confirm release is
    // NOT instant: some non-zero level must remain in the first few samples after note-off.
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    adsr.processBlock(buffer, midiMessages);
    midiMessages.clear();
    ASSERT_GT(buffer.getSample(0, buffer.getNumSamples() - 1), 0.1f);

    buffer.clear();
    midiMessages.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    adsr.processBlock(buffer, midiMessages);
    EXPECT_GT(buffer.getMagnitude(0, 0, 10), 0.01f) << "the 1 ms default release must not click";

    // An explicit 0 ms release is a deliberate design choice, not a bug that needs a clamp:
    // it must be honoured, reaching silence on the very next sample after note-off.
    ADSRModule instant;
    instant.prepareToPlay(44100.0, 512);
    auto* instantRelease = dynamic_cast<juce::AudioParameterFloat*>(instant.getParameters()[4]);
    ASSERT_NE(instantRelease, nullptr);
    *instantRelease = 0.0f;

    juce::AudioBuffer<float> instantBuffer(1, 512);
    instantBuffer.clear();
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    instant.processBlock(instantBuffer, noteOnMidi);

    instantBuffer.clear();
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    instant.processBlock(instantBuffer, noteOffMidi);
    EXPECT_NEAR(instantBuffer.getSample(0, 0), 0.0f, 1e-4f) << "an explicit 0 ms release must be honoured, not clamped";
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

TEST_F(AntiClickTest, ADSRAttackAntiClickContract) {
    ADSRModule adsr;
    adsr.prepareToPlay(44100.0, 512);

    // Leave attack at its 1 ms default and confirm it is NOT instant: sample 1 should still be
    // well short of full level.
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    adsr.processBlock(buffer, midiMessages);
    EXPECT_LT(buffer.getSample(0, 1), 0.5f) << "the 1 ms default attack must not click";

    // An explicit 0 ms attack is a deliberate design choice, not a bug that needs a clamp: it
    // must be honoured, reaching full level on the very first sample.
    ADSRModule instant;
    instant.prepareToPlay(44100.0, 512);
    auto* instantAttack = dynamic_cast<juce::AudioParameterFloat*>(instant.getParameters()[1]);
    ASSERT_NE(instantAttack, nullptr);
    *instantAttack = 0.0f;

    juce::AudioBuffer<float> instantBuffer(1, 512);
    instantBuffer.clear();
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    instant.processBlock(instantBuffer, noteOnMidi);
    EXPECT_NEAR(instantBuffer.getSample(0, 0), 1.0f, 1e-3f) << "an explicit 0 ms attack must be honoured, not clamped";
}
