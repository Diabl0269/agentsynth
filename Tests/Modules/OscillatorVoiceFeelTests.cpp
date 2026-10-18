// Oscillator voice feel: Phase and Rand restart every unison voice on a note.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/Oscillator/HeldNotes.h"
#include "Modules/OscillatorModule.h"

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

TEST(OscillatorVoiceFeel, NewParametersExistWithNeutralDefaults) {
    OscillatorModule osc;
    EXPECT_NEAR(actualOf(osc, "phase"), 0.0f, 1e-4f);
    EXPECT_NEAR(actualOf(osc, "randomPhase"), 0.0f, 1e-4f);
    EXPECT_NEAR(param(osc, "phase")->convertFrom0to1(1.0f), 360.0f, 1e-3f);
    EXPECT_NEAR(param(osc, "randomPhase")->convertFrom0to1(1.0f), 100.0f, 1e-3f);
}

TEST(OscillatorVoiceFeel, NewParametersSurviveSaveAndLoad) {
    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    auto& module = *node->getProcessor();
    setActual(module, "phase", 123.0f);
    setActual(module, "randomPhase", 40.0f);

    const juce::var json = synth::AIStateMapper::graphToJSON(source);
    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(restored.getNumNodes(), 1);
    auto& back = *restored.getNodes().getFirst()->getProcessor();
    EXPECT_NEAR(actualOf(back, "phase"), 123.0f, 0.05f);
    EXPECT_NEAR(actualOf(back, "randomPhase"), 40.0f, 0.05f);

    juce::MemoryBlock state;
    module.getStateInformation(state);
    OscillatorModule viaBinary;
    viaBinary.setStateInformation(state.getData(), (int)state.getSize());
    EXPECT_NEAR(actualOf(viaBinary, "phase"), 123.0f, 0.05f);
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
