#include "Modules/OscillatorModule.h"
#include <cmath>
#include <gtest/gtest.h>

class OscillatorCVModulationTest : public ::testing::Test {
protected:
    std::unique_ptr<OscillatorModule> osc;
    static constexpr double sampleRate = 44100.0;
    static constexpr int blockSize = 512;

    void SetUp() override {
        osc = std::make_unique<OscillatorModule>();
        osc->prepareToPlay(sampleRate, blockSize);

        // Send noteOn A4 (440Hz) to set voice 0's lastMidiNote
        juce::AudioBuffer<float> initBuf(14, blockSize);
        initBuf.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8)100), 0);
        osc->processBlock(initBuf, midi);
    }

    static float computeRMS(const juce::AudioBuffer<float>& buf, int channel) {
        float sum = 0.0f;
        for (int i = 0; i < buf.getNumSamples(); ++i) {
            float s = buf.getSample(channel, i);
            sum += s * s;
        }
        return std::sqrt(sum / (float)buf.getNumSamples());
    }

    static float estimateFreqByZeroCrossings(const juce::AudioBuffer<float>& buf, int channel, double sr) {
        int crossings = 0;
        for (int i = 1; i < buf.getNumSamples(); ++i) {
            if ((buf.getSample(channel, i - 1) >= 0.0f) != (buf.getSample(channel, i) >= 0.0f))
                ++crossings;
        }
        return (float)(crossings * sr / (2.0 * buf.getNumSamples()));
    }
};

TEST_F(OscillatorCVModulationTest, OctaveCVShiftsFrequency) {
    // Reference block with no CV
    juce::AudioBuffer<float> refBuf(14, blockSize);
    refBuf.clear();
    juce::MidiBuffer emptyMidi;
    osc->processBlock(refBuf, emptyMidi);
    float refFreq = estimateFreqByZeroCrossings(refBuf, 0, sampleRate);

    // Reset oscillator
    osc->prepareToPlay(sampleRate, blockSize);
    juce::AudioBuffer<float> initBuf(14, blockSize);
    initBuf.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8)100), 0);
    osc->processBlock(initBuf, midi);

    // Octave CV = 0.5 on channel 2 -> octShift = round(0.5 * 4) = 2 octaves up
    juce::AudioBuffer<float> modBuf(14, blockSize);
    modBuf.clear();
    for (int i = 0; i < blockSize; ++i)
        modBuf.setSample(2, i, 0.5f);
    osc->processBlock(modBuf, emptyMidi);
    float modFreq = estimateFreqByZeroCrossings(modBuf, 0, sampleRate);

    // 2 octaves up = ~4x frequency
    EXPECT_GT(modFreq, refFreq * 2.5f) << "Octave CV should shift frequency up significantly";
}

TEST_F(OscillatorCVModulationTest, LevelCVReducesAmplitude) {
    // Reference with default level (1.0)
    juce::AudioBuffer<float> refBuf(14, blockSize);
    refBuf.clear();
    juce::MidiBuffer emptyMidi;
    osc->processBlock(refBuf, emptyMidi);
    float refRMS = computeRMS(refBuf, 0);

    // Level CV = -0.5 on channel 5 -> level = clamp(1.0 + (-0.5)) = 0.5
    juce::AudioBuffer<float> modBuf(14, blockSize);
    modBuf.clear();
    for (int i = 0; i < blockSize; ++i)
        modBuf.setSample(5, i, -0.5f);
    osc->processBlock(modBuf, emptyMidi);
    float modRMS = computeRMS(modBuf, 0);

    EXPECT_LT(modRMS, refRMS * 0.75f) << "Level CV should reduce output amplitude";
    EXPECT_GT(modRMS, 0.01f) << "Output should still have signal";
}

TEST_F(OscillatorCVModulationTest, CoarseCVShiftsFrequency) {
    // Reference
    juce::AudioBuffer<float> refBuf(14, blockSize);
    refBuf.clear();
    juce::MidiBuffer emptyMidi;
    osc->processBlock(refBuf, emptyMidi);
    float refFreq = estimateFreqByZeroCrossings(refBuf, 0, sampleRate);

    // Reset
    osc->prepareToPlay(sampleRate, blockSize);
    juce::AudioBuffer<float> initBuf(14, blockSize);
    initBuf.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8)100), 0);
    osc->processBlock(initBuf, midi);

    // Coarse CV = 1.0 on channel 3 -> coarseShift = round(1.0 * 12) = 12 semitones = 1 octave
    juce::AudioBuffer<float> modBuf(14, blockSize);
    modBuf.clear();
    for (int i = 0; i < blockSize; ++i)
        modBuf.setSample(3, i, 1.0f);
    osc->processBlock(modBuf, emptyMidi);
    float modFreq = estimateFreqByZeroCrossings(modBuf, 0, sampleRate);

    EXPECT_GT(modFreq, refFreq * 1.5f) << "Coarse CV should shift frequency up";
}

// ============================================================
// Poly-mode CV modulation tests
// ============================================================

// Helper to enable poly mode on an oscillator
static void enablePolyMode(OscillatorModule& osc_) {
    for (auto* p : osc_.getParameters()) {
        if (p->getName(100) == "Poly") {
            if (auto* bp = dynamic_cast<juce::AudioParameterBool*>(p))
                *bp = true;
            break;
        }
    }
}

TEST(OscillatorPolyModulationTest, PolyLevelCVReducesAmplitude) {
    constexpr double sr = 44100.0;
    constexpr int N = 512;

    // --- Baseline: Level CV = 0 ---
    OscillatorModule oscBase;
    enablePolyMode(oscBase);
    oscBase.prepareToPlay(sr, N);

    juce::AudioBuffer<float> baseBuf(14, N);
    baseBuf.clear();
    for (int i = 0; i < N; ++i)
        baseBuf.setSample(0, i, 440.0f); // voice 0 pitch = 440 Hz
    juce::MidiBuffer emptyMidi;
    oscBase.processBlock(baseBuf, emptyMidi);
    float rmsBase = 0.0f;
    for (int i = 0; i < N; ++i) {
        float s = baseBuf.getSample(0, i);
        rmsBase += s * s;
    }
    rmsBase = std::sqrt(rmsBase / N);

    // --- CV run: Level CV = -0.5 on ch12 ---
    OscillatorModule oscCut;
    enablePolyMode(oscCut);
    oscCut.prepareToPlay(sr, N);

    juce::AudioBuffer<float> cutBuf(14, N);
    cutBuf.clear();
    for (int i = 0; i < N; ++i) {
        cutBuf.setSample(0, i, 440.0f);
        cutBuf.setSample(12, i, -0.5f); // Level CV = -0.5 -> level clamp(1.0 - 0.5) = 0.5
    }
    oscCut.processBlock(cutBuf, emptyMidi);
    float rmsCut = 0.0f;
    for (int i = 0; i < N; ++i) {
        float s = cutBuf.getSample(0, i);
        rmsCut += s * s;
    }
    rmsCut = std::sqrt(rmsCut / N);

    EXPECT_GT(rmsBase, 0.01f) << "Baseline should have signal";
    EXPECT_LT(rmsCut, rmsBase * 0.8f) << "Level CV -0.5 should audibly reduce amplitude";
}

TEST(OscillatorPolyModulationTest, PolyOctaveCVShiftsFrequency) {
    constexpr double sr = 44100.0;
    constexpr int N = 512;

    auto countZeroCrossings = [&](const juce::AudioBuffer<float>& buf, int ch) {
        int xings = 0;
        for (int i = 1; i < N; ++i)
            if ((buf.getSample(ch, i - 1) >= 0.0f) != (buf.getSample(ch, i) >= 0.0f))
                ++xings;
        return xings;
    };

    // --- Baseline: no octave CV ---
    OscillatorModule oscBase;
    enablePolyMode(oscBase);
    oscBase.prepareToPlay(sr, N);

    juce::AudioBuffer<float> baseBuf(14, N);
    baseBuf.clear();
    for (int i = 0; i < N; ++i)
        baseBuf.setSample(0, i, 220.0f); // 220 Hz
    juce::MidiBuffer emptyMidi;
    oscBase.processBlock(baseBuf, emptyMidi);
    int xBase = countZeroCrossings(baseBuf, 0);

    // --- CV run: Octave CV = +0.25 on ch9 -> round(0.25*4)=1 octave up -> ~440 Hz ---
    OscillatorModule oscOct;
    enablePolyMode(oscOct);
    oscOct.prepareToPlay(sr, N);

    juce::AudioBuffer<float> octBuf(14, N);
    octBuf.clear();
    for (int i = 0; i < N; ++i) {
        octBuf.setSample(0, i, 220.0f);
        octBuf.setSample(9, i, 0.25f); // Octave CV = +0.25
    }
    oscOct.processBlock(octBuf, emptyMidi);
    int xOct = countZeroCrossings(octBuf, 0);

    EXPECT_GT(xBase, 0) << "Baseline should have zero crossings";
    float ratio = (xBase > 0) ? (float)xOct / (float)xBase : 0.0f;
    // Expect ~2x zero crossings (1 octave up), allow tolerance [1.6, 2.4]
    EXPECT_GT(ratio, 1.6f) << "Octave CV should roughly double frequency; ratio=" << ratio;
    EXPECT_LT(ratio, 2.4f) << "Octave CV should not more than double+tolerance; ratio=" << ratio;
}

TEST(OscillatorPolyModulationTest, PolyWaveformCVCrossfadeNoClick) {
    // Verify that a mid-block waveform boundary crossing produces no audible click
    // (max adjacent-sample delta must be below a threshold even as waveform changes).
    constexpr double sr = 44100.0;
    constexpr int N = 256; // >= 128 as required; 2 * CROSSFADE_SAMPLES = 128
    juce::MidiBuffer emptyMidi;

    OscillatorModule osc;
    enablePolyMode(osc);
    osc.prepareToPlay(sr, N);

    // Build buffer: voice 0 at 220 Hz constant pitch (ch0).
    // Waveform CV (ch8): 0.0f for first half, 0.34f for second half.
    //   round(0.0  * 3) = 0  -> waveform index = base (0, Sine)
    //   round(0.34 * 3) = 1  -> waveform index = base+1 (1, Square)
    // This forces exactly one boundary crossing at sample N/2.
    juce::AudioBuffer<float> buf(14, N);
    buf.clear();
    for (int i = 0; i < N; ++i) {
        buf.setSample(0, i, 220.0f);
        buf.setSample(8, i, (i < N / 2) ? 0.0f : 0.34f);
    }

    osc.processBlock(buf, emptyMidi);

    // Measure max absolute adjacent-sample delta across the whole output block.
    float maxDelta = 0.0f;
    for (int i = 1; i < N; ++i) {
        float delta = std::abs(buf.getSample(0, i) - buf.getSample(0, i - 1));
        if (delta > maxDelta)
            maxDelta = delta;
    }

    // Crossfade should smooth the transition: no instantaneous jump >= 0.5.
    EXPECT_LT(maxDelta, 0.5f) << "Waveform CV crossfade should suppress clicks; maxDelta=" << maxDelta;

    // Verify non-zero output (audible signal present).
    float rms = 0.0f;
    for (int i = 0; i < N; ++i) {
        float s = buf.getSample(0, i);
        rms += s * s;
    }
    rms = std::sqrt(rms / N);
    EXPECT_GT(rms, 0.01f) << "Output should be non-zero after waveform CV crossfade";
}

TEST(OscillatorPolyModulationTest, PolyZeroCVEqualsBaseline) {
    constexpr double sr = 44100.0;
    constexpr int N = 512;
    juce::MidiBuffer emptyMidi;

    // Run A: 14-channel buffer with channels 8-13 all zero
    OscillatorModule oscA;
    enablePolyMode(oscA);
    oscA.prepareToPlay(sr, N);

    juce::AudioBuffer<float> bufA(14, N);
    bufA.clear();
    for (int i = 0; i < N; ++i)
        bufA.setSample(0, i, 440.0f);
    // channels 8-13 remain zero (already cleared)
    oscA.processBlock(bufA, emptyMidi);

    // Run B: 8-channel buffer (no CV channels 8-13 present at all)
    OscillatorModule oscB;
    enablePolyMode(oscB);
    oscB.prepareToPlay(sr, N);

    juce::AudioBuffer<float> bufB(8, N);
    bufB.clear();
    for (int i = 0; i < N; ++i)
        bufB.setSample(0, i, 440.0f);
    // No crash expected even with numChannels <= 12
    oscB.processBlock(bufB, emptyMidi); // must not OOB-read

    // Both outputs should be equal sample-by-sample within 1e-5
    for (int i = 0; i < N; ++i) {
        EXPECT_NEAR(bufA.getSample(0, i), bufB.getSample(0, i), 1e-5f)
            << "Sample " << i << " differs: A=" << bufA.getSample(0, i) << " B=" << bufB.getSample(0, i);
    }
}
