// ADSRStageTests.cpp
// Stage progression: attack/decay/sustain/release timing and levels in mono, including the
// zero-sustain and sustain==1 edge cases and the FRO110 amputation repros.

#include "ADSRTestFixture.h"

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
