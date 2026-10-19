// The Oscillator's Gate input: a rising edge restarts the voice's phase at Phase/Rand on its own sample,
// in poly (one gate channel per voice) and in mono (one channel), without any MIDI note-on.

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
    EXPECT_EQ(osc.getInputPortLabel(OscillatorModule::kJackGate), "Gate");
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
