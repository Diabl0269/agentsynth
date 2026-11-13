// SequencerStepCableTests.cpp
// The Sequencer's and Poly Sequencer's BPM, Transpose, Gate, Velocity and Chance knobs take cables (five channels, no
// jacks). BPM is read every block; the others on the sample each step hits, and no cable is ever written into a knob.

#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "SequencerTestSupport.h"
#include <functional>

using namespace seqtest;

namespace {

constexpr int kRate = 44100;
constexpr int kQuarter = 22050; // samples per step at 120 BPM, Rate 1/4

struct Hit {
    juce::int64 sample = 0;
    int note = 0;
    int velocity = 0;
    bool on = false;
};

// Free-runs the module for `total` samples. `cableAt(paramId, absoluteSample)` is each cable's value; nullptr = no
// cable.
std::vector<Hit> play(synth::SequencerModuleBase& seq, int total, const char* paramId = nullptr,
                      const std::function<float(juce::int64)>& cableAt = nullptr) {
    setNumber(seq, "run", 1.0f);
    std::vector<Hit> out;
    const int channel = paramId != nullptr ? seq.modulationChannelForParam(paramId) : -1;
    for (juce::int64 position = 0; position < total; position += 512) {
        const int n = (int)std::min<juce::int64>(512, total - position);
        juce::AudioBuffer<float> buffer(seq.getTotalNumInputChannels(), n);
        buffer.clear();
        if (channel >= 0)
            for (int i = 0; i < n; ++i)
                buffer.setSample(channel, i, cableAt(position + i));
        juce::MidiBuffer midi;
        seq.processBlock(buffer, midi);
        for (const auto meta : midi) {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
                out.push_back(
                    {position + meta.samplePosition, msg.getNoteNumber(), (int)msg.getVelocity(), msg.isNoteOn()});
        }
    }
    return out;
}

std::vector<Hit> ons(const std::vector<Hit>& hits) {
    std::vector<Hit> out;
    for (const auto& h : hits)
        if (h.on)
            out.push_back(h);
    return out;
}

template <class Module>
std::unique_ptr<Module> make() {
    auto seq = std::make_unique<Module>();
    seq->prepareToPlay(kRate, 512);
    return seq;
}

float knob(synth::SequencerModuleBase& seq, const char* id) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&seq, id));
    return p->convertFrom0to1(p->getValue());
}

} // namespace

TEST(SequencerStepCable, EveryStepKnobIsAKnobOnlyTargetOnItsOwnChannel) {
    for (const bool poly : {false, true}) {
        SCOPED_TRACE(poly);
        auto seq = poly ? std::unique_ptr<synth::SequencerModuleBase>(make<PolySequencerModule>())
                        : std::unique_ptr<synth::SequencerModuleBase>(make<SequencerModule>());
        EXPECT_EQ(seq->getTotalNumInputChannels(), 5);
        EXPECT_EQ(seq->getTotalNumOutputChannels(), 0);
        EXPECT_EQ(seq->getVisibleInputPortCount(), 0) << "no jack on the card";
        const char* ids[] = {"bpm", "transpose", "gate", "velocity", "chance"};
        const auto targets = seq->getModulationTargets();
        ASSERT_EQ(targets.size(), 5u);
        for (int k = 0; k < 5; ++k) {
            EXPECT_EQ(targets[(size_t)k].paramId, ids[k]);
            EXPECT_EQ(targets[(size_t)k].channelIndex, k);
            EXPECT_TRUE(targets[(size_t)k].knobOnly);
            EXPECT_EQ(seq->getInputPortLabel(k), targets[(size_t)k].name);
        }
    }
}

// Regression test for FRO1556: the Sequencer's Transpose, Gate, Velocity, Chance and BPM took no cable.
TEST(SequencerStepCable, TransposeIsSampledOnTheSampleEachStepHitsAndNeverWrittenIntoTheKnob) {
    auto plain = make<SequencerModule>();
    const auto base = ons(play(*plain, 3 * kQuarter));
    ASSERT_GE(base.size(), 3u);

    // +0.25 of Transpose's -24..24 is +12 semitones, but only from sample 30000: after step 1 (at 22050) has played.
    auto cabled = make<SequencerModule>();
    const auto hits =
        ons(play(*cabled, 3 * kQuarter, "transpose", [](juce::int64 s) { return s < 30000 ? 0.0f : 0.25f; }));
    ASSERT_GE(hits.size(), 3u);
    EXPECT_EQ(hits[0].note, base[0].note);
    EXPECT_EQ(hits[1].note, base[1].note) << "the cable moved after this step's sample, so it plays as before";
    EXPECT_EQ(hits[2].note, base[2].note + 12) << "the third step reads +12 on its own sample";
    EXPECT_EQ(knob(*cabled, "transpose"), 0.0f) << "the knob itself is never written";
}

TEST(SequencerStepCable, GateVelocityAndChanceMoveEachStepsNote) {
    auto plain = make<SequencerModule>();
    const auto base = play(*plain, 2 * kQuarter);
    const auto baseOffs = std::find_if(base.begin(), base.end(), [](const Hit& h) { return !h.on; });
    ASSERT_NE(baseOffs, base.end());

    auto shortGate = make<SequencerModule>();
    const auto shortHits = play(*shortGate, 2 * kQuarter, "gate", [](juce::int64) { return -0.25f; });
    const auto shortOff = std::find_if(shortHits.begin(), shortHits.end(), [](const Hit& h) { return !h.on; });
    ASSERT_NE(shortOff, shortHits.end());
    EXPECT_LT(shortOff->sample, baseOffs->sample) << "a lower Gate ends the note sooner";

    auto soft = make<SequencerModule>();
    const auto softHits = ons(play(*soft, 2 * kQuarter, "velocity", [](juce::int64) { return -0.25f; }));
    ASSERT_FALSE(softHits.empty());
    EXPECT_LT(softHits[0].velocity, ons(base)[0].velocity) << "a lower Velocity plays softer";

    auto never = make<SequencerModule>();
    EXPECT_TRUE(ons(play(*never, 4 * kQuarter, "chance", [](juce::int64) { return -1.0f; })).empty())
        << "Chance swept to zero plays no step";
}

TEST(SequencerStepCable, BpmCableChangesTheClockAndPolyTransposeMatchesMono) {
    auto plain = make<SequencerModule>();
    const auto base = ons(play(*plain, 3 * kQuarter));
    auto quick = make<SequencerModule>();
    // BPM 30..300 with 120 as the knob: +0.2 is about 174 BPM, so the second step comes sooner.
    const auto fast = ons(play(*quick, 3 * kQuarter, "bpm", [](juce::int64) { return 0.2f; }));
    ASSERT_GE(fast.size(), 2u);
    EXPECT_LT(fast[1].sample, base[1].sample);

    auto poly = make<PolySequencerModule>();
    auto polyPlain = make<PolySequencerModule>();
    const auto shifted = ons(play(*poly, kQuarter, "transpose", [](juce::int64) { return 0.25f; }));
    const auto unshifted = ons(play(*polyPlain, kQuarter));
    ASSERT_FALSE(shifted.empty());
    ASSERT_FALSE(unshifted.empty());
    EXPECT_GT(shifted[0].note, unshifted[0].note);
}
