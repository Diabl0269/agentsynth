// The Oscillator's Gate input: a rising edge restarts the voice's phase at Phase/Rand on its own sample,
// in poly (one gate channel per voice) and in mono (one channel), without any MIDI note-on.

#include "../Engine/AudioRendering/ClickDetector.h"
#include "Modules/OscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kFade = 64; // OscillatorModule::CROSSFADE_SAMPLES
constexpr float kPitchHz = 220.0f;
constexpr double kTwoPi = 6.283185307179586;

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct GateBlock {
    int riseAt = -1; ///< sample where the gate goes high; -1 = stays low
    int fallAt = -1; ///< sample where it goes low again; -1 = stays as it is to the end of the block
};

// Renders voice 0 block by block: a held 220 Hz pitch CV in poly (what Poly MIDI leaves after a
// note-off) and the gate on the module's gate channel.
struct GateRig {
    OscillatorModule osc;
    bool poly;
    bool patchGate;
    int channels = OscillatorModule::kNumOutputs;
    bool gateHigh = false;

    GateRig(bool polyMode, bool gatePatched)
        : poly(polyMode)
        , patchGate(gatePatched) {
        setActual(osc, "phase", 90.0f);
        setActual(osc, "randomPhase", 0.0f);
        setActual(osc, "unison", 1.0f);
        if (polyMode)
            setActual(osc, "poly", 1.0f);
        osc.prepareToPlay(kSampleRate, kBlock);
    }

    std::vector<float> renderBlock(const GateBlock& gate) {
        juce::AudioBuffer<float> buf(channels, kBlock);
        buf.clear();
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(0), kPitchHz, kBlock);
        if (patchGate) {
            float* g = buf.getWritePointer(OscillatorModule::kGateBase);
            for (int s = 0; s < kBlock; ++s) {
                if (s == gate.riseAt)
                    gateHigh = true;
                if (s == gate.fallAt)
                    gateHigh = false;
                g[s] = gateHigh ? 1.0f : 0.0f;
            }
        }
        juce::MidiBuffer midi;
        osc.processBlock(buf, midi);
        return {buf.getReadPointer(0), buf.getReadPointer(0) + kBlock};
    }
};

float maxDiff(const std::vector<float>& a, int aFrom, const std::vector<float>& b, int bFrom, int count) {
    float d = 0.0f;
    for (int i = 0; i < count; ++i)
        d = std::max(d, std::abs(a[(size_t)(aFrom + i)] - b[(size_t)(bFrom + i)]));
    return d;
}

// Two notes by gate edge only: note 1 at sample 100 of block A (gate low again at 200), note 2 at sample 300
// of block B. Returns the two blocks.
std::pair<std::vector<float>, std::vector<float>> twoNotes(GateRig& rig) {
    auto a = rig.renderBlock({100, 200});
    auto b = rig.renderBlock({300, -1});
    return {a, b};
}

} // namespace

TEST(OscillatorGateRestart, PolyEdgeRestartsThePhaseOnItsOwnSample) {
    GateRig rig(/*poly*/ true, /*gate*/ true);
    const auto [a, b] = twoNotes(rig);

    const double dt = (double)kPitchHz / kSampleRate;
    for (const int edge : {100}) {
        // The run-out of the old phases is gone 64 samples after the edge; what is left is the sine
        // started at 90 degrees.
        const int at = edge + kFade;
        EXPECT_NEAR(a[(size_t)at], std::sin(kTwoPi * (0.25 + (double)kFade * dt)), 1e-3);
    }
    EXPECT_NEAR(b[300 + kFade], std::sin(kTwoPi * (0.25 + (double)kFade * dt)), 1e-3);

    // Both notes then run the same samples: the restart landed at the same phase on the exact sample.
    EXPECT_EQ(maxDiff(a, 100 + kFade, b, 300 + kFade, kBlock - 300 - kFade), 0.0f);
}

TEST(OscillatorGateRestart, PolyWithoutAGateTheTwoNotesAreNotAligned) {
    GateRig rig(/*poly*/ true, /*gate*/ false);
    const auto [a, b] = twoNotes(rig);

    EXPECT_GT(maxDiff(a, 100 + kFade, b, 300 + kFade, kBlock - 300 - kFade), 0.1f)
        << "the held pitch never restarts the voice, so only the gate can align the notes";
}

TEST(OscillatorGateRestart, PolyVoiceOneRestartsFromItsOwnGateChannelOnly) {
    OscillatorModule osc;
    setActual(osc, "phase", 90.0f);
    setActual(osc, "unison", 1.0f);
    setActual(osc, "poly", 1.0f);
    osc.prepareToPlay(kSampleRate, kBlock);
    const auto run = [&](bool gateOnVoice1) {
        osc.prepareToPlay(kSampleRate, kBlock);
        std::vector<float> out0, out1;
        for (int block = 0; block < 2; ++block) {
            juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
            buf.clear();
            juce::FloatVectorOperations::fill(buf.getWritePointer(0), kPitchHz, kBlock);
            juce::FloatVectorOperations::fill(buf.getWritePointer(1), kPitchHz, kBlock);
            if (gateOnVoice1 && block == 1)
                juce::FloatVectorOperations::fill(buf.getWritePointer(OscillatorModule::kGateBase + 1) + 300, 1.0f,
                                                  kBlock - 300);
            juce::MidiBuffer midi;
            osc.processBlock(buf, midi);
            out0.insert(out0.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
            out1.insert(out1.end(), buf.getReadPointer(1), buf.getReadPointer(1) + kBlock);
        }
        return std::make_pair(out0, out1);
    };
    const auto without = run(false);
    const auto with = run(true);
    EXPECT_EQ(maxDiff(with.first, 0, without.first, 0, 2 * kBlock), 0.0f) << "voice 0 has no gate edge";
    EXPECT_GT(maxDiff(with.second, kBlock + 300 + kFade, without.second, kBlock + 300 + kFade, 64), 0.05f)
        << "voice 1 restarted";
}

TEST(OscillatorGateRestart, MonoEdgeRestartsWithoutAnyMidi) {
    GateRig rig(/*poly*/ false, /*gate*/ true);
    const auto [a, b] = twoNotes(rig);
    EXPECT_EQ(maxDiff(a, 100 + kFade, b, 300 + kFade, kBlock - 300 - kFade), 0.0f);

    GateRig control(/*poly*/ false, /*gate*/ false);
    const auto [ca, cb] = twoNotes(control);
    EXPECT_GT(maxDiff(ca, 100 + kFade, cb, 300 + kFade, kBlock - 300 - kFade), 0.1f);
}

TEST(OscillatorGateRestart, AnUnpatchedGateLeavesTheOutputUnchanged) {
    for (const bool poly : {false, true}) {
        // Gate channels present but all zero, against a buffer that has no gate channels at all.
        GateRig withChannels(poly, /*gate*/ true);
        GateRig withoutChannels(poly, /*gate*/ false);
        withoutChannels.channels = OscillatorModule::kGateBase;
        for (int block = 0; block < 3; ++block) {
            const auto x = withChannels.renderBlock({-1, -1});
            const auto y = withoutChannels.renderBlock({-1, -1});
            EXPECT_EQ(maxDiff(x, 0, y, 0, kBlock), 0.0f) << (poly ? "poly" : "mono") << " block " << block;
        }
    }
}

TEST(OscillatorGateRestart, GateChannelsAreMappedInBothModes) {
    OscillatorModule osc;
    const auto mono = osc.mapInputChannel(OscillatorModule::kGateBase);
    EXPECT_EQ(mono.visibleJackIndex, OscillatorModule::kJackGate);
    EXPECT_EQ(mono.role, PortRole::Gate);
    EXPECT_TRUE(mono.isPolyGroupHead);
    EXPECT_EQ(mono.polyVoiceSpan, 1);
    EXPECT_NE(osc.mapInputChannel(OscillatorModule::kGateBase + 1).role, PortRole::Gate) << "mono has one gate channel";

    setActual(osc, "poly", 1.0f);
    for (int v = 0; v < OscillatorModule::kNumVoices; ++v) {
        const auto p = osc.mapInputChannel(OscillatorModule::kGateBase + v);
        EXPECT_EQ(p.visibleJackIndex, OscillatorModule::kJackGate) << v;
        EXPECT_EQ(p.role, PortRole::Gate) << v;
        EXPECT_EQ(p.isPolyGroupHead, v == 0) << v;
        EXPECT_EQ(p.polyVoiceSpan, v == 0 ? OscillatorModule::kNumVoices : 1) << v;
    }
    EXPECT_EQ(osc.getInputPortLabel(OscillatorModule::kJackGate), "Restart");
    EXPECT_EQ(osc.getVisibleInputPortCount(), OscillatorModule::kNumJacks);
    for (const auto& t : osc.getModulationTargets())
        EXPECT_NE(t.name, "Gate") << "the Gate input is a trigger, not a modulation target";
}

TEST(OscillatorGateRestart, OutputsStayAtOrAboveEveryInputAndAudioRDoesNotMove) {
    static_assert(OscillatorModule::kNumOutputs >= OscillatorModule::kNumInputs);
    EXPECT_EQ(OscillatorModule::kRightBase, 14);
    EXPECT_EQ(OscillatorModule::kNumInputs, 28);
    EXPECT_EQ(OscillatorModule::kNumOutputs, 28);

    OscillatorModule osc;
    setActual(osc, "poly", 1.0f);
    // Outputs 22-27 are silent pass-throughs, never a poly head and never Audio R.
    for (int raw = OscillatorModule::kRightBase + OscillatorModule::kNumVoices; raw < OscillatorModule::kNumOutputs;
         ++raw) {
        EXPECT_FALSE(osc.mapOutputChannel(raw).isPolyGroupHead) << raw;
        EXPECT_EQ(osc.mapOutputChannel(raw).visibleJackIndex, 0) << raw;
    }
}

namespace {

struct GateRun {
    std::vector<float> out;
    std::vector<int> edges; ///< the samples where the gate rose, after the first
};

// Mono Sine at 220 Hz (a held MIDI note), gated by a square: high for `high` of every `period` samples.
GateRun renderGatePattern(int period, int high, int totalSamples, int block, bool alwaysHigh = false) {
    OscillatorModule osc;
    setActual(osc, "unison", 1.0f);
    osc.prepareToPlay(kSampleRate, block);
    GateRun run;
    for (int pos = 0; pos < totalSamples; pos += block) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, block);
        buf.clear();
        float* gate = buf.getWritePointer(OscillatorModule::kGateBase);
        for (int s = 0; s < block; ++s) {
            const int t = pos + s;
            gate[s] = (alwaysHigh || (t % period) < high) ? 1.0f : 0.0f;
            if (t > 0 && t % period == 0)
                run.edges.push_back(t);
        }
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        osc.processBlock(buf, midi);
        run.out.insert(run.out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + block);
    }
    return run;
}

// The largest sample-to-sample step in the 64 samples a restart hands over in, and one either side.
float worstStepAround(const std::vector<float>& x, int edge) {
    float worst = 0.0f;
    for (int i = std::max(1, edge - 1); i < edge + kFade + 2 && i < (int)x.size(); ++i)
        worst = std::max(worst, std::abs(x[(size_t)i] - x[(size_t)i - 1]));
    return worst;
}

// The wave's own steepest step is 2 pi 220 / 44100 = 0.031; a hand-over between two such waves adds at most a
// raised cosine across their difference (<= 2) over 64 samples, 0.05 per sample, so 0.12 leaves headroom for both
// and still catches a step.
constexpr float kRestartStepBound = 0.12f;

} // namespace

// Regression test for FRO1082: every restart from a pulse left a corner at the start and the end of its hand-over.
TEST(OscillatorGateRestart, SubAudioGatesRestartWithoutAClickAtEveryEdge) {
    struct Pattern {
        const char* name;
        int period, high;
    };
    for (const Pattern& p : {Pattern{"1-sample pulse", 4410, 1}, Pattern{"10-sample pulse", 4410, 10},
                             Pattern{"5 Hz square", 8820, 4410}}) {
        const auto run = renderGatePattern(p.period, p.high, 44100, kBlock);
        ASSERT_GE(run.edges.size(), 4u) << p.name;
        std::vector<float> afterFirstNote(run.out.begin() + 4096, run.out.end());
        const auto clicks = clickdetector::findClicks(afterFirstNote);
        EXPECT_TRUE(clicks.empty()) << p.name << ": " << clicks.size() << " clicks, first at sample "
                                    << (clicks.empty() ? 0 : clicks[0].index + 4096);
        for (const int edge : run.edges)
            EXPECT_LE(worstStepAround(run.out, edge), kRestartStepBound) << p.name << " edge " << edge;
    }
}

// Regression test for FRO1082: a restart inside the previous 64-sample hand-over copied voices that were themselves
// mid-fade, which stepped the output by up to half the wave's height at every edge above about 700 Hz.
TEST(OscillatorGateRestart, AudioRateGatesNeverStepTheOutputAtARestart) {
    for (const int period : {44, 44100 / 3000, 44100 / 5000}) { // 1 kHz, 3 kHz, 5 kHz
        const auto run = renderGatePattern(period, period / 2, 22050, kBlock);
        float worst = 0.0f;
        for (size_t i = 4096; i < run.out.size(); ++i)
            worst = std::max(worst, std::abs(run.out[i] - run.out[i - 1]));
        EXPECT_LE(worst, kRestartStepBound) << "period " << period << ": a step of " << worst;
        float peak = 0.0f;
        for (float v : run.out)
            peak = std::max(peak, std::abs(v));
        EXPECT_LE(peak, 1.0f + 1e-4f) << "period " << period;
    }
}

// Regression test for FRO1082: edges past a per-block cap were dropped, so the restart pattern of a fast gate
// changed with the block size. The same gate must restart on the same samples whatever the host's block.
TEST(OscillatorGateRestart, AFastGateRestartsTheSameWhateverTheBlockSize) {
    const auto small = renderGatePattern(44, 22, 24576, 128);
    const auto large = renderGatePattern(44, 22, 24576, 2048);
    ASSERT_EQ(small.out.size(), large.out.size());
    int firstDiff = -1;
    for (size_t i = 0; i < small.out.size() && firstDiff < 0; ++i)
        if (small.out[i] != large.out[i])
            firstDiff = (int)i;
    EXPECT_EQ(firstDiff, -1) << "the outputs first differ at sample " << firstDiff;
}

TEST(OscillatorGateRestart, EdgesCloserThanTheSpacingRestartOnlyOnceAcrossBlocks) {
    GateEdges edges;
    std::vector<float> gate(64, 0.0f);
    for (int s = 0; s < 64; s += 4) // a rising edge every 4 samples
        gate[(size_t)s] = 1.0f, gate[(size_t)s + 2] = 0.0f;
    std::vector<int> kept;
    for (int block = 0; block < 4; ++block) {
        edges.scan(gate.data(), 64);
        for (int i = 0; i < edges.count; ++i)
            kept.push_back(block * 64 + edges.offsets[(size_t)i]);
    }
    ASSERT_GE(kept.size(), 8u);
    for (size_t i = 1; i < kept.size(); ++i)
        EXPECT_GE(kept[i] - kept[i - 1], GateEdges::kMinSpacing) << "edges " << kept[i - 1] << " and " << kept[i];
}
