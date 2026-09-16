#include "Modules/ADSRModule.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>
#include <iostream>

static juce::AudioParameterFloat* floatParam(ADSRModule& m, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, id));
}

static juce::AudioParameterBool* boolParam(ADSRModule& m, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, id));
}

static void setFloat(ADSRModule& m, const juce::String& id, float actual) {
    auto* p = floatParam(m, id);
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

static void setPoly(ADSRModule& m, bool on) {
    auto* p = boolParam(m, "poly");
    ASSERT_NE(p, nullptr);
    *p = on;
}

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
    adsr.processBlock(buffer, midiMessages);
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_NEAR(rms, 0.0f, 1e-4);
}

TEST_F(ADSRTest, AttackPhase) {
    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Set buffer to constant 1.0 to measure envelope
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }

    adsr.processBlock(buffer, midiMessages);

    // Envelope should have risen from 0.
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rms, 0.001f);
}

TEST_F(ADSRTest, SustainLevel) {
    // Set sustain to 0.75
    setFloat(adsr, "sustain", 0.75f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process the note on block
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process many more blocks to reach sustain phase (attack + decay complete)
    // At 44100 Hz, 512 samples per block ≈ 11.6ms per block
    // Default attack=0.05s, decay=0.2s, so need ~24 blocks to settle
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
    setFloat(adsr, "sustain", 0.8f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process note on
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process many blocks to reach sustain phase
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, noteOffMidi);

    float releaseStartRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    // Process more blocks during release
    for (int block = 0; block < 20; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, noteOnMidi);

    // Process blocks to reach sustain
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // Trigger NoteOff to begin release
    auto noteOff = juce::MidiMessage::noteOff(1, 60, (juce::uint8)0);
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(noteOff, 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, noteOffMidi);

    // Process a few blocks during release
    for (int block = 0; block < 5; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, retriggerMidi);

    // Process a few more blocks during the new attack
    for (int block = 0; block < 3; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
    setFloat(adsr, "sustain", 0.0f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process many blocks; with zero sustain, should decay toward 0
    for (int block = 0; block < 40; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
    setFloat(adsr, "attack", 0.01f);

    // Trigger NoteOn
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    // Fill buffer and process
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // With very fast attack, envelope should rise quickly
    float rmsFirstBlock = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rmsFirstBlock, 0.5f); // Should rise significantly with fast attack

    // Process one more block
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
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
        buffer.setSample(0, i, 0.0f);
        buffer.setSample(1, i, 0.0f);
    }
    adsr.processBlock(buffer, midiMessages);

    // Process to reach sustain phase with default sustain (0.0)
    for (int block = 0; block < 30; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
        }
        juce::MidiBuffer emptyMidi;
        adsr.processBlock(buffer, emptyMidi);
    }

    // At sustain=0.0, output should be very low
    float rmsBeforeChange = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_LT(rmsBeforeChange, 0.05f);

    // Now change sustain parameter mid-stream to 0.6
    setFloat(adsr, "sustain", 0.6f);

    // Process more blocks; the envelope should adapt
    for (int block = 0; block < 15; ++block) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.0f);
            buffer.setSample(1, i, 0.0f);
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
    setPoly(adsr, true);

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
    setPoly(adsr, true);

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

TEST_F(ADSRTest, GateBelowThresholdDoesNotStartEnvelope) {
    setFloat(adsr, "gateThreshold", 0.8f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.6f);

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_NEAR(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.0f, 1e-4);
}

TEST_F(ADSRTest, GateAboveRaisedThresholdStartsEnvelope) {
    setFloat(adsr, "gateThreshold", 0.8f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.95f);

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.001f);
}

TEST_F(ADSRTest, ThresholdCVShiftsTheGatePoint) {
    juce::AudioBuffer<float> buf(9, 512);
    buf.clear();
    for (int i = 0; i < 512; ++i) {
        buf.setSample(0, i, 0.4f);  // below the 0.5 default
        buf.setSample(8, i, -0.2f); // pulls threshold down to 0.3
    }
    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buf, emptyMidi);
    EXPECT_GT(buf.getRMSLevel(0, 0, 512), 0.001f);
    EXPECT_NEAR(adsr.getEffectiveThreshold(), 0.3f, 1e-4);
}

TEST_F(ADSRTest, DefaultThresholdIsHalf) { EXPECT_NEAR(adsr.getEffectiveThreshold(), 0.5f, 1e-5f); }

TEST_F(ADSRTest, HysteresisRejectsDitherAroundThreshold) {
    juce::MidiBuffer emptyMidi;
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.6f);
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_TRUE(adsr.isOverThreshold());

    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.48f); // still inside the 0.05 gap below 0.5
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_TRUE(adsr.isOverThreshold());
}

TEST_F(ADSRTest, ChannelLayoutIncludesThresholdCv) {
    EXPECT_EQ(adsr.getTotalNumInputChannels(), 9);
    EXPECT_EQ(adsr.getTotalNumOutputChannels(), 9);
    EXPECT_EQ(adsr.getVisibleInputPortCount(), 2);
    EXPECT_EQ(adsr.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(adsr.getInputPortLabel(0), "Gate");
    EXPECT_EQ(adsr.getInputPortLabel(1), "Threshold");
}

TEST_F(ADSRTest, ThresholdChannelIsModCv) {
    auto gate = adsr.mapInputChannel(0);
    EXPECT_EQ(gate.role, PortRole::Gate);
    auto thresh = adsr.mapInputChannel(8);
    EXPECT_EQ(thresh.visibleJackIndex, 1);
    EXPECT_EQ(thresh.role, PortRole::ModCV);
}

// ---------------------------------------------------------------------------
// Repro* tests: TESTS-FIRST diagnostics for four suspected ADSR bugs (FRO110).
// These are pure repro/measurement tests — no Source/ changes accompany them.
// Each prints the actual measured number in its failure message so the real
// behaviour is visible whether the assertion passes or fails.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, ReproMonoZeroSustainStillProducesAttackAndDecay) {
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "attack", 0.05f);
    setFloat(adsr, "decay", 0.5f);

    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);

    float peak = 0.0f;
    auto processAndTrackPeak = [&](juce::MidiBuffer& midi) {
        buffer.clear();
        adsr.processBlock(buffer, midi);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::max(peak, buffer.getSample(0, i));
    };

    processAndTrackPeak(midiMessages);

    juce::MidiBuffer emptyMidi;
    // ~0.3s total at 512 samples / 44.1kHz block ~= 11.6ms/block -> ~26 blocks.
    for (int b = 1; b < 26; ++b)
        processAndTrackPeak(emptyMidi);

    std::cout << "[Repro] MonoZeroSustain: measured peak = " << peak << std::endl;
    EXPECT_GT(peak, 0.9f) << "measured peak " << peak;
}

TEST_F(ADSRTest, ReproPolyZeroSustainReleaseTakesFullReleaseTime) {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    // Control variant: sustain = 0.0. The level at note-off is already ~0 here, so release
    // *timing* can't be isolated from this alone -- it's run only for context/comparison.
    {
        ADSRModule adsrZero;
        adsrZero.prepareToPlay(sampleRate, blockSize);
        setPoly(adsrZero, true);
        setFloat(adsrZero, "sustain", 0.0f);
        setFloat(adsrZero, "attack", 0.01f);
        setFloat(adsrZero, "decay", 0.05f);
        setFloat(adsrZero, "release", 1.0f);

        juce::AudioBuffer<float> polyBuffer(8, blockSize);
        juce::MidiBuffer emptyMidi;

        for (int b = 0; b < 10; ++b) {
            polyBuffer.clear();
            for (int i = 0; i < blockSize; ++i)
                polyBuffer.setSample(0, i, 1.0f);
            adsrZero.processBlock(polyBuffer, emptyMidi);
        }
        for (int b = 0; b < 10; ++b) {
            polyBuffer.clear(); // gate low -> release
            adsrZero.processBlock(polyBuffer, emptyMidi);
        }
    }

    // Subject variant: sustain = 0.5 isolates the release-time / "amputation" behaviour.
    ADSRModule adsrHalf;
    adsrHalf.prepareToPlay(sampleRate, blockSize);
    setPoly(adsrHalf, true);
    setFloat(adsrHalf, "sustain", 0.5f);
    setFloat(adsrHalf, "attack", 0.01f);
    setFloat(adsrHalf, "decay", 0.05f);
    setFloat(adsrHalf, "release", 1.0f);

    juce::AudioBuffer<float> polyBuffer(8, blockSize);
    juce::MidiBuffer emptyMidi;

    // Settle through attack+decay (~0.06s ~= 6 blocks); use 10 for margin.
    for (int b = 0; b < 10; ++b) {
        polyBuffer.clear();
        for (int i = 0; i < blockSize; ++i)
            polyBuffer.setSample(0, i, 1.0f);
        adsrHalf.processBlock(polyBuffer, emptyMidi);
    }
    const float settled = polyBuffer.getSample(0, blockSize - 1);
    ASSERT_GT(settled, 0.3f) << "did not settle near sustain 0.5 before release started, got " << settled;

    // Drop the gate; scan sample-by-sample for the below-0.01 crossing and the 0.2s checkpoint.
    long long samplesToBelowThreshold = -1;
    float levelAt0p2s = -1.0f;
    const long long samplesAt0p2s = static_cast<long long>(0.2 * sampleRate);
    long long sampleCounter = 0;
    const int maxReleaseBlocks = 300; // generous cap (~3.5s) so a stuck envelope can't hang the test

    for (int b = 0; b < maxReleaseBlocks && samplesToBelowThreshold < 0; ++b) {
        polyBuffer.clear(); // gate channel 0 is 0.0 -> release
        adsrHalf.processBlock(polyBuffer, emptyMidi);
        for (int i = 0; i < blockSize; ++i) {
            const float v = polyBuffer.getSample(0, i);
            if (levelAt0p2s < 0.0f && sampleCounter == samplesAt0p2s)
                levelAt0p2s = v;
            if (samplesToBelowThreshold < 0 && v < 0.01f) {
                samplesToBelowThreshold = sampleCounter;
                break;
            }
            ++sampleCounter;
        }
    }

    ASSERT_GE(samplesToBelowThreshold, 0) << "envelope never fell below 0.01 within the cap";
    const double measuredSeconds = static_cast<double>(samplesToBelowThreshold) / sampleRate;
    std::cout << "[Repro] PolyZeroSustainRelease: measured " << measuredSeconds
              << "s to fall below 0.01 (release=1.0s); level at 0.2s = " << levelAt0p2s << std::endl;
    EXPECT_NEAR(measuredSeconds, 1.0, 0.2)
        << "measured " << measuredSeconds << " seconds to fall below 0.01 with release=1.0s";

    ASSERT_GE(levelAt0p2s, 0.0f) << "release finished before the 0.2s checkpoint could be measured";
    EXPECT_GT(levelAt0p2s, 0.1f) << "measured level " << levelAt0p2s
                                 << " at 0.2s into a 1.0s release (amputation check)";
}

TEST_F(ADSRTest, ReproMonoBackToBackNotesReArticulate) {
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

    float minAfter = buffer.getSample(0, 0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        minAfter = std::min(minAfter, buffer.getSample(0, i));

    // ~0.06s more (~5 blocks total including the transition block).
    for (int b = 0; b < 4; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            minAfter = std::min(minAfter, buffer.getSample(0, i));
    }

    EXPECT_LT(minAfter, 0.5f) << "measured min envelope after transition " << minAfter
                              << " (expected a re-attack dip well below sustain 0.8)";
}

TEST_F(ADSRTest, ReproMonoReleasingOneOfTwoHeldNotesKeepsEnvelopeUp) {
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

// ---------------------------------------------------------------------------
// FRO110 zero-sustain release-amputation hypothesis: with sustain == 0, releaseRate is 0 at
// recalculateRates() time, so a setParameters() call while in the release stage snaps the
// envelope to silence instantly. These tests probe note-off while still in attack/decay (level
// well above 0) with sustain == 0, both mono and poly, plus the sustain == 1 decayRate-== 0
// counterpart.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, ReproMonoZeroSustainReleaseFromMidDecayIsNotAmputated) {
    const double sampleRate = 44100.0;
    const int blockSize = buffer.getNumSamples();

    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "attack", 0.01f);
    setFloat(adsr, "decay", 2.0f);
    setFloat(adsr, "release", 1.0f);

    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    buffer.clear();
    adsr.processBlock(buffer, midiMessages);

    juce::MidiBuffer emptyMidi;
    // ~0.2s total (attack 0.01s + most of a 2.0s decay), so the note-off lands mid-decay.
    const int blocksFor0p2s = static_cast<int>(0.2 * sampleRate / blockSize);
    float levelBeforeNoteOff = buffer.getSample(0, blockSize - 1);
    for (int b = 1; b < blocksFor0p2s; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        levelBeforeNoteOff = buffer.getSample(0, blockSize - 1);
    }

    std::cout << "[Repro] MonoZeroSustainReleaseFromMidDecay: level right before note-off (mid-decay) = "
              << levelBeforeNoteOff << std::endl;
    ASSERT_GT(levelBeforeNoteOff, 0.3f) << "expected mid-decay level well above 0, got " << levelBeforeNoteOff;

    // Note-off now, while still in decay -- releaseRate is computed fresh at this moment.
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear();
    adsr.processBlock(buffer, noteOffMidi);

    // Scan sample-by-sample through release for the below-0.01 crossing and the 0.2s checkpoint.
    long long samplesToBelowThreshold = -1;
    float levelAt0p2sIntoRelease = -1.0f;
    const long long samplesAt0p2s = static_cast<long long>(0.2 * sampleRate);
    long long sampleCounter = 0;
    const int maxReleaseBlocks = 300; // generous cap (~3.5s) so a stuck envelope can't hang the test

    auto scanBlock = [&]() {
        for (int i = 0; i < blockSize; ++i) {
            const float v = buffer.getSample(0, i);
            if (levelAt0p2sIntoRelease < 0.0f && sampleCounter == samplesAt0p2s)
                levelAt0p2sIntoRelease = v;
            if (samplesToBelowThreshold < 0 && v < 0.01f) {
                samplesToBelowThreshold = sampleCounter;
                ++sampleCounter;
                return;
            }
            ++sampleCounter;
        }
    };

    scanBlock(); // the note-off block itself is already part of the release
    for (int b = 0; b < maxReleaseBlocks && samplesToBelowThreshold < 0; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        scanBlock();
    }

    ASSERT_GE(samplesToBelowThreshold, 0) << "envelope never fell below 0.01 within the cap";
    const double secondsToSilence = static_cast<double>(samplesToBelowThreshold) / sampleRate;
    std::cout << "[Repro] MonoZeroSustainReleaseFromMidDecay: secondsToSilence = " << secondsToSilence
              << "s (release=1.0s); levelAt0.2sIntoRelease = " << levelAt0p2sIntoRelease << std::endl;

    EXPECT_GT(levelAt0p2sIntoRelease, 0.1f)
        << "measured level " << levelAt0p2sIntoRelease << " at 0.2s into a 1.0s release (amputation check)";
    EXPECT_GT(secondsToSilence, 0.5f) << "measured " << secondsToSilence
                                      << " seconds to fall below 0.01 with release=1.0s";
}

TEST_F(ADSRTest, ReproMonoZeroSustainReleaseAmputatedByAParameterChange) {
    const double sampleRate = 44100.0;
    const int blockSize = buffer.getNumSamples();

    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "attack", 0.01f);
    setFloat(adsr, "decay", 2.0f);
    setFloat(adsr, "release", 1.0f);

    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    buffer.clear();
    adsr.processBlock(buffer, midiMessages);

    juce::MidiBuffer emptyMidi;
    const int blocksFor0p2s = static_cast<int>(0.2 * sampleRate / blockSize);
    for (int b = 1; b < blocksFor0p2s; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
    }

    // Note-off while mid-decay.
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear();
    adsr.processBlock(buffer, noteOffMidi);

    // ~0.1s into release.
    const int blocksFor0p1s = static_cast<int>(0.1 * sampleRate / blockSize);
    for (int b = 0; b < blocksFor0p1s; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
    }

    const float levelBefore = buffer.getSample(0, blockSize - 1);
    std::cout << "[Repro] MonoZeroSustainReleaseAmputatedByAParameterChange: levelBefore = " << levelBefore
              << std::endl;
    ASSERT_GT(levelBefore, 0.05f) << "release already silent before the parameter change, got " << levelBefore;

    // Force setParameters() to run again while in the release stage -- this is where a
    // freshly-recomputed releaseRate == 0 (from sustain == 0) would snap the envelope to 0.
    setFloat(adsr, "attack", 0.02f);
    buffer.clear();
    adsr.processBlock(buffer, emptyMidi);
    const float levelAfter = buffer.getSample(0, blockSize - 1);

    std::cout << "[Repro] MonoZeroSustainReleaseAmputatedByAParameterChange: levelBefore = " << levelBefore
              << ", levelAfter (post parameter-change block) = " << levelAfter << std::endl;
    EXPECT_GT(levelAfter, 0.5f * levelBefore)
        << "measured levelBefore=" << levelBefore << " levelAfter=" << levelAfter
        << " -- release should continue smoothly across a parameter change, not collapse";
}

TEST_F(ADSRTest, ReproPolyZeroSustainReleaseFromMidDecayIsNotAmputated) {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    setPoly(adsr, true);
    setFloat(adsr, "sustain", 0.0f);
    setFloat(adsr, "attack", 0.01f);
    setFloat(adsr, "decay", 2.0f);
    setFloat(adsr, "release", 1.0f);

    juce::AudioBuffer<float> polyBuffer(8, blockSize);
    juce::MidiBuffer emptyMidi;

    // Gate voice 0 high for ~0.2s (mid-decay); the poly branch calls setParameters() every block.
    const int blocksFor0p2s = static_cast<int>(0.2 * sampleRate / blockSize);
    float levelBeforeGateOff = 0.0f;
    for (int b = 0; b < blocksFor0p2s; ++b) {
        polyBuffer.clear();
        for (int i = 0; i < blockSize; ++i)
            polyBuffer.setSample(0, i, 1.0f);
        adsr.processBlock(polyBuffer, emptyMidi);
        levelBeforeGateOff = polyBuffer.getSample(0, blockSize - 1);
    }

    std::cout << "[Repro] PolyZeroSustainReleaseFromMidDecay: level right before gate-off (mid-decay) = "
              << levelBeforeGateOff << std::endl;
    ASSERT_GT(levelBeforeGateOff, 0.3f) << "expected mid-decay level well above 0, got " << levelBeforeGateOff;

    // Gate low -> release, still under sustain == 0.
    long long samplesToBelowThreshold = -1;
    float levelAt0p2sIntoRelease = -1.0f;
    const long long samplesAt0p2s = static_cast<long long>(0.2 * sampleRate);
    long long sampleCounter = 0;
    const int maxReleaseBlocks = 300;

    for (int b = 0; b < maxReleaseBlocks && samplesToBelowThreshold < 0; ++b) {
        polyBuffer.clear(); // gate channel 0 is 0.0 -> release
        adsr.processBlock(polyBuffer, emptyMidi);
        for (int i = 0; i < blockSize; ++i) {
            const float v = polyBuffer.getSample(0, i);
            if (levelAt0p2sIntoRelease < 0.0f && sampleCounter == samplesAt0p2s)
                levelAt0p2sIntoRelease = v;
            if (samplesToBelowThreshold < 0 && v < 0.01f) {
                samplesToBelowThreshold = sampleCounter;
                break;
            }
            ++sampleCounter;
        }
    }

    ASSERT_GE(samplesToBelowThreshold, 0) << "envelope never fell below 0.01 within the cap";
    const double secondsToSilence = static_cast<double>(samplesToBelowThreshold) / sampleRate;
    std::cout << "[Repro] PolyZeroSustainReleaseFromMidDecay: secondsToSilence = " << secondsToSilence
              << "s (release=1.0s); levelAt0.2sIntoRelease = " << levelAt0p2sIntoRelease << std::endl;

    EXPECT_GT(levelAt0p2sIntoRelease, 0.1f)
        << "measured level " << levelAt0p2sIntoRelease << " at 0.2s into a 1.0s release (amputation check)";
    EXPECT_GT(secondsToSilence, 0.5f) << "measured " << secondsToSilence
                                      << " seconds to fall below 0.01 with release=1.0s";
}

TEST_F(ADSRTest, ReproSustainOneStillDecaysAndReleases) {
    const double sampleRate = 44100.0;
    const int blockSize = buffer.getNumSamples();

    setFloat(adsr, "sustain", 1.0f);
    setFloat(adsr, "attack", 0.01f);
    setFloat(adsr, "decay", 0.5f);
    setFloat(adsr, "release", 0.5f);

    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    buffer.clear();
    adsr.processBlock(buffer, midiMessages);

    juce::MidiBuffer emptyMidi;
    // Hold ~0.3s -- past attack (0.01s) + decay (0.5s is still in progress at 0.3s, but with
    // sustain == 1.0 decayRate == 0 so the level should already sit at 1.0 the whole time.
    const int blocksFor0p3s = static_cast<int>(0.3 * sampleRate / blockSize);
    float heldLevel = buffer.getSample(0, blockSize - 1);
    for (int b = 1; b < blocksFor0p3s; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        heldLevel = buffer.getSample(0, blockSize - 1);
    }

    std::cout << "[Repro] SustainOneStillDecaysAndReleases: held level at ~0.3s = " << heldLevel << std::endl;
    EXPECT_NEAR(heldLevel, 1.0f, 0.05f) << "measured held level " << heldLevel << " (expected ~1.0 at sustain=1.0)";

    // Note-off: measure seconds until below 0.01.
    juce::MidiBuffer noteOffMidi;
    noteOffMidi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buffer.clear();
    adsr.processBlock(buffer, noteOffMidi);

    long long samplesToBelowThreshold = -1;
    long long sampleCounter = 0;
    const int maxReleaseBlocks = 200;

    auto scanBlock = [&]() {
        for (int i = 0; i < blockSize; ++i) {
            const float v = buffer.getSample(0, i);
            if (samplesToBelowThreshold < 0 && v < 0.01f) {
                samplesToBelowThreshold = sampleCounter;
                ++sampleCounter;
                return;
            }
            ++sampleCounter;
        }
    };

    scanBlock();
    for (int b = 0; b < maxReleaseBlocks && samplesToBelowThreshold < 0; ++b) {
        buffer.clear();
        adsr.processBlock(buffer, emptyMidi);
        scanBlock();
    }

    ASSERT_GE(samplesToBelowThreshold, 0) << "envelope never fell below 0.01 within the cap";
    const double secondsToSilence = static_cast<double>(samplesToBelowThreshold) / sampleRate;
    std::cout << "[Repro] SustainOneStillDecaysAndReleases: secondsToSilence = " << secondsToSilence
              << "s (release=0.5s)" << std::endl;
    EXPECT_NEAR(secondsToSilence, 0.5, 0.15)
        << "measured " << secondsToSilence << " seconds to fall below 0.01 with release=0.5s (expected within 30%)";
}
