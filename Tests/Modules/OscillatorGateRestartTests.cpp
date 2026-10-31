// The Oscillator's Gate input: a rising edge restarts the voice's phase at Phase/Rand on its own sample,
// in poly (one gate channel per voice) and in mono (one channel), without any MIDI note-on.

#include "../Engine/AudioRendering/ClickDetector.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
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

// Audio-rate restarts hand over in 8 samples; for this test's sine the difference at an edge is at most about 1, a
// slope of pi / 2 / 8 = 0.2 at the fade's middle, so 0.25 leaves room for the wave's slope and still catches a step.
constexpr float kSyncStepBound = 0.25f;

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
        EXPECT_LE(worst, kSyncStepBound) << "period " << period << ": a step of " << worst;
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

namespace {

constexpr double kSyncRate = 48000.0;
constexpr int kSyncBlock = 512;
constexpr int kSyncSkip = 9600; // past the first note's attack and decay
constexpr int kSyncSeconds = 48000;

struct SyncCase {
    int masterWave; // 0 Sine, 1 Square, 2 Saw, 3 Triangle
    int masterHz;
    int unison;
    float rand;
    bool poly;
    int targetWave = 2; // Saw
    int note = 57;      // 220 Hz, so 301 Hz is a ratio with no small fraction
};

juce::String describeCase(const SyncCase& c) {
    static const char* waves[] = {"Sine", "Square", "Saw", "Triangle"};
    return juce::String(waves[c.masterWave]) + " " + juce::String(c.masterHz) + " Hz, unison " +
           juce::String(c.unison) + ", rand " + juce::String((int)c.rand) + (c.poly ? ", poly" : ", mono");
}

// A real Oscillator on a Fixed rate, its Audio L cabled into the Restart jack of a target Oscillator that holds a
// note (in poly, a three-note chord with each voice restarted by the master's matching voice). The target's output
// passes an ADSR and a VCA at full sustain. Returns one second of the result, past the first note's attack.
std::vector<float> renderSyncedNote(const SyncCase& c, bool patched = true) {
    auto master = std::make_unique<OscillatorModule>();
    auto target = std::make_unique<OscillatorModule>();
    auto adsr = std::make_unique<ADSRModule>();
    auto vca = std::make_unique<VCAModule>();
    setActual(*master, "waveform", (float)c.masterWave);
    setActual(*master, "rateMode", 1.0f);
    setActual(*master, "fixedRate", (float)c.masterHz);
    setActual(*target, "waveform", (float)c.targetWave);
    setActual(*target, "unison", (float)c.unison);
    setActual(*target, "detune", c.unison > 1 ? 20.0f : 0.0f);
    setActual(*target, "randomPhase", c.rand);
    if (c.poly) {
        setActual(*master, "poly", 1.0f);
        setActual(*target, "poly", 1.0f);
    }
    setActual(*adsr, "attack", 0.001f);
    setActual(*adsr, "sustain", 1.0f);
    for (juce::AudioProcessor* p :
         {static_cast<juce::AudioProcessor*>(master.get()), static_cast<juce::AudioProcessor*>(target.get()),
          static_cast<juce::AudioProcessor*>(adsr.get()), static_cast<juce::AudioProcessor*>(vca.get())})
        p->prepareToPlay(kSyncRate, kSyncBlock);

    const float chordHz[] = {220.0f, 277.18f, 329.63f};
    const int voices = c.poly ? 3 : 1;
    std::vector<float> out;
    for (int pos = 0; pos < kSyncSkip + kSyncSeconds; pos += kSyncBlock) {
        juce::AudioBuffer<float> m(OscillatorModule::kNumOutputs, kSyncBlock),
            t(OscillatorModule::kNumOutputs, kSyncBlock);
        juce::AudioBuffer<float> a(16, kSyncBlock), v(16, kSyncBlock);
        m.clear();
        t.clear();
        a.clear();
        v.clear();
        juce::MidiBuffer note, noteForTarget, noteForAdsr, none;
        if (pos == 0)
            note.addEvent(juce::MidiMessage::noteOn(1, c.note, 0.8f), 0);
        noteForTarget = note;
        noteForAdsr = note;
        for (int i = 0; i < voices && c.poly; ++i) {
            juce::FloatVectorOperations::fill(m.getWritePointer(i), chordHz[i], kSyncBlock);
            juce::FloatVectorOperations::fill(t.getWritePointer(i), chordHz[i], kSyncBlock);
        }
        master->processBlock(m, none);
        if (patched)
            for (int i = 0; i < voices; ++i)
                t.copyFrom(OscillatorModule::kGateBase + i, 0, m, i, 0, kSyncBlock);
        target->processBlock(t, noteForTarget);
        adsr->processBlock(a, noteForAdsr);
        for (int i = 0; i < voices; ++i)
            v.addFrom(0, 0, t, i, 0, kSyncBlock, 1.0f / (float)voices);
        v.copyFrom(1, 0, a, 0, 0, kSyncBlock);
        vca->processBlock(v, none);
        if (pos >= kSyncSkip)
            out.insert(out.end(), v.getReadPointer(0), v.getReadPointer(0) + kSyncBlock);
    }
    out.resize((size_t)kSyncSeconds);
    return out;
}

// The share of the energy that is not at a multiple of the master's pitch, in dB. A synced wave repeats exactly
// once per master cycle, so everything else is what the ear hears as grit: restarts landing unevenly, random phases
// or folded-back overtones. The window is one second, so with a whole number of Hz each harmonic falls on its own bin.
// The result is relative to `reference`, the energy of the same note left free, or to the wave's own energy if that is
// larger: a cycle that happens to sit near a slow wave's zero is quiet, and its small remainder is not louder grit.
double acEnergy(const std::vector<float>& x) {
    double total = 0.0, mean = 0.0;
    for (float s : x)
        mean += (double)s / (double)x.size();
    for (float s : x)
        total += ((double)s - mean) * ((double)s - mean);
    return total;
}

double offHarmonicDb(const std::vector<float>& x, int masterHz, double reference) {
    const double total = acEnergy(x);
    double mean = 0.0;
    for (float s : x)
        mean += (double)s / (double)x.size();
    double harmonic = 0.0;
    const double n = (double)x.size();
    for (int h = masterHz; h < (int)(kSyncRate / 2.0); h += masterHz) {
        const double w = 2.0 * std::cos(kTwoPi * (double)h / kSyncRate);
        double s1 = 0.0, s2 = 0.0;
        for (float s : x) {
            const double s0 = (double)s - mean + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        harmonic += (s1 * s1 + s2 * s2 - w * s1 * s2) * 2.0 / n;
    }
    return 10.0 *
           std::log10(std::max(1e-12, (total - std::min(total, harmonic)) / std::max({1e-12, total, reference})));
}

float largestStep(const std::vector<float>& x) {
    float worst = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        worst = std::max(worst, std::abs(x[i] - x[i - 1]));
    return worst;
}

float peakOf(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x)
        p = std::max(p, std::abs(v));
    return p;
}

} // namespace

// Regression test for FRO1224: a real Oscillator cabled into Restart crackled. Rand above 0 drew a new random phase at
// every edge (98 % or more of the energy off the master's harmonics), and at Rand 0 the 64-sample hand-over averaged a
// fast master's wave down to under half its level. A synced wave must repeat once per master cycle and keep its level.
// A click is not counted as a failure by itself: a saw flags at each of its own wraps and a synced saw at each restart.
// Those repeat with the wave, so the limit is on their size, never above the free-running wave's own wrap.
TEST(OscillatorGateRestart, RestartFromARealOscillator) {
    constexpr double kMaxOffHarmonicDb = -30.0;
    constexpr float kMinLevelOfFree = 0.6f;
    for (const bool poly : {false, true})
        for (const int unison : {1, 5}) {
            const auto free = renderSyncedNote({0, 100, unison, 0.0f, poly}, /*patched*/ false);
            float freeClick = 0.0f;
            for (const auto& click : clickdetector::findClicks(free))
                freeClick = std::max(freeClick, click.size);
            for (const float rand : {0.0f, 50.0f, 100.0f})
                for (const int wave : {0, 1, 2, 3})
                    for (const int hz : {100, 440, 2000, 301}) {
                        const SyncCase c{wave, hz, unison, rand, poly};
                        const auto x = renderSyncedNote(c);
                        const auto clicks = clickdetector::findClicks(x);
                        float largest = 0.0f;
                        for (const auto& click : clicks)
                            largest = std::max(largest, click.size);
                        const double off = offHarmonicDb(x, hz, acEnergy(free));
                        EXPECT_LE(off, kMaxOffHarmonicDb) << describeCase(c);
                        // With Rand, a short cycle of a slow wave can sit near its zero, so only Rand 0 has a floor.
                        if (rand == 0.0f)
                            EXPECT_GE(peakOf(x), kMinLevelOfFree * peakOf(free)) << describeCase(c);
                        EXPECT_LE(largest, std::max(freeClick, 0.1f) * 1.1f) << describeCase(c);
                    }
        }
}

// Regression test for FRO1224: a master whose period is not a whole number of samples must not restart unevenly.
TEST(OscillatorGateRestart, GateEdgesPlaceARealWaveBetweenSamples) {
    GateEdges edges;
    const std::vector<float> ramp = {-0.5f, -0.1f, 0.3f, 0.7f};
    edges.scan(ramp.data(), (int)ramp.size());
    ASSERT_EQ(edges.count, 1);
    EXPECT_EQ(edges.offsets[0], 2);
    EXPECT_NEAR(edges.leads[0], 0.5f, 1e-5f) << "the ramp crosses 0.1 half a sample before sample 2";
}
