// MalletModuleTests.cpp
// The Mallet instrument: the bar's fundamental is in tune for every material, the upper partials sit at the
// documented ratios, Decay, Damping and Release shape the tail, eight bars play together and a ninth takes
// over the oldest, velocity sets the level, Tremolo wobbles the output, nothing plays without a note, bypass
// and mute are silent, and no parameter adds more than 6 dB over its default.

#include "../InstrumentTestUtil.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Mallet/MalletModule.h"
#include <gtest/gtest.h>

using namespace instrumenttest;

namespace {

constexpr double kRate = 44100.0;
const char* const kMaterials[] = {"Marimba", "Vibraphone", "Xylophone", "Glockenspiel", "Bell"};

std::unique_ptr<MalletModule> makeMallet(int material = 0, double rate = kRate) {
    auto m = std::make_unique<MalletModule>();
    m->prepareToPlay(rate, kBlock);
    setParam(*m, "material", static_cast<float>(material));
    return m;
}

} // namespace

TEST(MalletModule, IsAMidiInstrumentWithAStereoOutAndNoInputs) {
    MalletModule m;
    EXPECT_EQ(m.getName(), "Mallet");
    EXPECT_TRUE(m.acceptsMidi());
    EXPECT_FALSE(m.producesMidi());
    EXPECT_EQ(m.getVisibleInputPortCount(), 0);
    EXPECT_EQ(m.getTotalNumInputChannels(), 0);
    EXPECT_EQ(m.getTotalNumOutputChannels(), 2);
    EXPECT_EQ(m.getModuleType(), ModuleType::Mallet);
}

TEST(MalletModule, HasEveryDocumentedParameterWithItsRangeAndDefault) {
    MalletModule m;
    struct Spec {
        const char* id;
        float lo, hi, def;
    };
    const Spec specs[] = {{"level", 0, 1, 0.8f},       {"tune", -24, 24, 0},      {"fine", -100, 100, 0},
                          {"decay", 0.05f, 10, 1.2f},  {"damping", 0, 1, 0.5f},   {"brightness", 0, 1, 0.5f},
                          {"hardness", 0, 1, 0.5f},    {"resonator", 0, 1, 0.5f}, {"tremolo", 0, 1, 0},
                          {"release", 0.01f, 5, 0.5f}, {"velBright", 0, 1, 0.5f}};
    for (const auto& spec : specs) {
        SCOPED_TRACE(spec.id);
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, spec.id));
        ASSERT_NE(p, nullptr);
        EXPECT_FLOAT_EQ(p->range.start, spec.lo);
        EXPECT_FLOAT_EQ(p->range.end, spec.hi);
        EXPECT_NEAR(p->get(), spec.def, 1.0e-4f);
    }
    auto* material = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&m, "material"));
    ASSERT_NE(material, nullptr);
    ASSERT_EQ(material->choices.size(), 5);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(material->choices[i], kMaterials[i]);
    EXPECT_EQ(material->getIndex(), 0) << "Marimba is the default";
}

TEST(MalletModule, SilentWithoutNotes) {
    auto m = makeMallet();
    const auto out = render(*m, {}, 0.5, kRate);
    EXPECT_EQ(peakOf(out, 0, 1, kRate), 0.0f);
    EXPECT_EQ(m->activeVoiceCount(), 0);
}

TEST(MalletModule, TheFundamentalIsInTuneForEveryMaterial) {
    for (int material = 0; material < 5; ++material) {
        for (const int note : {48, 60, 72, 84}) {
            SCOPED_TRACE(std::string(kMaterials[material]) + " note " + std::to_string(note));
            auto m = makeMallet(material);
            setParam(*m, "decay", 4.0f);
            const auto out = render(*m, {on(0, note)}, 1.2, kRate);
            EXPECT_LT(std::fabs(centsOffFrom(out, 0.1, 1.1, noteHz(note), kRate)), 5.0);
        }
    }
}

TEST(MalletModule, InTuneAtOtherSampleRates) {
    for (const double rate : {48000.0, 96000.0}) {
        for (const int note : {60, 84}) {
            SCOPED_TRACE(std::to_string(rate) + " note " + std::to_string(note));
            auto m = makeMallet(0, rate);
            setParam(*m, "decay", 4.0f);
            const auto out = render(*m, {on(0, note)}, 1.2, rate);
            EXPECT_LT(std::fabs(centsOffFrom(out, 0.1, 1.1, noteHz(note), rate)), 5.0);
        }
    }
}

TEST(MalletModule, TuneAndFineMoveThePitch) {
    auto m = makeMallet();
    setParam(*m, "tune", -5.0f);
    setParam(*m, "fine", 40.0f);
    setParam(*m, "decay", 4.0f);
    const auto out = render(*m, {on(0, 69)}, 1.2, kRate);
    EXPECT_LT(std::fabs(centsOffFrom(out, 0.1, 1.1, noteHz(69) * std::exp2(-4.6 / 12.0), kRate)), 5.0);
}

TEST(MalletModule, UpperPartialsSitAtTheMaterialRatios) {
    struct Case {
        int material;
        std::vector<double> ratios;
    };
    const Case cases[] = {{0, {3.9, 9.2}},
                          {1, {4.0, 10.0}},
                          {2, {3.0, 6.0, 9.8}},
                          {3, {2.76, 5.40, 8.93}},
                          {4, {1.2, 1.5, 2.0, 2.5, 3.0}}};
    for (const auto& c : cases) {
        auto m = makeMallet(c.material);
        setParam(*m, "decay", 4.0f);
        setParam(*m, "damping", 0.0f);
        setParam(*m, "hardness", 1.0f);
        const auto out = render(*m, {on(0, 57)}, 1.0, kRate);
        const double f0 = noteHz(57);
        const double fundamental = magnitudeAt(out, 0.05, 0.8, f0, kRate);
        for (const double ratio : c.ratios) {
            SCOPED_TRACE(std::string(kMaterials[c.material]) + " ratio " + std::to_string(ratio));
            const double at = magnitudeAt(out, 0.05, 0.8, f0 * ratio, kRate);
            EXPECT_GT(at, 0.02 * fundamental);
            EXPECT_GT(at, 4.0 * magnitudeAt(out, 0.05, 0.8, f0 * ratio * 1.04, kRate))
                << "a clear peak, 4% away it drops";
        }
    }
}

TEST(MalletModule, TheBellHasItsHumAnOctaveBelow) {
    auto m = makeMallet(4);
    setParam(*m, "decay", 4.0f);
    const auto out = render(*m, {on(0, 60)}, 1.0, kRate);
    EXPECT_GT(magnitudeAt(out, 0.05, 0.8, noteHz(60) * 0.5, kRate),
              0.02 * magnitudeAt(out, 0.05, 0.8, noteHz(60), kRate));
}

TEST(MalletModule, ModesAboveTheLimitAreSkippedAndOutputStaysFinite) {
    for (int material = 0; material < 5; ++material) {
        for (const double rate : {44100.0, 96000.0}) {
            auto m = makeMallet(material, rate);
            for (const int note : {96, 110, 127}) {
                const auto out = render(*m, {on(0, note, 1.0f)}, 0.2, rate);
                for (const float s : out)
                    ASSERT_TRUE(std::isfinite(s)) << kMaterials[material] << " note " << note;
                EXPECT_LE(peakOf(out, 0, 1, rate), 1.0f);
                EXPECT_GT(peakOf(out, 0, 1, rate), 0.0f) << "the fundamental still sounds";
            }
        }
    }
}

TEST(MalletModule, DecayParameterSetsHowLongTheBarRings) {
    auto level = [](float decay) {
        auto m = makeMallet();
        setParam(*m, "decay", decay);
        const auto out = render(*m, {on(0, 60)}, 1.6, kRate);
        return rmsOf(out, 1.0, 1.2, kRate);
    };
    EXPECT_GT(level(8.0f), 4.0f * level(1.0f));
}

TEST(MalletModule, DampingMakesTheUpperPartialsDieFaster) {
    auto upper = [](float damping) {
        auto m = makeMallet();
        setParam(*m, "decay", 3.0f);
        setParam(*m, "damping", damping);
        setParam(*m, "hardness", 1.0f);
        const auto out = render(*m, {on(0, 57)}, 1.5, kRate);
        const double f = noteHz(57);
        return magnitudeAt(out, 0.6, 1.2, f * 3.9, kRate) / magnitudeAt(out, 0.6, 1.2, f, kRate);
    };
    EXPECT_LT(upper(1.0f), 0.5 * upper(0.0f));
}

TEST(MalletModule, HardnessBrightensTheStrike) {
    auto brightShare = [](float hardness) {
        auto m = makeMallet();
        setParam(*m, "hardness", hardness);
        setParam(*m, "velBright", 0.0f);
        const auto out = render(*m, {on(0, 52)}, 0.6, kRate);
        return magnitudeAt(out, 0.0, 0.3, noteHz(52) * 9.2, kRate) / magnitudeAt(out, 0.0, 0.3, noteHz(52), kRate);
    };
    EXPECT_GT(brightShare(1.0f), 2.0 * brightShare(0.0f));
}

TEST(MalletModule, TheResonatorBoostsTheFundamental) {
    auto fundamental = [](float resonator) {
        auto m = makeMallet();
        setParam(*m, "resonator", resonator);
        const auto out = render(*m, {on(0, 60)}, 0.8, kRate);
        return magnitudeAt(out, 0.05, 0.6, noteHz(60), kRate);
    };
    EXPECT_GT(fundamental(1.0f), 1.5 * fundamental(0.0f));
}

TEST(MalletModule, NoteOffDampsTheBarOverTheReleaseTime) {
    auto tail = [](std::vector<Event> events, float release) {
        auto m = makeMallet();
        setParam(*m, "decay", 8.0f);
        setParam(*m, "release", release);
        const auto out = render(*m, events, 1.6, kRate);
        return rmsOf(out, 1.0, 1.2, kRate);
    };
    const float held = tail({on(0, 60)}, 0.05f);
    const float quick = tail({on(0, 60), off(22050, 60)}, 0.05f);
    const float slow = tail({on(0, 60), off(22050, 60)}, 3.0f);
    EXPECT_LT(quick, 0.05f * held);
    EXPECT_GT(slow, 0.15f * held);
    EXPECT_GT(slow, 4.0f * quick);
}

TEST(MalletModule, NoteOffDoesNotCutAbruptly) {
    auto m = makeMallet();
    setParam(*m, "decay", 8.0f);
    setParam(*m, "release", 0.4f);
    const auto out = render(*m, {on(0, 60), off(22050, 60)}, 1.5, kRate);
    EXPECT_GT(rmsOf(out, 0.52, 0.56, kRate), 0.0f);
    EXPECT_GT(rmsOf(out, 0.52, 0.56, kRate), 0.3f * rmsOf(out, 0.48, 0.5, kRate)) << "no step at the note-off";
}

TEST(MalletModule, TwoNotesBothSound) {
    auto solo = [](int note) {
        auto m = makeMallet();
        return render(*m, {on(0, note)}, 1.0, kRate);
    };
    const auto a = solo(57), b = solo(64);
    auto m = makeMallet();
    const auto both = render(*m, {on(0, 57), on(0, 64)}, 1.0, kRate);
    EXPECT_EQ(m->activeVoiceCount(), 2);
    EXPECT_EQ(m->displayVoiceCount(), 2) << "the status bar count follows the sounding voices";
    EXPECT_GT(magnitudeAt(both, 0.1, 0.9, noteHz(57), kRate), 0.5 * magnitudeAt(a, 0.1, 0.9, noteHz(57), kRate));
    EXPECT_GT(magnitudeAt(both, 0.1, 0.9, noteHz(64), kRate), 0.5 * magnitudeAt(b, 0.1, 0.9, noteHz(64), kRate));
}

TEST(MalletModule, ANinthNoteTakesOverTheOldestBar) {
    const int notes[] = {40, 60, 62, 64, 65, 67, 69, 71};
    std::vector<Event> eight;
    for (int i = 0; i < 8; ++i)
        eight.push_back(on(i * 64, notes[i]));

    auto m = makeMallet();
    setParam(*m, "decay", 8.0f);
    const auto steady = render(*m, eight, 1.0, kRate);
    EXPECT_EQ(m->activeVoiceCount(), 8);

    auto stealing = makeMallet();
    setParam(*stealing, "decay", 8.0f);
    auto nine = eight;
    nine.push_back(on(1000, 72));
    const auto stolen = render(*stealing, nine, 1.0, kRate);
    EXPECT_EQ(stealing->activeVoiceCount(), 8);

    const double f = noteHz(40);
    EXPECT_LT(magnitudeAt(stolen, 0.2, 0.9, f, kRate), 0.2 * magnitudeAt(steady, 0.2, 0.9, f, kRate));
}

TEST(MalletModule, BarsFadeToSilenceAndFreeTheirVoices) {
    auto m = makeMallet();
    setParam(*m, "decay", 0.1f);
    setParam(*m, "release", 0.01f);
    render(*m, {on(0, 60), on(100, 64), off(4000, 60)}, 3.0, kRate);
    EXPECT_EQ(m->activeVoiceCount(), 0);
    EXPECT_EQ(m->displayVoiceCount(), 0) << "back to zero once the release finishes";
}

TEST(MalletModule, VelocityScalesTheLevel) {
    auto peakAt = [](float velocity) {
        auto m = makeMallet();
        return peakOf(render(*m, {on(0, 60, velocity)}, 0.5, kRate), 0, 0.5, kRate);
    };
    const float soft = peakAt(0.25f), loud = peakAt(1.0f);
    EXPECT_GT(soft, 0.0f);
    EXPECT_GT(loud, 2.5f * soft);
}

TEST(MalletModule, TremoloWobblesTheLevelAtAboutFiveAndAHalfHertz) {
    auto swing = [](float depth) {
        auto m = makeMallet();
        setParam(*m, "decay", 10.0f);
        setParam(*m, "tremolo", depth);
        setParam(*m, "hardness", 0.0f); // a soft yarn mallet: next to no upper modes beating against the fundamental
        setParam(*m, "damping", 1.0f);
        const auto out = render(*m, {on(0, 60)}, 1.4, kRate);
        float lo = 1.0e9f, hi = 0.0f;
        for (double t = 0.2; t < 0.6; t += 0.02) { // 20 ms peaks: about a tenth of a tremolo cycle
            const float r = peakOf(out, t, t + 0.02, kRate);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        return lo / hi;
    };
    EXPECT_GT(swing(0.0f), 0.6f) << "only the slow decay";
    EXPECT_LT(swing(1.0f), 0.3f);
}

TEST(MalletModule, BypassAndMuteAreSilentAndStopRingingBars) {
    for (const bool bypass : {true, false}) {
        SCOPED_TRACE(bypass ? "bypass" : "mute");
        auto m = makeMallet();
        render(*m, {on(0, 60)}, 0.1, kRate);
        EXPECT_EQ(m->activeVoiceCount(), 1);
        EXPECT_EQ(m->displayVoiceCount(), 1);
        if (bypass)
            m->setBypassed(true);
        else
            m->setMuted(true);
        render(*m, {}, 0.01, kRate); // the toggle block fades out; the silent branch runs from the next one
        const auto out = render(*m, {on(0, 64)}, 0.3, kRate);
        EXPECT_EQ(peakOf(out, 0, 1, kRate), 0.0f);
        EXPECT_EQ(m->activeVoiceCount(), 0);
        EXPECT_EQ(m->displayVoiceCount(), 0);
        if (bypass)
            m->setBypassed(false);
        else
            m->setMuted(false);
        EXPECT_GT(peakOf(render(*m, {on(0, 64)}, 0.3, kRate), 0, 1, kRate), 0.0f);
    }
}

TEST(MalletModule, ADefaultNotePeaksWhereADefaultKickDoes) {
    DrumKitModule kit;
    kit.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> block(2, 22050);
    block.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, 36, 100.0f / 127.0f), 0);
    kit.processBlock(block, midi);
    const float kickPeak = block.getMagnitude(0, 0, 22050);
    for (int material = 0; material < 5; ++material) {
        auto m = makeMallet(material);
        const float peak = peakOf(render(*m, {on(0, 60)}, 0.5, kRate), 0, 0.5, kRate);
        EXPECT_NEAR(20.0 * std::log10(peak / kickPeak), 0.0, 4.0)
            << kMaterials[material] << " " << peak << " kick " << kickPeak;
    }
}

TEST(MalletModule, NoParameterAddsMoreThanSixDecibelsOverItsDefault) {
    for (int material = 0; material < 5; ++material) {
        auto peakWith = [&](const char* id, float value) {
            auto m = makeMallet(material);
            if (id != nullptr)
                setParam(*m, id, value);
            return peakOf(render(*m, {on(0, 52, 1.0f), on(0, 64, 1.0f)}, 1.0, kRate), 0, 1, kRate);
        };
        const float base = peakWith(nullptr, 0.0f);
        MalletModule probe;
        for (auto* p : probe.getParameters()) {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
            if (ranged == nullptr || withId == nullptr || withId->paramID == "level" || withId->paramID == "material" ||
                withId->paramID == "bypassed" || withId->paramID == "muted" || withId->paramID == "dualIO")
                continue;
            for (const float norm : {0.0f, 1.0f}) {
                SCOPED_TRACE(std::string(kMaterials[material]) + " " + withId->paramID.toStdString() + " at " +
                             std::to_string(norm));
                EXPECT_LE(peakWith(withId->paramID.toRawUTF8(), ranged->convertFrom0to1(norm)), base * 2.0f);
            }
        }
    }
}
