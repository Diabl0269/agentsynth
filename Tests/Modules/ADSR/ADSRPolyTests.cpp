// ADSRPolyTests.cpp
// Poly mode: 8 independent per-voice envelopes gated by per-voice Gate CV, plus the FRO110
// zero-sustain release-amputation repros in poly.

#include "ADSRTestFixture.h"

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

// ---------------------------------------------------------------------------
// Repro* tests: TESTS-FIRST diagnostics for four suspected ADSR bugs (FRO110).
// These are pure repro/measurement tests — no Source/ changes accompany them.
// Each prints the actual measured number in its failure message so the real
// behaviour is visible whether the assertion passes or fails.
// ---------------------------------------------------------------------------

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
