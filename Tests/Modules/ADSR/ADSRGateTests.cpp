// ADSRGateTests.cpp
// Mono gating: the Gate CV Schmitt trigger, MIDI note-on/off, their OR combination, and the
// FRO110 repros for mono multi-note (re-articulation / held-note) behaviour.

#include "ADSRTestFixture.h"

TEST_F(ADSRTest, MonoGateCvRisingEdgeStartsEnvelope) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 1.0f);

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buffer, emptyMidi);

    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.001f);
    EXPECT_TRUE(adsr.isOverThreshold());
}

TEST_F(ADSRTest, MonoGateCvFallingEdgeReleasesEnvelope) {
    setFloat(adsr, "sustain", 0.8f);

    juce::MidiBuffer emptyMidi;
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, 1.0f);
        adsr.processBlock(buffer, emptyMidi);
    }
    const float sustainRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(sustainRms, 0.5f);

    buffer.clear();
    adsr.processBlock(buffer, emptyMidi);
    for (int block = 0; block < 20; ++block) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
    }
    EXPECT_LT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.1f);
}

TEST_F(ADSRTest, MonoMidiStillWorksWithSilentGate) {
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    adsr.processBlock(buffer, midiMessages);
    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.001f);
}

TEST_F(ADSRTest, MidiOffDoesNotReleaseWhileGateCvIsHigh) {
    setFloat(adsr, "sustain", 0.8f);

    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    adsr.processBlock(buffer, midiMessages);

    juce::MidiBuffer emptyMidi;
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, 1.0f);
        adsr.processBlock(buffer, emptyMidi);
    }
    const float held = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(held, 0.5f);

    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 1.0f);
    adsr.processBlock(buffer, noteOffMidi);

    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.5f);
}

// ---------------------------------------------------------------------------
// FRO110 regression suite (formerly the Repro* diagnostics that motivated the rewrite): the
// mono held-note bitset fix -- a MIDI note-on drives the retrigger directly instead of relying
// on an edge in a held/not-held flag.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, MonoBackToBackNotesReArticulate) {
    setFloat(adsr, "sustain", 0.8f);
    setFloat(adsr, "attack", 0.05f);
    setFloat(adsr, "decay", 0.1f);

    auto noteOn60 = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn60, 0);

    juce::MidiBuffer emptyMidi;
    buffer.clear();
    adsr.processBlock(buffer, midiMessages);

    // Settle at sustain (attack 0.05s + decay 0.1s ~= 0.15s); 30 blocks (~0.35s) for margin.
    for (int b = 0; b < 30; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
    }
    const float settled = buffer.getSample(0, buffer.getNumSamples() - 1);
    ASSERT_GT(settled, 0.6f) << "did not settle near sustain 0.8, got " << settled;

    // Both events at the SAME sample position (0): note-off(60) then note-on(62) -- a
    // back-to-back mono legato transition.
    juce::MidiBuffer transitionMidi;
    transitionMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    transitionMidi.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0);

    buffer.clear();
    adsr.processBlock(buffer, transitionMidi);

    // EnvelopeGenerator's retrigger is deliberately continuous -- noteOn() re-enters Attack
    // from the CURRENT level (0.8) toward its target (1.0), so this does not dip below 0.8 (a
    // dip would be exactly the discontinuity/click the design promises never to produce). The
    // unambiguous signature that a real new Attack cycle happened -- as opposed to the bug,
    // where the envelope stayed frozen at exactly 0.8 forever -- is that it climbs back up to a
    // fresh peak near 1.0 within one attack time (0.05s).
    float maxAfter = buffer.getSample(0, 0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        maxAfter = std::max(maxAfter, buffer.getSample(0, i));

    // ~0.06s more (~5 blocks total including the transition block; attack 0.05s completes
    // within this window).
    for (int b = 0; b < 4; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            maxAfter = std::max(maxAfter, buffer.getSample(0, i));
    }

    EXPECT_GT(maxAfter, 0.95f) << "measured max envelope after transition " << maxAfter
                               << " (expected a fresh re-attack peak near 1.0, not a level frozen at 0.8)";
}

TEST_F(ADSRTest, MonoReleasingOneOfTwoHeldNotesKeepsEnvelopeUp) {
    setFloat(adsr, "sustain", 0.8f);

    auto noteOn60 = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn60, 0);
    buffer.clear();
    adsr.processBlock(buffer, midiMessages);

    juce::MidiBuffer emptyMidi;
    for (int b = 0; b < 30; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
    }
    const float settled = buffer.getSample(0, buffer.getNumSamples() - 1);
    ASSERT_GT(settled, 0.6f) << "did not settle near sustain 0.8, got " << settled;

    // Second note held while the first is still down.
    juce::MidiBuffer noteOn64Midi;
    noteOn64Midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
    buffer.clear();
    adsr.processBlock(buffer, noteOn64Midi);
    for (int b = 0; b < 5; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
    }

    // Release note 60 only -- note 64 is still physically held.
    juce::MidiBuffer noteOff60Midi;
    noteOff60Midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear();
    adsr.processBlock(buffer, noteOff60Midi);

    // ~0.3s total (~26 blocks).
    float level = buffer.getSample(0, buffer.getNumSamples() - 1);
    for (int b = 0; b < 25; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        level = buffer.getSample(0, buffer.getNumSamples() - 1);
    }

    EXPECT_GT(level, 0.5f) << "measured level " << level
                           << " after releasing one of two held notes (64 should still be held)";
}
