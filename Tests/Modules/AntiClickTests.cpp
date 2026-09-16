#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include <algorithm>
#include <cmath>
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

// FRO110 retired the PARAMETER's old minimum-time clamps (2ms attack / 5ms release): 0 ms is a
// real, reachable, displayed value now, not silently raised. FRO116 found that honouring it
// *literally* -- a one-sample full-scale level step on note-off/note-on -- is an audible click
// in its own right, so `EnvelopeGenerator` floors the stage's internal effective time to a
// fixed, much smaller click-free minimum (0.1 ms attack, 1 ms decay/release). These two tests
// assert that contract: the 1 ms default is click-safe (unaffected by the floor, since it is
// already above it), and an explicit 0 ms is honoured as "fastest click-free", not as a literal
// single-sample cliff and not silently raised back to the old 2 ms / 5 ms clamp either.

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

    // An explicit 0 ms release is honoured as "as fast as is click-free" -- floored internally
    // to a 1 ms ramp (kMinRampSeconds), not clamped back to the old 5 ms default and not cut to
    // silence on the very next sample either.
    ADSRModule instant;
    instant.prepareToPlay(44100.0, 512);
    auto* instantRelease = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&instant, "release"));
    ASSERT_NE(instantRelease, nullptr);
    *instantRelease = 0.0f;

    juce::AudioBuffer<float> instantBuffer(1, 512);
    instantBuffer.clear();
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    instant.processBlock(instantBuffer, noteOnMidi);
    const float levelBeforeRelease = instantBuffer.getSample(0, instantBuffer.getNumSamples() - 1);
    ASSERT_GT(levelBeforeRelease, 0.9f) << "expected the note to be at (near) full level before release";

    instantBuffer.clear();
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    instant.processBlock(instantBuffer, noteOffMidi);

    // NOT an instant cut: the very first sample after note-off must still carry most of the
    // level (release takes ~1 ms == ~44 samples at 44.1kHz, so one sample is a small fraction
    // of the ramp).
    EXPECT_GT(instantBuffer.getSample(0, 0), 0.5f * levelBeforeRelease)
        << "an explicit 0 ms release must not cut to silence in a single sample";

    // No single-sample step anywhere in the release exceeds a generous click-safety bound (worst
    // case across all curve amounts is ~11% of the level being released from; 25% leaves margin
    // without being vacuous).
    float prev = levelBeforeRelease;
    float maxStep = 0.0f;
    for (int i = 0; i < instantBuffer.getNumSamples(); ++i) {
        const float v = instantBuffer.getSample(0, i);
        maxStep = std::max(maxStep, std::abs(prev - v));
        prev = v;
    }
    EXPECT_LT(maxStep, 0.25f * levelBeforeRelease)
        << "no single sample of an explicit 0 ms release should look like an instant cut";

    // But it IS fast: fully silent well within ~2 ms (~88 samples) of note-off.
    EXPECT_NEAR(instantBuffer.getSample(0, 87), 0.0f, 0.01f)
        << "an explicit 0 ms release must still be fast -- silent within ~2 ms of note-off";
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

    // An explicit 0 ms attack is honoured as "as fast as is click-free" -- floored internally to
    // a 0.1 ms ramp (kMinAttackSeconds), not a literal single-sample 0->1.0 step and not clamped
    // back to the old 2 ms default either.
    ADSRModule instant;
    instant.prepareToPlay(44100.0, 512);
    auto* instantAttack = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&instant, "attack"));
    ASSERT_NE(instantAttack, nullptr);
    *instantAttack = 0.0f;

    juce::AudioBuffer<float> instantBuffer(1, 512);
    instantBuffer.clear();
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    instant.processBlock(instantBuffer, noteOnMidi);

    // NOT an instant 0->1.0 step: the very first sample must be well short of full level (0.1 ms
    // == ~4.4 samples at 44.1kHz, so a single sample is a meaningful fraction of the ramp).
    EXPECT_LT(instantBuffer.getSample(0, 0), 0.5f)
        << "an explicit 0 ms attack must not jump to full level in a single sample";

    // But it IS fast: full level well within ~1 ms (~44 samples) of note-on.
    EXPECT_NEAR(instantBuffer.getSample(0, 43), 1.0f, 0.01f)
        << "an explicit 0 ms attack must still be fast -- full level within ~1 ms of note-on";
}
