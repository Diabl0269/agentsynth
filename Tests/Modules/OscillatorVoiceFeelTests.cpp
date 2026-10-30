// Oscillator voice feel: Phase and Rand restart every unison voice on a note, and Blend and Width
// shape the unison stack. A new Oscillator starts at Phase 0, Rand 0, Detune 35 cents, Blend 75 %, Width 100 %.

#include "../Engine/AudioRendering/ClickDetector.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/Oscillator/HeldNotes.h"
#include "Modules/OscillatorModule.h"
#include "Modules/UnisonVoicing.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kFade = 64; // OscillatorModule::CROSSFADE_SAMPLES
constexpr double kTwoPi = 6.283185307179586;

juce::RangedAudioParameter* param(juce::AudioProcessor& m, const char* id) {
    return dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
}

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = param(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

float actualOf(juce::AudioProcessor& m, const char* id) {
    auto* p = param(m, id);
    return p != nullptr ? p->convertFrom0to1(p->getValue()) : -12345.0f;
}

struct NoteEvent {
    int block;
    int offset;
    int note;
    bool on;
};

// Renders whole blocks; Audio L and Audio R of voice 0 are collected. Mono mode takes its pitch
// from MIDI; poly mode reads voice `polyVoice`'s pitch from `pitchHz` (0 = silent).
struct Rig {
    OscillatorModule osc;
    std::vector<float> left;
    std::vector<float> right;
    float pitchHz = 440.0f;
    int polyVoice = 0;
    bool poly = false;

    explicit Rig(bool polyMode = false)
        : poly(polyMode) {
        osc.prepareToPlay(kSampleRate, kBlock);
        if (polyMode)
            setActual(osc, "poly", 1.0f);
    }

    // Blend and Width are smoothed; restart the smoothers at the knob values so a test starts settled.
    void settle() { osc.prepareToPlay(kSampleRate, kBlock); }

    void renderBlock(int blockIndex, const std::vector<NoteEvent>& events, float hz) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(polyVoice), hz, kBlock);
        juce::MidiBuffer midi;
        for (const auto& e : events) {
            if (e.block != blockIndex)
                continue;
            midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)100)
                               : juce::MidiMessage::noteOff(1, e.note),
                          e.offset);
        }
        osc.processBlock(buf, midi);
        const int rightCh = OscillatorModule::kRightBase + (poly ? polyVoice : 0);
        const int leftCh = poly ? polyVoice : 0;
        left.insert(left.end(), buf.getReadPointer(leftCh), buf.getReadPointer(leftCh) + kBlock);
        right.insert(right.end(), buf.getReadPointer(rightCh), buf.getReadPointer(rightCh) + kBlock);
    }

    void render(int numBlocks, const std::vector<NoteEvent>& events = {}) {
        for (int b = 0; b < numBlocks; ++b)
            renderBlock(b, events, pitchHz);
    }
};

double dtFor(double hz) { return hz / kSampleRate; }

double energy(const std::vector<float>& x, size_t from, size_t to) {
    double e = 0.0;
    for (size_t i = from; i < to; ++i)
        e += (double)x[i] * x[i];
    return e;
}

} // namespace

// ---- Parameters -----------------------------------------------------------------------------------

TEST(OscillatorVoiceFeel, NewParametersExistWithTheirDefaults) {
    OscillatorModule osc;
    EXPECT_NEAR(actualOf(osc, "phase"), 0.0f, 1e-4f);
    EXPECT_NEAR(actualOf(osc, "randomPhase"), 0.0f, 1e-4f);
    EXPECT_NEAR(actualOf(osc, "blend"), 0.75f, 1e-4f) << "a new Oscillator starts at Blend 75 %";
    EXPECT_NEAR(actualOf(osc, "width"), 1.0f, 1e-4f);
    EXPECT_NEAR(param(osc, "phase")->convertFrom0to1(1.0f), 360.0f, 1e-3f);
    EXPECT_NEAR(param(osc, "randomPhase")->convertFrom0to1(1.0f), 100.0f, 1e-3f);
}

TEST(OscillatorVoiceFeel, DetuneKnobIsSkewedButStaysInCents) {
    OscillatorModule osc;
    WavetableOscillatorModule wt;
    for (juce::AudioProcessor* m :
         {static_cast<juce::AudioProcessor*>(&osc), static_cast<juce::AudioProcessor*>(&wt)}) {
        auto* detune = param(*m, "detune");
        ASSERT_NE(detune, nullptr);
        EXPECT_NEAR(detune->convertFrom0to1(0.5f), 25.0f, 0.01f) << "the knob's middle is a musical 25 cents";
        EXPECT_NEAR(detune->convertFrom0to1(1.0f), 100.0f, 1e-3f);
        setActual(*m, "detune", 37.0f);
        EXPECT_NEAR(actualOf(*m, "detune"), 37.0f, 0.01f);
        EXPECT_EQ(detune->getText(detune->getValue(), 32), "37.0 ct") << "reads as cents, not a raw float";
        EXPECT_NEAR(detune->convertFrom0to1(detune->getValueForText("12.5 ct")), 12.5f, 0.01f);
    }
}

TEST(OscillatorVoiceFeel, NewParametersSurviveSaveAndLoad) {
    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    auto& module = *node->getProcessor();
    setActual(module, "phase", 123.0f);
    setActual(module, "randomPhase", 40.0f);
    setActual(module, "blend", 0.25f);
    setActual(module, "width", 0.75f);
    setActual(module, "detune", 37.0f);

    const juce::var json = synth::AIStateMapper::graphToJSON(source);
    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(restored.getNumNodes(), 1);
    auto& back = *restored.getNodes().getFirst()->getProcessor();
    EXPECT_NEAR(actualOf(back, "phase"), 123.0f, 0.05f);
    EXPECT_NEAR(actualOf(back, "randomPhase"), 40.0f, 0.05f);
    EXPECT_NEAR(actualOf(back, "blend"), 0.25f, 1e-4f);
    EXPECT_NEAR(actualOf(back, "width"), 0.75f, 1e-4f);
    EXPECT_NEAR(actualOf(back, "detune"), 37.0f, 0.05f) << "saved cents load as the same cents";

    juce::MemoryBlock state;
    module.getStateInformation(state);
    OscillatorModule viaBinary;
    viaBinary.setStateInformation(state.getData(), (int)state.getSize());
    EXPECT_NEAR(actualOf(viaBinary, "phase"), 123.0f, 0.05f);
    EXPECT_NEAR(actualOf(viaBinary, "width"), 0.75f, 1e-4f);
    EXPECT_NEAR(actualOf(viaBinary, "detune"), 37.0f, 0.05f);
}

// Regression test for FRO1077: the default Blend moved from 100 % (then to 25 %, now 75 %), which must not touch a
// project (or a preset or generated patch) that never recorded a Blend.
TEST(OscillatorVoiceFeel, ANodeStoredWithoutBlendKeepsBlendAtOneHundredPercent) {
    juce::AudioProcessorGraph source;
    source.addNode(std::make_unique<OscillatorModule>());
    const juce::var json = synth::AIStateMapper::graphToJSON(source);
    auto* params = json["nodes"][0]["params"].getDynamicObject();
    ASSERT_NE(params, nullptr);
    ASSERT_TRUE(params->hasProperty("blend"));

    juce::AudioProcessorGraph kept;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, kept, true, /*trusted=*/true));
    EXPECT_NEAR(actualOf(*kept.getNodes().getFirst()->getProcessor(), "blend"), 0.75f, 1e-4f)
        << "a stored value is loaded as it is";

    params->removeProperty("blend");
    juce::AudioProcessorGraph legacy;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, legacy, true, /*trusted=*/true));
    EXPECT_NEAR(actualOf(*legacy.getNodes().getFirst()->getProcessor(), "blend"), 1.0f, 1e-4f);

    OscillatorModule old;
    juce::MemoryBlock state;
    old.getStateInformation(state);
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int)state.getSize());
    ASSERT_NE(xml, nullptr);
    xml->removeAttribute("blend");
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*xml, legacyState);
    OscillatorModule viaBinary;
    viaBinary.setStateInformation(legacyState.getData(), (int)legacyState.getSize());
    EXPECT_NEAR(actualOf(viaBinary, "blend"), 1.0f, 1e-4f) << "binary state without a Blend key";
}

// ---- HeldNotes ------------------------------------------------------------------------------------

TEST(HeldNotes, PushRemoveAndOverlap) {
    synth::HeldNotes held;
    EXPECT_TRUE(held.empty());
    EXPECT_FALSE(held.anyOtherThan(60));
    held.press(60);
    EXPECT_FALSE(held.anyOtherThan(60)) << "the note itself is not an overlap";
    held.press(64);
    EXPECT_TRUE(held.anyOtherThan(64));
    EXPECT_EQ(held.newest(), 64);
    held.release(64);
    EXPECT_EQ(held.newest(), 60);
    held.release(99); // not held: ignored
    EXPECT_EQ(held.size(), 1);
    held.press(60); // pressing a held note again keeps one entry
    EXPECT_EQ(held.size(), 1);
    held.release(60);
    EXPECT_TRUE(held.empty());
}

TEST(HeldNotes, FullStackDropsTheOldest) {
    synth::HeldNotes held;
    for (int n = 0; n < synth::HeldNotes::kCapacity + 3; ++n)
        held.press(n);
    EXPECT_EQ(held.size(), synth::HeldNotes::kCapacity);
    EXPECT_EQ(held.newest(), synth::HeldNotes::kCapacity + 2);
}

// ---- Shared unison helpers ------------------------------------------------------------------------

TEST(UnisonVoicing, CentreVoicesAreTheOnesNearestTheRoot) {
    for (int count = 1; count <= 8; ++count) {
        int centres = 0;
        for (int u = 0; u < count; ++u)
            centres += synth::isUnisonCentreVoice(u, count) ? 1 : 0;
        EXPECT_EQ(centres, (count % 2 == 1) ? 1 : 2) << count;
    }
    EXPECT_TRUE(synth::isUnisonCentreVoice(1, 3));
    EXPECT_TRUE(synth::isUnisonCentreVoice(1, 4));
    EXPECT_TRUE(synth::isUnisonCentreVoice(2, 4));
    EXPECT_FALSE(synth::isUnisonCentreVoice(0, 4));
}

// ---- Mono restart ---------------------------------------------------------------------------------

TEST(OscillatorVoiceFeel, TwoIdenticalNotesStartIdenticallyWithoutRand) {
    Rig rig;
    setActual(rig.osc, "unison", 3.0f);
    setActual(rig.osc, "detune", 12.0f);
    const std::vector<NoteEvent> notes = {{0, 0, 69, true}, {3, 100, 69, false}, {8, 0, 69, true}};
    rig.render(12, notes);

    // The first kFade samples of a restart blend out the old phases, so compare after them.
    const size_t a = (size_t)kFade;
    const size_t b = (size_t)(8 * kBlock + kFade);
    for (size_t i = 0; i < 400; ++i)
        ASSERT_EQ(rig.left[a + i], rig.left[b + i]) << "sample " << i;
}

TEST(OscillatorVoiceFeel, RandMakesEachNoteStartDifferently) {
    Rig rig;
    setActual(rig.osc, "unison", 3.0f);
    setActual(rig.osc, "detune", 12.0f);
    setActual(rig.osc, "randomPhase", 60.0f);
    const std::vector<NoteEvent> notes = {{0, 0, 69, true}, {3, 100, 69, false}, {8, 0, 69, true}};
    rig.render(12, notes);

    double diff = 0.0;
    for (size_t i = 0; i < 400; ++i)
        diff = std::max(diff, (double)std::abs(rig.left[kFade + i] - rig.left[8 * kBlock + kFade + i]));
    EXPECT_GT(diff, 0.05);
}

TEST(OscillatorVoiceFeel, RandRendersRepeatExactly) {
    std::vector<float> first;
    for (int pass = 0; pass < 2; ++pass) {
        Rig rig;
        setActual(rig.osc, "unison", 4.0f);
        setActual(rig.osc, "randomPhase", 100.0f);
        rig.render(3, {{0, 0, 69, true}, {1, 200, 69, true}});
        if (pass == 0)
            first = rig.left;
        else
            EXPECT_EQ(first, rig.left) << "the seeded generator makes a render reproducible";
    }
}

TEST(OscillatorVoiceFeel, PhaseNinetyStartsASineAtItsPeak) {
    Rig rig;
    setActual(rig.osc, "phase", 90.0f);
    rig.render(2, {{0, 0, 69, true}});

    const double dt = dtFor(440.0);
    for (size_t i = (size_t)kFade; i < 600; ++i)
        ASSERT_NEAR(rig.left[i], std::sin(kTwoPi * (0.25 + (double)i * dt)), 2e-3) << "sample " << i;
    // The restart lands at the note-on's own sample: a later offset leaves the earlier samples free-running.
    Rig late;
    setActual(late.osc, "phase", 90.0f);
    late.render(2, {{0, 300, 69, true}});
    for (size_t i = 0; i < 300; ++i)
        ASSERT_NEAR(late.left[i], std::sin(kTwoPi * (double)i * dt), 2e-3) << "sample " << i;
    for (size_t i = 300 + (size_t)kFade; i < 700; ++i)
        ASSERT_NEAR(late.left[i], std::sin(kTwoPi * (0.25 + (double)(i - 300) * dt)), 2e-3) << "sample " << i;
}

TEST(OscillatorVoiceFeel, RestartMidNoteHasNoStep) {
    // Find where the free-running sine peaks, then restart there at the trough phase.
    Rig baseline;
    baseline.render(4);
    size_t peak = 1100;
    for (size_t i = 1100; i < 1400; ++i)
        if (baseline.left[i] > baseline.left[peak])
            peak = i;
    ASSERT_GT(baseline.left[peak], 0.99f);

    Rig rig;
    setActual(rig.osc, "phase", 270.0f); // sin = -1: a naive restart jumps from +1 to -1
    const int block = (int)(peak / kBlock);
    rig.render(4, {{block, (int)(peak % kBlock), 69, true}});

    float worst = 0.0f;
    for (size_t i = peak - 20; i < peak + 200; ++i)
        worst = std::max(worst, std::abs(rig.left[i] - rig.left[i - 1]));
    EXPECT_LT(worst, 0.2f) << "the old phases fade out instead of stepping";
    EXPECT_NEAR(rig.left[peak + 150], std::sin(kTwoPi * (0.75 + 150.0 * dtFor(440.0))), 2e-3)
        << "and the new phase has taken over";
}

TEST(OscillatorVoiceFeel, NothingRestartsWithoutANoteOn) {
    Rig rig;
    setActual(rig.osc, "phase", 90.0f);
    setActual(rig.osc, "randomPhase", 100.0f);
    rig.render(6, {{3, 10, 69, true}, {4, 10, 69, false}}); // one note, then its release

    // Before the note the oscillator free-runs from phase 0; neither knob touches it.
    const double dt = dtFor(440.0);
    for (size_t i = 0; i < (size_t)(3 * kBlock + 10); ++i)
        ASSERT_NEAR(rig.left[i], std::sin(kTwoPi * (double)i * dt), 2e-3) << "sample " << i;
    // After the note-on at block 3 offset 10 the phase runs on from the restart, release included.
    // Rand 100 makes the start random, so only continuity is checked: no step across the release.
    float worst = 0.0f;
    const size_t release = (size_t)(4 * kBlock + 10);
    for (size_t i = release - 5; i < release + 5; ++i)
        worst = std::max(worst, std::abs(rig.left[i] - rig.left[i - 1]));
    EXPECT_LT(worst, 0.1f);
}

TEST(OscillatorVoiceFeel, ANoteOffWhileMutedIsNotLeftHeld) {
    // Tracking continues while muted, so a later single note-on is not seen as overlapping.
    // Both behave the same today; this pins that muted blocks do not throw off the render.
    Rig rig;
    setActual(rig.osc, "muted", 1.0f);
    rig.render(1, {{0, 0, 69, true}});
    setActual(rig.osc, "muted", 0.0f);
    rig.renderBlock(1, {{1, 0, 69, false}}, 440.0f);
    rig.renderBlock(2, {{2, 0, 69, true}}, 440.0f);
    EXPECT_GT(energy(rig.left, 2 * kBlock, 3 * kBlock), 1.0);
}

// ---- Poly restart ---------------------------------------------------------------------------------

TEST(OscillatorVoiceFeel, PolyVoiceRestartsWhenItGoesFromSilentToSounding) {
    Rig rig(/*poly*/ true);
    rig.polyVoice = 1;
    setActual(rig.osc, "phase", 90.0f);
    const std::vector<NoteEvent> none;
    rig.renderBlock(0, none, 0.0f); // silent
    rig.renderBlock(1, none, 440.0f);
    rig.renderBlock(2, none, 440.0f);
    rig.renderBlock(3, none, 0.0f); // silent again
    rig.renderBlock(4, none, 440.0f);

    const double dt = dtFor(440.0);
    for (size_t i = 0; i < 300; ++i) {
        const double expected = std::sin(kTwoPi * (0.25 + (double)i * dt));
        ASSERT_NEAR(rig.left[(size_t)kBlock + i], expected, 2e-3) << "first note, sample " << i;
        ASSERT_NEAR(rig.left[(size_t)(4 * kBlock) + i], expected, 2e-3) << "second note, sample " << i;
    }
    // A voice that keeps sounding is not restarted: the phase runs on across the block boundary.
    const size_t seam = (size_t)(2 * kBlock);
    EXPECT_NEAR(rig.left[seam], std::sin(kTwoPi * (0.25 + (double)kBlock * dt)), 2e-3);
}

// ---- Blend and Width ------------------------------------------------------------------------------

namespace {

// The constant-power sum of the sines of the unison voices, started at phase 0.
std::vector<double> expectedStack(int count, double cents, double blend, bool useBlend, size_t numSamples) {
    std::vector<double> out(numSamples, 0.0);
    double sumPower = 0.0;
    for (int u = 0; u < count; ++u) {
        const double gain = useBlend ? synth::unisonBlendGain(u, count, (float)blend) : 1.0;
        sumPower += gain * gain;
        const double ratio = std::pow(2.0, cents * synth::unisonOffsetFraction(u, count) / 1200.0);
        for (size_t i = 0; i < numSamples; ++i)
            out[i] += gain * std::sin(kTwoPi * (double)i * dtFor(440.0) * ratio);
    }
    for (auto& v : out)
        v /= std::sqrt(sumPower);
    return out;
}

} // namespace

TEST(OscillatorVoiceFeel, BlendOneWidthZeroIsTheConstantPowerSum) {
    Rig rig;
    setActual(rig.osc, "unison", 4.0f);
    setActual(rig.osc, "detune", 30.0f);
    setActual(rig.osc, "blend", 1.0f);
    setActual(rig.osc, "width", 0.0f);
    rig.settle();
    rig.render(4, {{0, 0, 69, true}});
    const auto expected = expectedStack(4, 30.0, 1.0, /*useBlend*/ true, rig.left.size());
    for (size_t i = 0; i < rig.left.size(); ++i)
        ASSERT_NEAR(rig.left[i], expected[i], 1e-3) << "sample " << i;
    EXPECT_EQ(rig.left, rig.right) << "Audio R stays a bit-identical copy at Width 0";
}

TEST(OscillatorVoiceFeel, BlendZeroLeavesOnlyTheCentreVoices) {
    for (const int count : {3, 4}) {
        Rig rig;
        setActual(rig.osc, "unison", (float)count);
        setActual(rig.osc, "detune", 50.0f);
        setActual(rig.osc, "blend", 0.0f);
        setActual(rig.osc, "width", 0.0f);
        rig.settle();
        rig.render(4, {{0, 0, 69, true}});
        const auto expected = expectedStack(count, 50.0, 0.0, /*useBlend*/ true, rig.left.size());
        for (size_t i = 0; i < rig.left.size(); ++i)
            ASSERT_NEAR(rig.left[i], expected[i], 1e-3) << count << " voices, sample " << i;
    }
}

TEST(OscillatorVoiceFeel, WidthSpreadsTheStackAcrossBothLegsWithoutAddingGain) {
    auto run = [](float width, bool poly) {
        Rig rig(poly);
        setActual(rig.osc, "unison", 4.0f);
        setActual(rig.osc, "detune", 30.0f);
        setActual(rig.osc, "width", width);
        rig.settle();
        if (poly)
            rig.render(4);
        else
            rig.render(4, {{0, 0, 69, true}});
        return std::make_pair(rig.left, rig.right);
    };

    for (const bool poly : {false, true}) {
        const auto narrow = run(0.0f, poly);
        const auto wide = run(1.0f, poly);
        EXPECT_EQ(narrow.first, narrow.second) << "Width 0: both legs identical";

        double maxDiff = 0.0;
        for (size_t i = 0; i < wide.first.size(); ++i)
            maxDiff = std::max(maxDiff, (double)std::abs(wide.first[i] - wide.second[i]));
        EXPECT_GT(maxDiff, 0.05) << "Width 1: the legs differ";

        const size_t n = wide.first.size();
        const double mono = energy(narrow.first, 0, n) + energy(narrow.second, 0, n);
        const double spread = energy(wide.first, 0, n) + energy(wide.second, 0, n);
        EXPECT_LE(spread, mono * 1.001) << "spreading the voices never adds level";
        EXPECT_GT(spread, mono * 0.2) << "and does not lose the signal";
        // Four in-phase voices of a constant-power stack peak at sqrt(4); downstream stages own that headroom.
        for (size_t i = 0; i < n; ++i) {
            ASSERT_LE(std::abs(wide.first[i]), 2.05f);
            ASSERT_LE(std::abs(wide.second[i]), 2.05f);
        }
    }
}

TEST(OscillatorVoiceFeel, PanScalesTheWiderStackOnTop) {
    Rig rig;
    setActual(rig.osc, "unison", 4.0f);
    setActual(rig.osc, "detune", 30.0f);
    setActual(rig.osc, "width", 1.0f);
    setActual(rig.osc, "pan", 1.0f); // hard right
    rig.settle();
    rig.render(4, {{0, 0, 69, true}});
    EXPECT_LT(energy(rig.left, (size_t)kBlock, rig.left.size()), 1e-9);
    EXPECT_GT(energy(rig.right, (size_t)kBlock, rig.right.size()), 1.0);
}

TEST(OscillatorVoiceFeel, AnAutomatedBlendAndWidthGlideInsteadOfStepping) {
    Rig rig;
    setActual(rig.osc, "unison", 8.0f);
    setActual(rig.osc, "detune", 40.0f);
    rig.render(2, {{0, 0, 69, true}});
    setActual(rig.osc, "blend", 0.0f);
    setActual(rig.osc, "width", 1.0f);
    rig.renderBlock(2, {}, 440.0f);
    rig.renderBlock(3, {}, 440.0f);
    float worst = 0.0f;
    for (size_t i = (size_t)(2 * kBlock) - 1; i + 1 < rig.left.size(); ++i)
        worst = std::max({worst, std::abs(rig.left[i + 1] - rig.left[i]), std::abs(rig.right[i + 1] - rig.right[i])});
    EXPECT_LT(worst, 0.2f);
}

// A constant-power stack can peak at sqrt(N) when its voices start in phase (the default Rand 0 does exactly
// that); the headroom for it is handled downstream, so the source only has to stay within sqrt(N). Its loudness
// is checked with decorrelated start phases, where uncorrelated voices must keep the level of one.
TEST(OscillatorVoiceFeel, SourcePeakStaysUnderRootNAcrossBlendAndWidth) {
    constexpr int kVoices = 8;
    auto render = [](int unison, float blend, float width, float rand, std::vector<float>& left,
                     std::vector<float>& right) {
        Rig rig;
        setActual(rig.osc, "unison", (float)unison);
        setActual(rig.osc, "detune", 60.0f);
        setActual(rig.osc, "waveform", 2.0f); // Saw: the hottest of the four
        setActual(rig.osc, "blend", blend);
        setActual(rig.osc, "width", width);
        setActual(rig.osc, "randomPhase", rand);
        rig.settle();
        rig.render(40, {{0, 0, 60, true}});
        left = rig.left;
        right = rig.right;
    };
    auto rmsDb = [](const std::vector<float>& x) {
        double e = 0.0;
        for (size_t i = (size_t)kBlock; i < x.size(); ++i)
            e += (double)x[i] * x[i];
        return 10.0 * std::log10(e / (double)(x.size() - (size_t)kBlock));
    };
    std::vector<float> left, right;
    render(1, 1.0f, 0.0f, 0.0f, left, right);
    const double singleDb = rmsDb(left);
    for (const float blend : {0.5f, 1.0f}) {
        for (const float width : {0.0f, 1.0f}) {
            render(kVoices, blend, width, 0.0f, left, right);
            float peak = 0.0f;
            for (size_t i = (size_t)kBlock; i < left.size(); ++i)
                peak = std::max({peak, std::abs(left[i]), std::abs(right[i])});
            EXPECT_LE(peak, std::sqrt((float)kVoices) * 1.05f) << "blend " << blend << " width " << width;

            if (width == 0.0f) {
                render(kVoices, blend, width, 100.0f, left, right);
                EXPECT_NEAR(rmsDb(left), singleDb, 1.5) << "blend " << blend;
            }
        }
    }
    // Blend 0 keeps only the two centre voices, each at full level, so it peaks at sqrt(2) in phase.
    render(kVoices, 0.0f, 0.0f, 0.0f, left, right);
    float peak = 0.0f;
    for (size_t i = (size_t)kBlock; i < left.size(); ++i)
        peak = std::max(peak, std::abs(left[i]));
    EXPECT_LE(peak, std::sqrt(2.0f) * 1.05f);
}

namespace {

// Sixteenth notes at 120 bpm, touching, each ending as the next starts, rendered block by block with the
// note-ons landing mid-block. Returns Audio L and Audio R and the start sample of every note.
struct Seams {
    std::vector<float> left, right;
    std::vector<int> starts;
};

Seams renderTouchingNotes(OscillatorModule& osc, const std::vector<int>& pitches) {
    osc.prepareToPlay(kSampleRate, kBlock);
    const int step = (int)std::lround(0.125 * kSampleRate);
    const int total = step * (int)pitches.size();
    Seams out;
    for (size_t i = 0; i < pitches.size(); ++i)
        out.starts.push_back((int)i * step);
    for (int pos = 0; pos < total; pos += kBlock) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        for (size_t i = 0; i < pitches.size(); ++i) {
            const int on = out.starts[i];
            const int off = on + step;
            if (off >= pos && off < pos + kBlock && i + 1 <= pitches.size())
                midi.addEvent(juce::MidiMessage::noteOff(1, pitches[i], 0.0f), off - pos);
            if (on >= pos && on < pos + kBlock)
                midi.addEvent(juce::MidiMessage::noteOn(1, pitches[i], 0.8f), on - pos);
        }
        osc.processBlock(buf, midi);
        out.left.insert(out.left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        out.right.insert(out.right.end(), buf.getReadPointer(OscillatorModule::kRightBase),
                         buf.getReadPointer(OscillatorModule::kRightBase) + kBlock);
    }
    return out;
}

const std::vector<int> kSeamPitches = {68, 52, 58, 51, 53, 48, 66, 69, 37, 46, 65, 41, 71, 53, 69, 39};

} // namespace

// Regression test for FRO1071: a note restarting the wave ticked at the start and at the end of its 64-sample
// hand-over, because the old and new waves were blended along a straight ramp that bends where it starts and
// ends. A Sine has no corners of its own, so any second-difference spike in the source is the seam.
TEST(OscillatorVoiceFeel, SineNoteRestartsLeaveNoClickWithAndWithoutRand) {
    for (const int unison : {1, 5}) {
        for (const float rand : {0.0f, 100.0f}) {
            OscillatorModule osc;
            setActual(osc, "unison", (float)unison);
            setActual(osc, "randomPhase", rand);
            if (unison > 1) {
                setActual(osc, "width", 1.0f);
                setActual(osc, "blend", 0.82f);
                setActual(osc, "detune", 59.0f);
            }
            const auto seams = renderTouchingNotes(osc, kSeamPitches);
            const auto left = clickdetector::findClicks(seams.left);
            const auto right = clickdetector::findClicks(seams.right);
            EXPECT_TRUE(left.empty()) << "unison " << unison << " rand " << rand << ": " << left.size()
                                      << " clicks in Audio L, first at sample " << (left.empty() ? 0 : left[0].index);
            EXPECT_TRUE(right.empty()) << "unison " << unison << " rand " << rand << ": " << right.size()
                                       << " clicks in Audio R";
        }
    }
}

// A Saw jumps once a cycle by design, which hides a seam from the spike detector, so this looks only at the two
// places a hand-over bends, its first and its last sample, where the wave is a straight ramp and the second
// difference must be nil. Seams with a wrap of either wave within a few samples are left out.
TEST(OscillatorVoiceFeel, SawNoteRestartsBendNoMoreThanTheirHandOverAllows) {
    const std::vector<int> low = {36, 48, 43, 40, 47, 38, 45, 41, 36, 44, 39, 46};
    for (const float rand : {0.0f, 100.0f}) {
        OscillatorModule osc;
        setActual(osc, "waveform", 2.0f);
        setActual(osc, "randomPhase", rand);
        const auto seams = renderTouchingNotes(osc, low);
        const auto& x = seams.left;
        int measured = 0;
        float worst = 0.0f;
        for (const int start : seams.starts) {
            if (start == 0)
                continue; // the first note comes from silence, with no earlier wave to hand over from
            for (const int at : {start, start + kFade}) {
                if (at < 8 || at + 8 >= (int)x.size())
                    continue;
                bool wrap = false;
                for (int k = at - 6; k <= at + 6; ++k)
                    wrap = wrap || std::abs(x[(size_t)k + 1] - x[(size_t)k]) > 0.25f;
                if (wrap)
                    continue;
                ++measured;
                worst = std::max(worst, std::abs(x[(size_t)at - 1] - 2.0f * x[(size_t)at] + x[(size_t)at + 1]));
            }
        }
        EXPECT_GE(measured, 8) << "rand " << rand << ": too few wrap-free seams to measure";
        EXPECT_LT(worst, 0.002f) << "rand " << rand << ": the hand-over bends by " << worst;
    }
}

// ---- Phase and Rand are audible at a note's start -------------------------------------------------------

namespace {

constexpr int kRepeatStep = 17640; // 0.4 s at 44.1 kHz
constexpr int kRepeatNotes = 8;
constexpr int kAttackSamples = 44; // a 1 ms attack, applied to the raw render like the default ADSR

struct Repeats {
    std::vector<std::vector<float>> notes; ///< the first 100 ms of each repeat, through a 1 ms attack
    double rms(size_t note, size_t from, size_t to) const {
        double e = 0.0;
        for (size_t i = from; i < to; ++i)
            e += (double)notes[note][i] * notes[note][i];
        return std::sqrt(e / (double)(to - from));
    }
    double meanRms(size_t from, size_t to) const {
        double sum = 0.0;
        for (size_t n = 0; n < notes.size(); ++n)
            sum += rms(n, from, to);
        return sum / (double)notes.size();
    }
    // Relative standard deviation of the 100 ms level over the repeats.
    double levelSpread() const {
        std::vector<double> levels;
        for (size_t n = 0; n < notes.size(); ++n)
            levels.push_back(rms(n, 0, notes[n].size()));
        double mean = 0.0, var = 0.0;
        for (const double l : levels)
            mean += l / (double)levels.size();
        for (const double l : levels)
            var += (l - mean) * (l - mean) / (double)levels.size();
        return std::sqrt(var) / mean;
    }
    // Largest difference between the first two repeats' first 30 ms, against the first one's peak.
    double repeatDifference() const {
        double diff = 0.0, peak = 1e-9;
        for (size_t i = 0; i < 1323; ++i) {
            diff = std::max(diff, (double)std::abs(notes[0][i] - notes[1][i]));
            peak = std::max(peak, (double)std::abs(notes[0][i]));
        }
        return diff / peak;
    }
};

Repeats renderRepeats(int unison, float phase, float rand, int waveform) {
    Rig rig;
    setActual(rig.osc, "waveform", (float)waveform);
    setActual(rig.osc, "unison", (float)unison);
    setActual(rig.osc, "detune", 25.0f);
    setActual(rig.osc, "blend", 0.75f);
    setActual(rig.osc, "phase", phase);
    setActual(rig.osc, "randomPhase", rand);
    rig.settle();
    std::vector<NoteEvent> events;
    for (int n = 0; n < kRepeatNotes; ++n) {
        const int on = 2048 + n * kRepeatStep;
        events.push_back({on / kBlock, on % kBlock, 57, true});
        events.push_back({(on + 6600) / kBlock, (on + 6600) % kBlock, 57, false});
    }
    rig.render((2048 + kRepeatNotes * kRepeatStep) / kBlock + 2, events);
    Repeats r;
    for (int n = 0; n < kRepeatNotes; ++n) {
        const size_t on = (size_t)(2048 + n * kRepeatStep);
        std::vector<float> note(rig.left.begin() + (long)on, rig.left.begin() + (long)on + 4410);
        for (int i = 0; i < kAttackSamples; ++i)
            note[(size_t)i] *= (float)i / kAttackSamples;
        r.notes.push_back(std::move(note));
    }
    return r;
}

// Root-mean-square difference of two renders' notes between samples [from, to), against the first one's level.
double relativeDifference(const Repeats& a, const Repeats& b, size_t from, size_t to) {
    double diff = 0.0, level = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double d = (double)a.notes[0][i] - b.notes[0][i];
        diff += d * d;
        level += (double)a.notes[0][i] * a.notes[0][i];
    }
    return std::sqrt(diff / level);
}

} // namespace

// The reference synth, measured as a black box (a saw at 220 Hz, 1 ms attack, five voices, Blend 75 %, Width 0): the
// shape of the first 30 ms differs from Phase 0 by 1.5 to 1.8 times its own level at Phase 90, 180 and 270, so
// Phase is plainly heard in the attack of one voice and of a stack.
TEST(OscillatorPhaseRand, PhaseChangesTheAttackWaveformOfOneVoiceAndOfAStack) {
    for (const int unison : {1, 5}) {
        const auto zero = renderRepeats(unison, 0.0f, 0.0f, 2);
        for (const float phase : {90.0f, 180.0f, 270.0f}) {
            const auto moved = renderRepeats(unison, phase, 0.0f, 2);
            EXPECT_GT(relativeDifference(zero, moved, kFade, 240), 1.0) << "unison " << unison << " phase " << phase;
            EXPECT_GT(relativeDifference(zero, moved, 240, 1323), 1.0) << "unison " << unison << " phase " << phase;
        }
    }
}

// Rand 0 starts every voice, on every note, at Phase: the stack is in phase, so the first 30 ms are louder than
// the settled sound (2.0 times in the reference synth) and every repeat of the note is the same. Rand 100 starts the
// voices apart: no bump in level (1.0 in the reference synth) and each note differs from the last, by about 15 %
// in level over 100 ms.
TEST(OscillatorPhaseRand, RandSpreadsTheStackPerVoiceAndPerNote) {
    const auto together = renderRepeats(5, 0.0f, 0.0f, 2);
    EXPECT_GT(together.meanRms(kFade, 1323) / together.meanRms(1323, 4410), 1.3) << "an in-phase stack starts loud";
    EXPECT_LT(together.levelSpread(), 0.02) << "and every repeat is the same";

    const auto apart = renderRepeats(5, 0.0f, 100.0f, 2);
    EXPECT_LT(apart.meanRms(kFade, 1323) / apart.meanRms(1323, 4410), 1.15) << "a stack started apart has no bump";
    EXPECT_GT(apart.levelSpread(), 0.05) << "the stack's thickness differs from note to note";
    EXPECT_LT(apart.levelSpread(), 0.4);
    EXPECT_GT(apart.repeatDifference(), 0.5) << "consecutive notes start differently";

    const auto half = renderRepeats(5, 0.0f, 50.0f, 2);
    EXPECT_GT(half.meanRms(kFade, 1323) / half.meanRms(1323, 4410),
              apart.meanRms(kFade, 1323) / apart.meanRms(1323, 4410))
        << "Rand 50 sits between";
    EXPECT_LT(half.meanRms(kFade, 1323) / half.meanRms(1323, 4410),
              together.meanRms(kFade, 1323) / together.meanRms(1323, 4410));
}

// Regression test for FRO1223: Phase and Rand looked dead because they only set where each unison voice starts when a
// NEW note begins. That is measured here on a detuned five-voice Saw: Phase 180 changes the first 20 ms of the note
// against Phase 0, Rand 0 repeats the note identically, and Rand 50 makes each repeat start differently.
TEST(OscillatorPhaseRand, PhaseAndRandSetTheStartOfEachNewNoteOfADetunedUnisonSaw) {
    constexpr size_t kTwentyMs = 882;
    // Largest difference between two repeats over the first 20 ms (after the hand-over), against the first one's peak.
    auto repeatGap = [&](const Repeats& r, size_t a, size_t b) {
        double diff = 0.0, peak = 1e-9;
        for (size_t i = kFade; i < kTwentyMs; ++i) {
            diff = std::max(diff, (double)std::abs(r.notes[a][i] - r.notes[b][i]));
            peak = std::max(peak, (double)std::abs(r.notes[a][i]));
        }
        return diff / peak;
    };
    const auto phaseZero = renderRepeats(5, 0.0f, 0.0f, 2);
    const auto phaseHalf = renderRepeats(5, 180.0f, 0.0f, 2);
    EXPECT_GT(relativeDifference(phaseZero, phaseHalf, kFade, kTwentyMs), 0.5)
        << "Phase 180 starts the note differently";

    EXPECT_LT(repeatGap(phaseZero, 1, 2), 1e-3) << "Rand 0 repeats the note identically";

    const auto randHalf = renderRepeats(5, 0.0f, 50.0f, 2);
    EXPECT_GT(repeatGap(randHalf, 1, 2), 0.3) << "Rand 50 starts each repeat differently";
}
