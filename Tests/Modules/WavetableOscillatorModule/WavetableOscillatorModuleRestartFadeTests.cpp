// WavetableOscillatorModuleRestartFadeTests.cpp
// A note on a voice that still sounds (MIDI note-on, or a rising edge on the Restart input) restarts the phases on
// its own sample and hands over from the old sound along a raised cosine, so it never clicks: a touching note, a
// slow pulse and an audio-rate gate all stay inside the wave's own slope.

#include "../../Engine/AudioRendering/ClickDetector.h"
#include "WavetableOscillatorModuleTestFixture.h"

namespace {

using WT = WavetableOscillatorModule;
constexpr int kFade = 64; // synth::kRestartFadeSamples
constexpr double kTwoPi = 6.283185307179586;
constexpr float kRestartStepBound = 0.12f; // see OscillatorGateRestartTests: the wave's own step plus a 64-sample fade
constexpr float kSyncStepBound = 0.25f;    // audio-rate restarts hand over in 8 samples, as on the Oscillator

void configure(WT& m, bool poly) {
    setFloat(m, "level", 1.0f);
    setFloat(m, "position", 0.0f); // the first frame of Basic Shapes is a sine
    setFloat(m, "phase", 90.0f);
    setFloat(m, "randomPhase", 0.0f);
    setInt(m, "unison", 1);
    setBool(m, "poly", poly);
    m.prepareToPlay(kSampleRate, kBlockSize);
}

struct GateRun {
    std::vector<float> out;
    std::vector<int> edges; ///< the samples where the gate rose, after the first
};

// A held 220 Hz sine (a MIDI note in mono, a pitch CV in poly) gated by a square: high for `high` of every
// `period` samples.
GateRun renderGatePattern(bool poly, int period, int high, int totalSamples, int block) {
    WT m;
    configure(m, poly);
    m.prepareToPlay(kSampleRate, block);
    GateRun run;
    for (int pos = 0; pos < totalSamples; pos += block) {
        juce::AudioBuffer<float> buf(WT::kNumOutputs, block);
        buf.clear();
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(0), 220.0f, block);
        float* gate = buf.getWritePointer(poly ? WT::kGateBase : WT::kJackGate);
        for (int s = 0; s < block; ++s) {
            const int t = pos + s;
            gate[s] = (t % period) < high ? 1.0f : 0.0f;
            if (t > 0 && t % period == 0)
                run.edges.push_back(t);
        }
        juce::MidiBuffer midi;
        if (pos == 0 && !poly)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        m.processBlock(buf, midi);
        run.out.insert(run.out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + block);
    }
    return run;
}

float worstStepAround(const std::vector<float>& x, int edge) {
    float worst = 0.0f;
    for (int i = std::max(1, edge - 1); i < edge + kFade + 2 && i < (int)x.size(); ++i)
        worst = std::max(worst, std::abs(x[(size_t)i] - x[(size_t)i - 1]));
    return worst;
}

// Blocks of a held note 57 (220 Hz): the second note-on is note 57 again at `offset` of block `noteBlock`, or none.
std::vector<float> renderTwoNotes(int noteBlock, int offset, bool secondNote) {
    WT m;
    configure(m, false);
    std::vector<float> out;
    for (int b = 0; b < 6; ++b) {
        juce::AudioBuffer<float> buf(WT::kNumOutputs, kBlockSize);
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0 || (secondNote && b == noteBlock))
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), b == 0 ? 0 : offset);
        m.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlockSize);
    }
    return out;
}

} // namespace

// Regression test for FRO1129: a MIDI note-on restarted the phase at the start of its block whatever its sample
// offset, with no fade.
TEST(WavetableRestartFade, ANoteOnLandsOnItsOwnSampleAndLeavesTheSoundBeforeItAlone) {
    const int noteBlock = 3, offset = 100;
    const auto control = renderTwoNotes(noteBlock, offset, false);
    const auto played = renderTwoNotes(noteBlock, offset, true);
    const int edge = noteBlock * kBlockSize + offset;

    for (int i = 0; i < edge; ++i)
        ASSERT_EQ(played[(size_t)i], control[(size_t)i]) << "sample " << i << " changed before the note-on";

    // Past the hand-over the wave is the sine restarted at 90 degrees on the note-on sample.
    const double dt = 220.0 / kSampleRate;
    const float amplitude = peakOf(played.data(), edge + kFade, kBlockSize);
    EXPECT_NEAR(played[(size_t)(edge + kFade)], std::sin(kTwoPi * (0.25 + (double)kFade * dt)) * amplitude, 2e-3);
    EXPECT_LE(worstStepAround(played, edge), kRestartStepBound);
}

// Regression test for FRO1129: touching notes at every pitch clicked at each note start (the output stepped by
// up to the wave's full height).
TEST(WavetableRestartFade, TouchingNotesDoNotClick) {
    for (const int spacing : {6000, 11 * kBlockSize}) {
        WT m;
        configure(m, false);
        setInt(m, "unison", 3);
        setFloat(m, "detune", 30.0f);
        const int pitches[] = {68, 52, 58, 51, 53, 48, 66, 69};
        const int total = spacing * 8 + 4096;
        std::vector<float> out;
        for (int pos = 0; pos < total; pos += kBlockSize) {
            juce::AudioBuffer<float> buf(WT::kNumOutputs, kBlockSize);
            buf.clear();
            juce::MidiBuffer midi;
            for (int n = 0; n < 8; ++n)
                if (n * spacing >= pos && n * spacing < pos + kBlockSize)
                    midi.addEvent(juce::MidiMessage::noteOn(1, pitches[n], 0.8f), n * spacing - pos);
            m.processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlockSize);
        }
        const auto clicks = clickdetector::findClicks(out);
        EXPECT_TRUE(clicks.empty()) << "spacing " << spacing << ": " << clicks.size() << " clicks, first at sample "
                                    << (clicks.empty() ? 0 : clicks[0].index);
    }
}

// Regression test for FRO1129: the Restart input of the Wavetable, like the Oscillator's, restarts a slow pulse
// in time with it and never clicks.
TEST(WavetableRestartFade, SubAudioGatesRestartWithoutAClickAtEveryEdge) {
    struct Pattern {
        const char* name;
        int period, high;
    };
    for (const bool poly : {false, true})
        for (const Pattern& p : {Pattern{"1-sample pulse", 4410, 1}, Pattern{"10-sample pulse", 4410, 10},
                                 Pattern{"5 Hz square", 8820, 4410}}) {
            const auto run = renderGatePattern(poly, p.period, p.high, 44100, kBlockSize);
            ASSERT_GE(run.edges.size(), 4u) << p.name;
            std::vector<float> afterFirstNote(run.out.begin() + 4096, run.out.end());
            const auto clicks = clickdetector::findClicks(afterFirstNote);
            EXPECT_TRUE(clicks.empty()) << (poly ? "poly " : "mono ") << p.name << ": " << clicks.size()
                                        << " clicks, first at sample " << (clicks.empty() ? 0 : clicks[0].index + 4096);
            for (const int edge : run.edges)
                EXPECT_LE(worstStepAround(run.out, edge), kRestartStepBound)
                    << (poly ? "poly " : "mono ") << p.name << " edge " << edge;
        }
}

// Regression test for FRO1129: an audio-rate gate restarts the voice inside the previous hand-over every few
// samples; the output must never step however fast the restarts come.
TEST(WavetableRestartFade, AudioRateGatesNeverStepTheOutputAtARestart) {
    for (const bool poly : {false, true})
        for (const int period : {44, 44100 / 3000, 44100 / 5000}) { // 1 kHz, 3 kHz, 5 kHz
            const auto run = renderGatePattern(poly, period, period / 2, 22050, kBlockSize);
            float worst = 0.0f, peak = 0.0f;
            for (size_t i = 4096; i < run.out.size(); ++i)
                worst = std::max(worst, std::abs(run.out[i] - run.out[i - 1]));
            for (float v : run.out)
                peak = std::max(peak, std::abs(v));
            EXPECT_LE(worst, kSyncStepBound)
                << (poly ? "poly" : "mono") << " period " << period << ": a step of " << worst;
            EXPECT_LE(peak, 1.0f + 1e-4f) << (poly ? "poly" : "mono") << " period " << period;
        }
}

// A fast gate restarts on the same samples whatever the host's block size. The mip is chosen per block, so the
// outputs agree to the read's interpolation error rather than bit for bit.
TEST(WavetableRestartFade, AFastGateRestartsTheSameWhateverTheBlockSize) {
    const auto small = renderGatePattern(false, 44, 22, 24576, 128);
    const auto large = renderGatePattern(false, 44, 22, 24576, 2048);
    ASSERT_EQ(small.out.size(), large.out.size());
    float worst = 0.0f;
    for (size_t i = 4096; i < small.out.size(); ++i)
        worst = std::max(worst, std::abs(small.out[i] - large.out[i]));
    EXPECT_LT(worst, 0.01f) << "a restart that moved by a sample would differ by about 0.03";
}
