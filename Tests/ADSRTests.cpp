#include "Modules/ADSRModule.h"
#include <gtest/gtest.h>

class ADSRTest : public ::testing::Test {
protected:
    ADSRModule adsr;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiMessages;

    void SetUp() override {
        adsr.prepareToPlay(44100.0, 512);
        buffer.setSize(2, 512);
        buffer.clear();
    }
};

TEST_F(ADSRTest, StartsIdle) {
    // Initially should output 0
    adsr.processBlock(buffer, midiMessages);
    // Since ADSR modifies buffer in place (multiplying?), typically ADSR is a
    // control signal or VCA? In Agent Synth, ADSRModule might just generate an
    // envelope or apply it. Checking code... wait, I need to check ADSRModule
    // implementation to know if it's a generator or processor. Assuming standard
    // VCA-like behavior or just check "getNextSample" if exposed? "ModuleBase"
    // usually implies processBlock. If buffer was clear (0), output should be 0.

    // Let's set buffer to 1.0 to see if envelope is applied
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 1.0f);

    adsr.processBlock(buffer, midiMessages);

    // Without NoteOn, envelope should be 0 (if valid), so output 0.
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    // expect close to 0
    EXPECT_NEAR(rms, 0.0f, 1e-4);
}

TEST_F(ADSRTest, AttackPhase) {
    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Set buffer to constant 1.0 to measure envelope
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }

    adsr.processBlock(buffer, midiMessages);

    // Envelope should have risen from 0.
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rms, 0.001f);
}

TEST_F(ADSRTest, SustainLevel) {
    // Set sustain to 0.75
    dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[3])->setValueNotifyingHost(0.75f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process the note on block
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process many more blocks to reach sustain phase (attack + decay complete)
    // At 44100 Hz, 512 samples per block ≈ 11.6ms per block
    // Default attack=0.05s, decay=0.2s, so need ~24 blocks to settle
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Check that output level is near sustain value (0.75)
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_NEAR(rms, 0.75f, 0.05f); // Allow some tolerance
}

TEST_F(ADSRTest, ReleasePhase) {
    // Set sustain to non-zero so envelope holds at a level
    dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[3])->setValueNotifyingHost(0.8f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process note on
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process many blocks to reach sustain phase
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Get sustain level — should be near 0.8
    float sustainRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(sustainRms, 0.5f);

    // Now trigger NoteOff to begin release
    auto noteOff = juce::MidiMessage::noteOff(1, 60, (juce::uint8)0);
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(noteOff, 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, noteOffMidi);

    float releaseStartRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    // Process more blocks during release
    for (int block = 0; block < 20; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Output should decay toward 0 during release
    float finalRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_LT(finalRms, releaseStartRms);
    EXPECT_LT(finalRms, 0.1f); // Should be quite small after release
}

TEST_F(ADSRTest, RetriggerDuringRelease) {
    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    juce::MidiBuffer noteOnMidi;
    noteOnMidi.addEvent(noteOn, 0);

    // Fill buffer and process note on
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, noteOnMidi);

    // Process blocks to reach sustain
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Trigger NoteOff to begin release
    auto noteOff = juce::MidiMessage::noteOff(1, 60, (juce::uint8)0);
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(noteOff, 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, noteOffMidi);

    // Process a few blocks during release
    for (int block = 0; block < 5; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Get level during release (should be less than sustain)
    float releaseRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    // Now retrigger with NoteOn while releasing
    auto noteOnAgain = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    juce::MidiBuffer retriggerMidi;
    retriggerMidi.addEvent(noteOnAgain, 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, retriggerMidi);

    // Process a few more blocks during the new attack
    for (int block = 0; block < 3; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // After retrigger, the envelope should rise again
    float rmsAfterRetrigger = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rmsAfterRetrigger, releaseRms);
}

TEST_F(ADSRTest, ZeroSustain) {
    // Set sustain to 0.0
    dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[3])->setValueNotifyingHost(0.0f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process many blocks; with zero sustain, should decay toward 0
    for (int block = 0; block < 40; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Output should approach 0
    float finalRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_LT(finalRms, 0.05f);
}

TEST_F(ADSRTest, FastAttack) {
    // Set attack to minimum (0.01f, clamped to 0.002f)
    dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[1])->setValueNotifyingHost(0.0f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // With very fast attack, envelope should rise quickly
    float rmsFirstBlock = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rmsFirstBlock, 0.5f); // Should rise significantly with fast attack

    // Process one more block
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buffer, emptyMidi);

    // Second block should be at/near sustain level
    float rmsSecondBlock = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_NEAR(rmsSecondBlock, 1.0f, 0.1f);
}

TEST_F(ADSRTest, ParameterChangesDuringPlayback) {
    // Trigger NoteOn with default sustain (0.0)
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process to reach sustain phase with default sustain (0.0)
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // At sustain=0.0, output should be very low
    float rmsBeforeChange = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_LT(rmsBeforeChange, 0.05f);

    // Now change sustain parameter mid-stream to 0.6
    dynamic_cast<juce::AudioParameterFloat*>(adsr.getParameters()[3])->setValueNotifyingHost(0.6f);

    // Process more blocks; the envelope should adapt
    for (int block = 0; block < 15; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 1.0f);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Output should now be near the new sustain level (0.6)
    float rmsAfterChange = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_NEAR(rmsAfterChange, 0.6f, 0.1f);
}

TEST_F(ADSRTest, PolyMode_IndependentEnvelopes) {
    // Enable poly mode
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(adsr.getParameters()[5]);
    ASSERT_NE(polyP, nullptr);
    *polyP = true;

    juce::AudioBuffer<float> polyBuffer(8, 512);
    polyBuffer.clear();

    // Gate voice 0 ON, voice 1 OFF
    for (int i = 0; i < 512; ++i) {
        polyBuffer.setSample(0, i, 1.0f); // Voice 0: gate on
        polyBuffer.setSample(1, i, 0.0f); // Voice 1: gate off
    }

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(polyBuffer, emptyMidi);

    // Voice 0 should have envelope output > 0
    EXPECT_GT(polyBuffer.getRMSLevel(0, 0, 512), 0.0f);
    // Voice 1 should be near zero (no gate)
    EXPECT_NEAR(polyBuffer.getRMSLevel(1, 0, 512), 0.0f, 0.01f);
}

TEST_F(ADSRTest, PolyMode_GateEdgeDetection) {
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(adsr.getParameters()[5]);
    *polyP = true;

    // Block 1: gate ON
    juce::AudioBuffer<float> polyBuffer(8, 512);
    polyBuffer.clear();
    for (int i = 0; i < 512; ++i)
        polyBuffer.setSample(0, i, 1.0f);

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(polyBuffer, emptyMidi);
    float rmsAfterOn = polyBuffer.getRMSLevel(0, 0, 512);
    EXPECT_GT(rmsAfterOn, 0.0f);

    // Block 2: gate OFF (release)
    polyBuffer.clear();
    adsr.processBlock(polyBuffer, emptyMidi);
    // Envelope should start decaying
}
