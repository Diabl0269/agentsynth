// FilterModule's Custom type per note: in poly, each voice's note picks its own place on the morph map
// (By hand, By pitch, Random), glides there at the chosen Smoothness, and Off stays the one shared kernel.
// Shapes are flat curves at very different levels, so a voice's gain tells which shape it sits on.
//
// Raw channels are written as literals on purpose (they are part of the saved-patch contract): voice v's
// Audio L is ch v, its Audio R ch 11 + v, its Pitch ch 19 + v.
#include "AllocationCounter.h"
#include "CustomFilterTestHelpers.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::test;
using Mode = CustomFilterPerNote::Mode;

namespace {

constexpr int kBlock = 512;
constexpr int kChannels = 32;
constexpr int kPitchCh = 19;
constexpr int kVoices = 8;
constexpr float kLowDb = -30.0f, kMidDb = -12.0f, kHighDb = 0.0f;

void setValue(FilterModule& m, const juce::String& id, float plain) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id) {
            r->setValueNotifyingHost(r->convertTo0to1(plain));
            return;
        }
    FAIL() << id;
}

/** Three flat shapes, left to right: low (-30 dB), mid (-12 dB), high (0 dB), all at map y 0.2, so the shared
    cursor (0.5, 0.5) hears a blend of the three. */
CustomFilterShapes levelShapes() {
    CustomFilterShapes s;
    s.shapes = {makeShape("high", {{20.0f, kHighDb}, {20000.0f, kHighDb}}, {0.9f, 0.2f}),
                makeShape("low", {{20.0f, kLowDb}, {20000.0f, kLowDb}}, {0.1f, 0.2f}),
                makeShape("mid", {{20.0f, kMidDb}, {20000.0f, kMidDb}}, {0.5f, 0.2f})};
    return s;
}
constexpr int kHighShape = 0, kLowShape = 1, kMidShape = 2;

float hzOf(int note) { return 440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f); }

/** A poly Custom filter on levelShapes() with `perNote` applied. */
struct Rig {
    FilterModule m;
    juce::AudioBuffer<float> buf{kChannels, kBlock};
    juce::MidiBuffer midi;
    std::array<int, kVoices> notes{}; // MIDI note per voice, -1 = no pitch
    std::array<bool, kVoices> sounding{};
    long long sample = 0;

    explicit Rig(const CustomFilterPerNote& perNote, bool poly = true) {
        setValue(m, "filterType", (float)FilterModule::kCustomTypeIndex);
        setValue(m, "poly", poly ? 1.0f : 0.0f);
        m.setCustomShapes(levelShapes());
        m.setCustomPerNote(perNote);
        m.prepareToPlay(kTestSampleRate, kBlock);
        notes.fill(-1);
    }

    /** One block: every sounding voice gets the same 0.5-amplitude sine on both legs, and its note's pitch. */
    void block() {
        buf.clear();
        for (int v = 0; v < kVoices; ++v) {
            if (notes[(size_t)v] >= 0)
                for (int i = 0; i < kBlock; ++i)
                    buf.setSample(kPitchCh + v, i, hzOf(notes[(size_t)v]));
            if (!sounding[(size_t)v])
                continue;
            for (int i = 0; i < kBlock; ++i) {
                const float x = 0.5f * std::sin(0.031f * (float)(sample + i));
                buf.setSample(v, i, x);
                buf.setSample(11 + v, i, x);
            }
        }
        m.processBlock(buf, midi);
        sample += kBlock;
    }
    void blocks(int n) {
        for (int i = 0; i < n; ++i)
            block();
    }
    void play(int v, int note) {
        notes[(size_t)v] = note;
        sounding[(size_t)v] = true;
    }

    /** Voice v's gain over the last block, in dB (input RMS is 0.5 / sqrt 2). */
    float gainDb(int v, int leg = 0) const {
        const float rms = buf.getRMSLevel(leg == 0 ? v : 11 + v, 0, kBlock);
        return 20.0f * std::log10(std::max(1e-9f, rms) / (0.5f / std::sqrt(2.0f)));
    }
};

CustomFilterPerNote perNote(Mode mode, float smoothness = 0.0f) {
    CustomFilterPerNote p;
    p.mode = mode;
    p.smoothness = smoothness;
    return p;
}

} // namespace

TEST(FilterPerNote, ByPitchGivesLowAndHighNotesDifferentShapes) {
    Rig rig(perNote(Mode::ByPitch));
    rig.play(0, 36); // the lowest band: the leftmost shape
    rig.play(1, 96); // the highest band: the rightmost shape
    rig.play(2, 66); // the middle band
    rig.blocks(4);
    EXPECT_EQ(rig.m.getCustomVoiceCursors().getShape(0), kLowShape);
    EXPECT_EQ(rig.m.getCustomVoiceCursors().getShape(1), kHighShape);
    EXPECT_EQ(rig.m.getCustomVoiceCursors().getShape(2), kMidShape);
    EXPECT_NEAR(rig.gainDb(0), kLowDb, 1.0f);
    EXPECT_NEAR(rig.gainDb(1), kHighDb, 1.0f);
    EXPECT_NEAR(rig.gainDb(2), kMidDb, 1.0f);
    EXPECT_NEAR(rig.gainDb(1, 1), kHighDb, 1.0f) << "the right leg follows its voice";
}

TEST(FilterPerNote, RandomPicksANewShapeOnEveryNewNote) {
    Rig rig(perNote(Mode::Random));
    int previous = -2;
    for (int note : {60, 64, 67, 72, 60, 62, 65, 69}) {
        rig.play(0, note);
        rig.blocks(3);
        const int shape = rig.m.getCustomVoiceCursors().getShape(0);
        EXPECT_GE(shape, 0);
        EXPECT_NE(shape, previous) << "note " << note;
        previous = shape;
    }
    rig.blocks(4); // a held note keeps its shape
    EXPECT_EQ(rig.m.getCustomVoiceCursors().getShape(0), previous);
}

TEST(FilterPerNote, ANoteThatStartsAfterSilenceCountsAsNew) {
    Rig rig(perNote(Mode::Random));
    rig.play(0, 60);
    rig.blocks(3);
    const int first = rig.m.getCustomVoiceCursors().getShape(0);
    rig.sounding[0] = false; // released: silence, the pitch stays where it was
    rig.blocks(2);
    rig.sounding[0] = true; // the same note again
    rig.blocks(2);
    EXPECT_NE(rig.m.getCustomVoiceCursors().getShape(0), first);
}

TEST(FilterPerNote, ByHandSendsEachKeyToItsShape) {
    auto p = perNote(Mode::ByHand);
    p.keys[60] = kHighShape;
    p.keys[62] = kLowShape;
    Rig rig(p);
    rig.play(0, 60);
    rig.play(1, 62);
    rig.play(2, 64); // not assigned: the shared cursor
    rig.blocks(4);

    Rig shared(perNote(Mode::Off));
    shared.play(2, 64);
    shared.blocks(4);
    EXPECT_NEAR(rig.gainDb(0), kHighDb, 1.0f);
    EXPECT_NEAR(rig.gainDb(1), kLowDb, 1.0f);
    EXPECT_EQ(rig.m.getCustomVoiceCursors().getShape(2), -1);
    EXPECT_NEAR(rig.gainDb(2), shared.gainDb(2), 0.05f);
}

TEST(FilterPerNote, SmoothnessZeroLandsInTheNotesFirstBlockAndOneGlides) {
    for (float smoothness : {0.0f, 1.0f}) {
        Rig rig(perNote(Mode::ByPitch, smoothness));
        rig.play(0, 36);
        rig.blocks(60); // settled on the low shape
        ASSERT_NEAR(rig.gainDb(0), kLowDb, 1.0f);
        rig.play(0, 96);
        rig.block(); // the note is spotted here
        rig.block();
        if (smoothness == 0.0f) {
            EXPECT_NEAR(rig.gainDb(0), kHighDb, 1.0f) << "a hard note jumps";
            continue;
        }
        const float early = rig.gainDb(0);
        EXPECT_LT(early, kHighDb - 10.0f) << "a smooth note is still on its way";
        rig.blocks(10);
        const float later = rig.gainDb(0);
        EXPECT_GT(later, early + 2.0f) << "and keeps moving";
        EXPECT_LT(later, kHighDb - 1.0f);
        rig.blocks(40); // 500 ms is about 43 blocks of 512
        EXPECT_NEAR(rig.gainDb(0), kHighDb, 1.0f);
    }
}

TEST(FilterPerNote, OffIsTheSharedKernelExactly) {
    auto off = perNote(Mode::Off, 0.9f);
    off.keys[60] = kHighShape;
    Rig untouched(CustomFilterPerNote{}), offRig(off), wasOn(perNote(Mode::ByPitch));
    for (auto* r : {&untouched, &offRig, &wasOn}) {
        r->play(0, 36);
        r->play(3, 96);
    }
    for (int b = 0; b < 6; ++b) {
        if (b == 2)
            wasOn.m.setCustomPerNote(perNote(Mode::Off)); // back to the shared kernel mid-stream
        for (auto* r : {&untouched, &offRig, &wasOn})
            r->block();
        for (int ch : {0, 3, 11, 14})
            for (int i = 0; i < kBlock; ++i) {
                ASSERT_EQ(offRig.buf.getSample(ch, i), untouched.buf.getSample(ch, i)) << "block " << b;
                if (b >= 3)
                    ASSERT_NEAR(wasOn.buf.getSample(ch, i), untouched.buf.getSample(ch, i), 1e-6f) << "block " << b;
            }
    }
    EXPECT_NEAR(untouched.gainDb(0), untouched.gainDb(3), 1e-3f) << "one cursor for every voice";
}

TEST(FilterPerNote, MonoIgnoresThePerNoteMode) {
    Rig mono(perNote(Mode::ByPitch), false), reference(CustomFilterPerNote{}, false);
    for (auto* r : {&mono, &reference}) {
        r->play(0, 96);
        r->blocks(4);
    }
    for (int i = 0; i < kBlock; ++i)
        ASSERT_EQ(mono.buf.getSample(0, i), reference.buf.getSample(0, i));
}

TEST(FilterPerNote, AChordSettlesWithinTheDesignBudget) {
    Rig rig(perNote(Mode::ByPitch));
    const int before = rig.m.getCustomDesignCount();
    for (int v = 0; v < kVoices; ++v)
        rig.play(v, v % 2 == 0 ? 36 : 96);
    rig.block();
    const int budget = CustomFilterEngine::voiceDesignBudget(kBlock);
    EXPECT_LE(rig.m.getCustomDesignCount() - before, budget);
    rig.blocks((kVoices + budget - 1) / budget + 1);
    for (int v = 0; v < kVoices; ++v)
        EXPECT_NEAR(rig.gainDb(v), v % 2 == 0 ? kLowDb : kHighDb, 1.0f) << "voice " << v;
}

TEST(FilterPerNote, EightMovingVoicesNeverAllocateOnTheAudioThread) {
    CustomFilterEngine engine;
    engine.setShapes(levelShapes());
    engine.setPerNote(perNote(Mode::Random, 0.6f));
    engine.prepare(kTestSampleRate);
    std::vector<std::vector<float>> bufs(16, std::vector<float>(kBlock));
    std::vector<float*> ptrs;
    for (auto& b : bufs)
        ptrs.push_back(b.data());
    std::array<CustomFilterEngine::VoiceInput, kVoices> voices{};
    auto run = [&](int blk) {
        for (int c = 0; c < 16; ++c)
            for (int i = 0; i < kBlock; ++i)
                bufs[(size_t)c][(size_t)i] = 0.3f * std::sin(0.01f * (float)((c + 1) * (blk * kBlock + i)));
        for (int v = 0; v < kVoices; ++v) {
            const float hz = hzOf(40 + 5 * v + 7 * ((blk / 6 + v) % 5)); // a new note every 6 blocks
            voices[(size_t)v] = {hz, hz, true};
        }
        engine.process(ptrs.data(), 16, kBlock, {}, voices.data(), kVoices);
    };
    run(0);
    const int before = engine.getDesignCount();
    {
        AllocationScope scope;
        for (int blk = 1; blk < 64; ++blk)
            run(blk);
        EXPECT_EQ(scope.count(), 0);
    }
    EXPECT_GE(engine.getDesignCount() - before, 100) << "the voices kept moving";
    engine.setPerNote(perNote(Mode::ByHand)); // the key map handoff does not allocate either
    {
        AllocationScope scope;
        run(64);
        EXPECT_EQ(scope.count(), 0);
    }
}

TEST(FilterPerNote, SettingsRoundTripThroughTheExtraState) {
    auto p = perNote(Mode::ByHand, 0.7f);
    p.keys[60] = 2;
    p.keys[61] = 0;
    p.keys[127] = 1;
    FilterModule a;
    a.setCustomPerNote(p);
    const auto state = a.getExtraState();
    ASSERT_FALSE(state.isVoid()) << "default shapes plus per-note settings still save";
    FilterModule b;
    b.setExtraState(juce::JSON::parse(juce::JSON::toString(state)));
    EXPECT_EQ(b.getCustomPerNote(), p);
    EXPECT_TRUE(b.getCustomShapes().isDefault());

    FilterModule plain;
    EXPECT_TRUE(plain.getExtraState().isVoid()) << "a Filter that never used either saves as before";
}

TEST(FilterPerNote, ProjectsWithoutPerNoteLoadAsOffAndNewerVersionsFallBack) {
    FilterModule old;
    old.setCustomPerNote(perNote(Mode::Random)); // something to be replaced
    old.setExtraState(levelShapes().toVar());    // a project saved before per-note settings existed
    EXPECT_TRUE(old.getCustomPerNote().isDefault());
    EXPECT_EQ(old.getCustomShapes(), levelShapes());

    auto state = levelShapes().toVar();
    auto future = perNote(Mode::ByPitch).toVar();
    future.getDynamicObject()->setProperty("version", 2);
    state.getDynamicObject()->setProperty("perNote", future);
    FilterModule newer;
    newer.setExtraState(state);
    EXPECT_TRUE(newer.getCustomPerNote().isDefault());
    EXPECT_EQ(newer.getCustomShapes(), levelShapes()) << "the shapes beside it still load";

    const auto garbage = CustomFilterPerNote::fromVar(juce::JSON::parse(
        R"({"version":1,"mode":"sideways","smoothness":9,"keys":[[200,1],[60,99],[61,1],"x",[62]]})"));
    EXPECT_EQ(garbage.mode, Mode::Off);
    EXPECT_EQ(garbage.smoothness, 1.0f);
    EXPECT_EQ(garbage.keys[61], 1);
    EXPECT_EQ(garbage.keys[60], CustomFilterPerNote::kUnassigned);
}
