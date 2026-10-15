// PluckModuleTests.cpp
// The Pluck instrument: notes are in tune from E0 to C7 at several sample rates, Decay and Release shape the
// tail, eight strings play together and a ninth takes over the oldest, velocity sets the level, nothing
// plays without a note, bypass and mute are silent, and no parameter adds more than 6 dB over its default.

#include "../InstrumentTestUtil.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Pluck/PluckModule.h"
#include <gtest/gtest.h>

using namespace instrumenttest;

namespace {

constexpr double kRate = 44100.0;

std::unique_ptr<PluckModule> makePluck(double rate = kRate) {
    auto m = std::make_unique<PluckModule>();
    m->prepareToPlay(rate, kBlock);
    return m;
}

// The module with its body resonator off, so only the string is heard.
std::unique_ptr<PluckModule> makeBareString(double rate = kRate) {
    auto m = makePluck(rate);
    setParam(*m, "body", 0.0f);
    setParam(*m, "decayTrack", 0.0f); // Decay is then the T60 of every note, not only of C3
    return m;
}

} // namespace

TEST(PluckModule, IsAMidiInstrumentWithAStereoOutAndNoInputs) {
    PluckModule m;
    EXPECT_EQ(m.getName(), "Pluck");
    EXPECT_TRUE(m.acceptsMidi());
    EXPECT_FALSE(m.producesMidi());
    EXPECT_EQ(m.getVisibleInputPortCount(), 0);
    EXPECT_EQ(m.getTotalNumInputChannels(), 0);
    EXPECT_EQ(m.getTotalNumOutputChannels(), 2);
    EXPECT_EQ(m.getModuleType(), ModuleType::Pluck);
}

TEST(PluckModule, HasEveryDocumentedParameterWithItsRangeAndDefault) {
    PluckModule m;
    struct Spec {
        const char* id;
        float lo, hi, def;
    };
    const Spec specs[] = {
        {"level", 0, 1, 0.8f},        {"tune", -24, 24, 0},       {"fine", -100, 100, 0},
        {"decay", 0.1f, 20, 3},       {"brightness", 0, 1, 0.5f}, {"pickPosition", 0.02f, 0.5f, 0.13f},
        {"pickHardness", 0, 1, 0.6f}, {"body", 0, 1, 0.3f},       {"stiffness", 0, 1, 0},
        {"release", 0.01f, 5, 0.3f},  {"velBright", 0, 1, 0.5f},  {"decayTrack", 0, 1, 0.7f}};
    for (const auto& spec : specs) {
        SCOPED_TRACE(spec.id);
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, spec.id));
        ASSERT_NE(p, nullptr);
        EXPECT_FLOAT_EQ(p->range.start, spec.lo);
        EXPECT_FLOAT_EQ(p->range.end, spec.hi);
        EXPECT_NEAR(p->get(), spec.def, 1.0e-4f);
    }
}

TEST(PluckModule, SilentWithoutNotes) {
    auto m = makePluck();
    const auto out = render(*m, {}, 0.5, kRate);
    EXPECT_EQ(peakOf(out, 0, 1, kRate), 0.0f);
    EXPECT_EQ(m->activeVoiceCount(), 0);
}

TEST(PluckModule, NotesAreInTuneFromE0ToC7) {
    // E0 (20.6 Hz) up to C7 at the base rate, plus the high and low ends at the other rates.
    struct Case {
        int note;
        double rate;
    };
    const Case cases[] = {{16, 44100}, {28, 44100}, {45, 44100}, {69, 44100}, {84, 44100},
                          {96, 44100}, {28, 48000}, {84, 48000}, {28, 96000}, {96, 96000}};
    for (const auto& c : cases) {
        SCOPED_TRACE("note " + std::to_string(c.note) + " at " + std::to_string(c.rate));
        auto m = makeBareString(c.rate);
        const double seconds = c.note < 30 ? 2.2 : 1.3;
        const auto out = render(*m, {on(0, c.note)}, seconds, c.rate);
        const double cents = centsOffFrom(out, 0.1, seconds - 0.1, noteHz(c.note), c.rate);
        EXPECT_LT(std::fabs(cents), 5.0) << cents << " cents off";
    }
}

TEST(PluckModule, TuneAndFineMoveThePitch) {
    auto m = makeBareString();
    setParam(*m, "tune", 7.0f);
    setParam(*m, "fine", 30.0f);
    const auto out = render(*m, {on(0, 57)}, 1.2, kRate);
    const double expected = noteHz(57) * std::exp2(7.3 / 12.0);
    EXPECT_LT(std::fabs(centsOffFrom(out, 0.1, 1.1, expected, kRate)), 5.0);
}

TEST(PluckModule, StiffnessKeepsTheFundamentalInTune) {
    auto m = makeBareString();
    setParam(*m, "stiffness", 1.0f);
    const auto out = render(*m, {on(0, 57)}, 1.2, kRate);
    EXPECT_LT(std::fabs(centsOffFrom(out, 0.1, 1.1, noteHz(57), kRate)), 5.0);
}

TEST(PluckModule, StiffnessStretchesTheUpperPartials) {
    auto render5th = [](float stiffness) {
        auto m = makeBareString();
        setParam(*m, "stiffness", stiffness);
        setParam(*m, "brightness", 1.0f);
        setParam(*m, "pickHardness", 1.0f);
        const auto out = render(*m, {on(0, 69)}, 1.5, kRate);
        return centsOffFrom(out, 0.05, 1.0, noteHz(69) * 4.0, kRate);
    };
    EXPECT_GT(render5th(1.0f), render5th(0.0f) + 3.0) << "a stiff string's 4th partial runs sharp";
}

TEST(PluckModule, DecayParameterSetsHowLongTheStringRings) {
    auto level = [](float decay) {
        auto m = makeBareString();
        setParam(*m, "decay", decay);
        const auto out = render(*m, {on(0, 57)}, 1.4, kRate);
        return rmsOf(out, 1.0, 1.2, kRate);
    };
    const float longDecay = level(8.0f), shortDecay = level(1.0f);
    EXPECT_GT(longDecay, 4.0f * shortDecay);
    EXPECT_GT(longDecay, 1.0e-3f) << "a long decay is still clearly audible after one second";
}

TEST(PluckModule, DecayIsRoughlyTheSixtyDecibelTimeOfTheFundamental) {
    auto m = makeBareString();
    setParam(*m, "decay", 2.0f);
    const auto out = render(*m, {on(0, 57)}, 3.5, kRate);
    const double f0 = noteHz(57);
    const double early = magnitudeAt(out, 0.2, 0.7, f0, kRate), late = magnitudeAt(out, 1.7, 2.2, f0, kRate);
    const double fallDb = 20.0 * std::log10(early / late); // the two spans are 1.5 s apart: 45 dB at T60 = 2 s
    EXPECT_NEAR(fallDb, 45.0, 6.0);
}

TEST(PluckModule, DecayTrackingRingsHighNotesShorterAndLeavesC3Alone) {
    // A C3 note rings for Decay whatever the tracking; a C6 note (8x the frequency) rings 8^-tracking as long.
    auto tailOf = [](int note, float tracking) {
        auto m = makeBareString();
        setParam(*m, "decay", 4.0f);
        setParam(*m, "decayTrack", tracking);
        const auto out = render(*m, {on(0, note)}, 1.4, kRate);
        return rmsOf(out, 1.0, 1.2, kRate) / rmsOf(out, 0.05, 0.25, kRate);
    };
    EXPECT_NEAR(tailOf(48, 1.0f), tailOf(48, 0.0f), 0.2f * tailOf(48, 0.0f));
    EXPECT_LT(tailOf(84, 1.0f), 0.25f * tailOf(84, 0.0f));
}

TEST(PluckModule, NoteOffDampsTheStringOverTheReleaseTime) {
    auto tail = [](std::vector<Event> events, float release) {
        auto m = makeBareString();
        setParam(*m, "decay", 8.0f);
        setParam(*m, "release", release);
        const auto out = render(*m, events, 1.6, kRate);
        return rmsOf(out, 1.0, 1.2, kRate);
    };
    const float held = tail({on(0, 57)}, 0.05f);
    const float quick = tail({on(0, 57), off(22050, 57)}, 0.05f);
    const float slow = tail({on(0, 57), off(22050, 57)}, 3.0f);
    EXPECT_LT(quick, 0.05f * held) << "a 50 ms release has gone quiet by 0.5 s after the key lifts";
    EXPECT_GT(slow, 0.3f * held) << "a 3 s release rings on";
    EXPECT_GT(slow, 4.0f * quick);
}

TEST(PluckModule, NoteOffDoesNotCutAbruptly) {
    auto m = makeBareString();
    setParam(*m, "decay", 8.0f);
    setParam(*m, "release", 0.4f);
    const auto out = render(*m, {on(0, 57), off(22050, 57)}, 1.5, kRate);
    EXPECT_GT(rmsOf(out, 0.52, 0.56, kRate), 0.0f) << "still sounding just after the note-off";
}

TEST(PluckModule, TwoNotesBothSound) {
    auto solo = [](int note) {
        auto m = makeBareString();
        return render(*m, {on(0, note)}, 1.0, kRate);
    };
    const auto a = solo(57), b = solo(64);
    auto m = makeBareString();
    const auto both = render(*m, {on(0, 57), on(0, 64)}, 1.0, kRate);
    EXPECT_EQ(m->activeVoiceCount(), 2);
    EXPECT_GT(magnitudeAt(both, 0.1, 0.9, noteHz(57), kRate), 0.3 * magnitudeAt(a, 0.1, 0.9, noteHz(57), kRate));
    EXPECT_GT(magnitudeAt(both, 0.1, 0.9, noteHz(64), kRate), 0.3 * magnitudeAt(b, 0.1, 0.9, noteHz(64), kRate));
}

TEST(PluckModule, ANinthNoteTakesOverTheOldestString) {
    const int notes[] = {28, 60, 62, 64, 65, 67, 69, 71};
    std::vector<Event> eight;
    for (int i = 0; i < 8; ++i)
        eight.push_back(on(i * 64, notes[i]));

    auto m = makeBareString();
    setParam(*m, "decay", 8.0f);
    const auto steady = render(*m, eight, 1.0, kRate);
    EXPECT_EQ(m->activeVoiceCount(), 8);

    auto stealing = makeBareString();
    setParam(*stealing, "decay", 8.0f);
    auto nine = eight;
    nine.push_back(on(1000, 72));
    const auto stolen = render(*stealing, nine, 1.0, kRate);
    EXPECT_EQ(stealing->activeVoiceCount(), 8) << "still eight strings, not nine";

    const double f = noteHz(28); // the oldest note is the only low one
    EXPECT_LT(magnitudeAt(stolen, 0.2, 0.9, f, kRate), 0.2 * magnitudeAt(steady, 0.2, 0.9, f, kRate));
    EXPECT_GT(magnitudeAt(stolen, 0.2, 0.9, noteHz(72), kRate), 0.0);
}

TEST(PluckModule, StringsFadeToSilenceAndFreeTheirVoices) {
    auto m = makePluck();
    setParam(*m, "decay", 0.1f);
    setParam(*m, "release", 0.01f);
    render(*m, {on(0, 60), on(100, 64), off(4000, 60), off(4000, 64)}, 3.0, kRate);
    EXPECT_EQ(m->activeVoiceCount(), 0);
}

TEST(PluckModule, VelocityScalesTheLevel) {
    auto peakAt = [](float velocity) {
        auto m = makePluck();
        return peakOf(render(*m, {on(0, 57, velocity)}, 0.5, kRate), 0, 0.5, kRate);
    };
    const float soft = peakAt(0.25f), loud = peakAt(1.0f);
    EXPECT_GT(soft, 0.0f);
    EXPECT_GT(loud, 2.5f * soft);
}

TEST(PluckModule, VelocityBrightnessOpensTheTone) {
    auto highShare = [](float velBright) {
        auto m = makeBareString();
        setParam(*m, "velBright", velBright);
        const auto out = render(*m, {on(0, 45, 1.0f)}, 0.6, kRate);
        return magnitudeAt(out, 0.0, 0.3, noteHz(45) * 12.0, kRate) / magnitudeAt(out, 0.0, 0.3, noteHz(45), kRate);
    };
    EXPECT_GT(highShare(1.0f), highShare(0.0f));
}

TEST(PluckModule, BodyAddsTheBoxResonances) {
    auto lowEnergy = [](float body) {
        auto m = makePluck();
        setParam(*m, "body", body);
        const auto out = render(*m, {on(0, 57)}, 0.8, kRate);
        return magnitudeAt(out, 0.0, 0.5, 250.0, kRate);
    };
    EXPECT_NE(lowEnergy(1.0f), lowEnergy(0.0f));
}

TEST(PluckModule, BypassAndMuteAreSilentAndStopRingingStrings) {
    for (const bool bypass : {true, false}) {
        SCOPED_TRACE(bypass ? "bypass" : "mute");
        auto m = makePluck();
        render(*m, {on(0, 57)}, 0.1, kRate);
        EXPECT_EQ(m->activeVoiceCount(), 1);
        if (bypass)
            m->setBypassed(true);
        else
            m->setMuted(true);
        const auto out = render(*m, {on(0, 60)}, 0.3, kRate);
        EXPECT_EQ(peakOf(out, 0, 1, kRate), 0.0f);
        EXPECT_EQ(m->activeVoiceCount(), 0);
        if (bypass)
            m->setBypassed(false);
        else
            m->setMuted(false);
        EXPECT_GT(peakOf(render(*m, {on(0, 60)}, 0.3, kRate), 0, 1, kRate), 0.0f) << "plays again once restored";
    }
}

TEST(PluckModule, OutputIsFiniteAcrossTheWholeNoteRange) {
    auto m = makePluck();
    setParam(*m, "stiffness", 1.0f);
    setParam(*m, "brightness", 0.0f);
    setParam(*m, "tune", 24.0f);
    for (int note = 0; note < 128; note += 7) {
        const auto out = render(*m, {on(0, note, 1.0f)}, 0.2, kRate);
        for (const float s : out)
            ASSERT_TRUE(std::isfinite(s)) << "note " << note;
        EXPECT_LE(peakOf(out, 0, 1, kRate), 1.0f);
    }
}

TEST(PluckModule, ADefaultNotePeaksWhereADefaultKickDoes) {
    DrumKitModule kit;
    kit.prepareToPlay(kRate, kBlock);
    std::vector<float> kick(22050, 0.0f);
    {
        juce::AudioBuffer<float> block(2, 22050);
        block.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, 36, 100.0f / 127.0f), 0);
        kit.processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + 22050, kick.begin());
    }
    const float kickPeak = peakOf(kick, 0, 1, kRate);
    auto m = makePluck();
    const float pluckPeak = peakOf(render(*m, {on(0, 57)}, 0.5, kRate), 0, 0.5, kRate);
    EXPECT_NEAR(20.0 * std::log10(pluckPeak / kickPeak), 0.0, 3.0) << "pluck " << pluckPeak << " kick " << kickPeak;
}

TEST(PluckModule, NoParameterAddsMoreThanSixDecibelsOverItsDefault) {
    auto peakWith = [](const char* id, float value) {
        auto m = makePluck();
        if (id != nullptr)
            setParam(*m, id, value);
        return peakOf(render(*m, {on(0, 45, 1.0f), on(0, 57, 1.0f)}, 1.0, kRate), 0, 1, kRate);
    };
    const float base = peakWith(nullptr, 0.0f);
    PluckModule probe;
    for (auto* p : probe.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
        if (ranged == nullptr || withId == nullptr || withId->paramID == "level" || withId->paramID == "bypassed" ||
            withId->paramID == "muted" || withId->paramID == "dualIO")
            continue;
        for (const float norm : {0.0f, 1.0f}) {
            SCOPED_TRACE(withId->paramID.toStdString() + " at " + std::to_string(norm));
            const float v = ranged->convertFrom0to1(norm);
            EXPECT_LE(peakWith(withId->paramID.toRawUTF8(), v), base * 2.0f);
        }
    }
}
