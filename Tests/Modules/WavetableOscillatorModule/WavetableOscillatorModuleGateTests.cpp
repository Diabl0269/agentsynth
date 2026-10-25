// WavetableOscillatorModuleGateTests.cpp
// The Wavetable's Gate input: a rising edge retriggers the voice's phases on its own sample, in poly (one
// gate channel per voice) and in mono (raw channel 16), without any MIDI note-on.

#include "WavetableOscillatorModuleTestFixture.h"

namespace {

using WT = WavetableOscillatorModule;
constexpr float kPitchHz = 220.0f;

struct GateBlock {
    int riseAt = -1;
    int fallAt = -1;
};

// Renders voice 0 block by block: a held 220 Hz pitch CV in poly, the gate on `gateChannel`.
struct Rig {
    WT& module;
    bool poly;
    int gateChannel;
    bool gateHigh = false;

    std::vector<float> renderBlock(const GateBlock& gate, bool patched = true) {
        juce::AudioBuffer<float> buf(kCh, kBlockSize);
        buf.clear();
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(0), kPitchHz, kBlockSize);
        if (patched) {
            float* g = buf.getWritePointer(gateChannel);
            for (int s = 0; s < kBlockSize; ++s) {
                if (s == gate.riseAt)
                    gateHigh = true;
                if (s == gate.fallAt)
                    gateHigh = false;
                g[s] = gateHigh ? 1.0f : 0.0f;
            }
        }
        juce::MidiBuffer midi;
        module.processBlock(buf, midi);
        return {buf.getReadPointer(0), buf.getReadPointer(0) + kBlockSize};
    }
};

float maxDiff(const std::vector<float>& a, int aFrom, const std::vector<float>& b, int bFrom, int count) {
    float d = 0.0f;
    for (int i = 0; i < count; ++i)
        d = std::max(d, std::abs(a[(size_t)(aFrom + i)] - b[(size_t)(bFrom + i)]));
    return d;
}

} // namespace

class WavetableGateTest : public WavetableOscillatorModuleTest {
protected:
    void configure(bool poly) {
        setFloat(*module, "level", 1.0f);
        setFloat(*module, "position", 0.0f); // sine
        setFloat(*module, "phase", 90.0f);
        setFloat(*module, "randomPhase", 0.0f);
        setInt(*module, "unison", 1);
        setBool(*module, "poly", poly);
        module->prepareToPlay(kSampleRate, kBlockSize);
    }
};

TEST_F(WavetableGateTest, PolyEdgeRetriggersTheVoiceOnItsOwnSample) {
    configure(true);
    Rig rig{*module, true, WT::kGateBase};
    rig.renderBlock({-1, -1}); // lets the 5 ms pitch smoother settle on 220 Hz
    const auto a = rig.renderBlock({100, 200});
    const auto b = rig.renderBlock({300, -1});

    // The restarted stack starts at 90 degrees on the edge sample while the old sound fades out over 64 samples;
    // past the fade both notes run alike, and the sine is where a quarter-turn start puts it.
    const double dt = (double)kPitchHz / kSampleRate;
    const float expected = (float)std::sin(6.283185307179586 * (0.25 + 64.0 * dt)) * peakOf(b.data(), 300 + 64, 100);
    EXPECT_NEAR(a[100 + 64], expected, 2e-3f);
    EXPECT_NEAR(b[300 + 64], expected, 2e-3f);
    EXPECT_EQ(maxDiff(a, 100 + 64, b, 300 + 64, kBlockSize - 300 - 64), 0.0f);
}

TEST_F(WavetableGateTest, PolyWithoutAGateTheTwoNotesAreNotAligned) {
    configure(true);
    Rig rig{*module, true, WT::kGateBase};
    const auto a = rig.renderBlock({100, 200}, /*patched*/ false);
    const auto b = rig.renderBlock({300, -1}, /*patched*/ false);
    EXPECT_GT(maxDiff(a, 100, b, 300, kBlockSize - 300), 0.1f);
}

TEST_F(WavetableGateTest, MonoEdgeRetriggersWithoutAnyMidi) {
    configure(false);
    Rig rig{*module, false, WT::kJackGate};
    rig.renderBlock({-1, -1}); // lets the pitch smoother settle
    const auto a = rig.renderBlock({100, 200});
    const auto b = rig.renderBlock({300, -1});
    const double dt = 440.0 / kSampleRate; // no MIDI note yet: the voice rests on A4
    EXPECT_NEAR(a[100 + 64], (float)std::sin(6.283185307179586 * (0.25 + 64.0 * dt)) * peakOf(b.data(), 300 + 64, 100),
                2e-3f)
        << "a sine started at 90 degrees, once the old sound has faded out";
    EXPECT_EQ(maxDiff(a, 100 + 64, b, 300 + 64, kBlockSize - 300 - 64), 0.0f);

    WT other;
    other.prepareToPlay(kSampleRate, kBlockSize);
    setFloat(other, "position", 0.0f);
    setFloat(other, "phase", 90.0f);
    setInt(other, "unison", 1);
    other.prepareToPlay(kSampleRate, kBlockSize);
    Rig control{other, false, WT::kJackGate};
    control.renderBlock({-1, -1}, false);
    const auto ca = control.renderBlock({100, 200}, false);
    const auto cb = control.renderBlock({300, -1}, false);
    EXPECT_GT(maxDiff(ca, 100, cb, 300, kBlockSize - 300), 0.1f);
}

TEST_F(WavetableGateTest, AnUnpatchedGateLeavesTheOutputUnchanged) {
    for (const bool poly : {false, true}) {
        WT zeros, shorter;
        for (WT* m : {&zeros, &shorter}) {
            setFloat(*m, "position", 0.0f);
            setBool(*m, "poly", poly);
            m->prepareToPlay(kSampleRate, kBlockSize);
        }
        for (int block = 0; block < 3; ++block) {
            juce::AudioBuffer<float> x(kCh, kBlockSize), y(WT::kGateBase, kBlockSize);
            x.clear();
            y.clear();
            if (poly) {
                juce::FloatVectorOperations::fill(x.getWritePointer(0), kPitchHz, kBlockSize);
                juce::FloatVectorOperations::fill(y.getWritePointer(0), kPitchHz, kBlockSize);
            }
            juce::MidiBuffer midi;
            zeros.processBlock(x, midi);
            shorter.processBlock(y, midi);
            for (int s = 0; s < kBlockSize; ++s)
                ASSERT_EQ(x.getSample(0, s), y.getSample(0, s)) << (poly ? "poly" : "mono") << " " << block << "/" << s;
        }
    }
}

TEST_F(WavetableGateTest, GateChannelsAreMappedInBothModesAndAudioRStaysPut) {
    const auto mono = module->mapInputChannel(WT::kJackGate);
    EXPECT_EQ(mono.visibleJackIndex, WT::kJackGate);
    EXPECT_EQ(mono.role, PortRole::Gate);
    EXPECT_EQ(mono.polyVoiceSpan, 1);
    EXPECT_EQ(module->getInputPortLabel(WT::kJackGate), "Restart");
    EXPECT_EQ(module->getVisibleInputPortCount(), WT::kNumJacks);
    EXPECT_EQ(WT::kNumModCV, 15);
    EXPECT_EQ(WT::kNumInputs, 31);
    EXPECT_EQ(WT::kRightBase, 23) << "saved Audio R routings key off raw output channel 23";
    EXPECT_EQ(WT::kNumOutputs, 31);
    EXPECT_EQ(module->mapOutputChannel(WT::kRightBase).visibleJackIndex, 1);

    for (const auto& t : module->getModulationTargets())
        EXPECT_NE(t.name, "Gate");

    setBool(*module, "poly", true);
    for (int raw = WT::kPolyModCVBase; raw < WT::kGateBase; ++raw)
        EXPECT_EQ(module->mapInputChannel(raw).role, PortRole::ModCV) << raw;
    for (int v = 0; v < WT::kNumVoices; ++v) {
        const auto p = module->mapInputChannel(WT::kGateBase + v);
        EXPECT_EQ(p.visibleJackIndex, WT::kJackGate) << v;
        EXPECT_EQ(p.role, PortRole::Gate) << v;
        EXPECT_EQ(p.isPolyGroupHead, v == 0) << v;
        EXPECT_EQ(p.polyVoiceSpan, v == 0 ? WT::kNumVoices : 1) << v;
    }
    for (const auto& t : module->getModulationTargets())
        EXPECT_NE(t.name, "Gate");
}
