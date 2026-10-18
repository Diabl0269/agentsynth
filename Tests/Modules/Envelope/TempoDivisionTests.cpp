// TempoDivisionTests.cpp -- the one app-wide tempo-division list (EnvelopeTempoSync.h): its names and order,
// the beat length of each entry, that every module offering a division picker offers exactly this list, and that
// a project keeps the choice by NAME across save/load (so old patches written when the list was longest-first
// still load the right division).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "Modules/Envelope/NoteLength.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/LFOModule.h"
#include "Modules/LfoRateDivisions.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {
juce::AudioParameterChoice* choiceOf(juce::AudioProcessor& p, const juce::String& id) {
    for (auto* param : p.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(param); c != nullptr && c->paramID == id)
            return c;
    return nullptr;
}

template <typename Module>
Module* firstNodeOf(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* m = dynamic_cast<Module*>(node->getProcessor()))
            return m;
    return nullptr;
}

juce::var patchWith(const juce::String& type, const juce::String& paramId, const juce::var& value, int id) {
    auto* params = new juce::DynamicObject();
    params->setProperty(paramId, value);
    auto* node = new juce::DynamicObject();
    node->setProperty("id", id);
    node->setProperty("type", type);
    node->setProperty("params", juce::var(params));
    return juce::var(node);
}

juce::var patchOf(std::initializer_list<juce::var> nodes) {
    juce::Array<juce::var> list;
    for (const auto& n : nodes)
        list.add(n);
    auto* root = new juce::DynamicObject();
    root->setProperty("nodes", juce::var(list));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    return juce::var(root);
}
} // namespace

TEST(TempoDivision, ListKeepsTheEightStraightNamesThenAppendsTripletsAndDotted) {
    const auto& names = synth::envelopeNoteDivisions();
    ASSERT_EQ(names.size(), 24);
    const juce::StringArray straight{"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(names[i], straight[i]) << "the old names keep their old index";
        EXPECT_EQ(names[8 + i], straight[i] + "T") << "triplets follow, shortest first";
        EXPECT_EQ(names[16 + i], straight[i] + "D") << "then the dotted values";
    }
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/4"), 5);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/1"), 7);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/8T"), 12);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/4D"), 21);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/3"), -1);
}

TEST(TempoDivision, EveryNameRoundTripsThroughItsIndexAndKeepsItsKind) {
    for (const auto* list : {&synth::envelopeNoteDivisions(), &synth::lfoRateDivisions()}) {
        for (int i = 0; i < list->size(); ++i) {
            const juce::String name = (*list)[i];
            EXPECT_EQ(list->indexOf(name), i) << name << " is listed once";
            const auto note = synth::parseNoteLength(name);
            ASSERT_TRUE(note.has_value()) << name;
            EXPECT_EQ(synth::noteLengthName(*note), name);
        }
    }
}

TEST(TempoDivision, BeatsRunFromAThirtySecondOfABeatToFour) {
    const float expected[] = {0.03125f, 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    for (int i = 0; i < 8; ++i)
        EXPECT_FLOAT_EQ(synth::envelopeNoteDivisionBeats(i), expected[i]) << synth::envelopeNoteDivisions()[i];
    EXPECT_FLOAT_EQ(synth::envelopeNoteDivisionBeats(-3), 0.03125f) << "clamps low";
    EXPECT_FLOAT_EQ(synth::envelopeNoteDivisionBeats(99), 6.0f) << "clamps to the last entry, 1/1D";
}

TEST(TempoDivision, TripletIsTwoThirdsAndDottedOneAndAHalfTimesTheStraightLength) {
    const auto beatsOf = [](const char* name) {
        return synth::envelopeNoteDivisionBeats(synth::envelopeNoteDivisionIndex(name));
    };
    EXPECT_NEAR(beatsOf("1/8T"), 1.0f / 3.0f, 1e-6f);
    EXPECT_FLOAT_EQ(beatsOf("1/4D"), 1.5f);
    EXPECT_FLOAT_EQ(beatsOf("1/1D"), 6.0f);
    EXPECT_NEAR(beatsOf("1/128T"), 0.03125f * 2.0f / 3.0f, 1e-7f);
    EXPECT_NEAR(beatsOf("1/1T"), 8.0f / 3.0f, 1e-6f);
    for (int i = 0; i < 8; ++i) {
        const float straight = synth::envelopeNoteDivisionBeats(i);
        EXPECT_NEAR(synth::envelopeNoteDivisionBeats(8 + i), straight * 2.0f / 3.0f, 1e-6f);
        EXPECT_NEAR(synth::envelopeNoteDivisionBeats(16 + i), straight * 1.5f, 1e-6f);
    }
    EXPECT_FLOAT_EQ(synth::envelopeLongestDivisionBeats(), 6.0f);
    // At 120 bpm a beat is 0.5 s: the stage length follows the same maths.
    EXPECT_NEAR(synth::envelopeNoteDivisionSeconds(synth::envelopeNoteDivisionIndex("1/4D"), 120.0), 0.75f, 1e-6f);
}

TEST(TempoDivision, NoteLengthNamesParseOnlyRealNoteLengths) {
    EXPECT_FALSE(synth::parseNoteLength("Off").has_value());
    EXPECT_FALSE(synth::parseNoteLength("Bar").has_value());
    EXPECT_FALSE(synth::parseNoteLength("1/").has_value());
    EXPECT_FALSE(synth::parseNoteLength("0/4").has_value());
    EXPECT_FALSE(synth::parseNoteLength("x/4").has_value());
    EXPECT_FALSE(synth::parseNoteLength("1/4X").has_value());
    const auto bars = synth::parseNoteLength("2/1");
    ASSERT_TRUE(bars.has_value());
    EXPECT_EQ(bars->count, 2);
    EXPECT_DOUBLE_EQ(synth::noteLengthBeats(*bars), 8.0);
    EXPECT_EQ(synth::noteKindOf("1/16T"), synth::NoteKind::triplet);
    EXPECT_EQ(synth::noteKindOf("1/16D"), synth::NoteKind::dotted);
    EXPECT_EQ(synth::noteKindOf("1/16"), synth::NoteKind::straight);
    EXPECT_EQ(synth::noteKindOf("Off"), synth::NoteKind::straight);
}

TEST(TempoDivision, EveryDivisionPickerOffersTheSameList) {
    ADSRModule adsr;
    LFOModule lfo;
    DelayModule delay;
    for (const char* id : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"}) {
        ASSERT_NE(choiceOf(adsr, id), nullptr) << id;
        EXPECT_EQ(choiceOf(adsr, id)->choices, synth::envelopeNoteDivisions()) << id;
    }
    ASSERT_NE(choiceOf(lfo, "rateSync"), nullptr);
    EXPECT_EQ(choiceOf(lfo, "rateSync")->choices, synth::lfoRateDivisions()) << "the LFO adds 2/1, 4/1, 8/1";
    ASSERT_NE(choiceOf(delay, "timeDiv"), nullptr);
    EXPECT_EQ(choiceOf(delay, "timeDiv")->choices, synth::envelopeNoteDivisions());
}

TEST(TempoDivision, DefaultsKeptTheirNames) {
    ADSRModule adsr;
    LFOModule lfo;
    DelayModule delay;
    EXPECT_EQ(choiceOf(adsr, "attackDiv")->getCurrentChoiceName(), "1/32");
    EXPECT_EQ(choiceOf(adsr, "holdDiv")->getCurrentChoiceName(), "1/32");
    EXPECT_EQ(choiceOf(adsr, "decayDiv")->getCurrentChoiceName(), "1/2");
    EXPECT_EQ(choiceOf(adsr, "releaseDiv")->getCurrentChoiceName(), "1/32");
    EXPECT_EQ(choiceOf(lfo, "rateSync")->getCurrentChoiceName(), "1/4");
    EXPECT_EQ(choiceOf(delay, "timeDiv")->getCurrentChoiceName(), "1/4");
}

TEST(TempoDivision, ProjectRoundTripRestoresTheSameNames) {
    juce::AudioProcessorGraph g1;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(
        patchOf({patchWith("LFO", "rateSync", "1/16", 1), patchWith("ADSR", "attackDiv", "1/64", 2)}), g1, true,
        /*trusted=*/false));
    auto* lfo1 = firstNodeOf<LFOModule>(g1);
    auto* adsr1 = firstNodeOf<ADSRModule>(g1);
    ASSERT_NE(lfo1, nullptr);
    ASSERT_NE(adsr1, nullptr);
    EXPECT_EQ(choiceOf(*lfo1, "rateSync")->getCurrentChoiceName(), "1/16");
    EXPECT_EQ(choiceOf(*adsr1, "attackDiv")->getCurrentChoiceName(), "1/64");

    const juce::var saved = synth::AIStateMapper::graphToJSON(g1);
    juce::AudioProcessorGraph g2;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(saved, g2, true, /*trusted=*/true));
    auto* lfo2 = firstNodeOf<LFOModule>(g2);
    auto* adsr2 = firstNodeOf<ADSRModule>(g2);
    ASSERT_NE(lfo2, nullptr);
    ASSERT_NE(adsr2, nullptr);
    EXPECT_EQ(choiceOf(*lfo2, "rateSync")->getCurrentChoiceName(), "1/16");
    EXPECT_EQ(choiceOf(*lfo2, "rateSync")->getIndex(), 3);
    EXPECT_EQ(choiceOf(*adsr2, "attackDiv")->getCurrentChoiceName(), "1/64");
    EXPECT_EQ(choiceOf(*adsr2, "attackDiv")->getIndex(), 1);
}

TEST(TempoDivision, OldPatchSavedByNameLoadsAtTheNewIndex) {
    // A patch written when "1/1" was index 0 stores the NAME; it must land on "1/1" (now index 7), not index 0.
    for (const bool trusted : {false, true}) {
        juce::AudioProcessorGraph g;
        ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patchOf({patchWith("LFO", "rateSync", "1/1", 1)}), g, true,
                                                           trusted));
        auto* lfo = firstNodeOf<LFOModule>(g);
        ASSERT_NE(lfo, nullptr);
        EXPECT_EQ(choiceOf(*lfo, "rateSync")->getIndex(), 7) << "trusted=" << trusted;
        EXPECT_EQ(choiceOf(*lfo, "rateSync")->getCurrentChoiceName(), "1/1");
    }
}

// Regression test for FRO775: an old project names "1/4" and must resolve to the same index and the same seconds as
// before the triplets and dotted values were appended.
TEST(TempoDivision, OldProjectNamingOneQuarterKeepsItsIndexAndSeconds) {
    for (const bool trusted : {false, true}) {
        juce::AudioProcessorGraph g;
        ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(
            patchOf({patchWith("Delay", "timeDiv", "1/4", 1), patchWith("ADSR", "decayDiv", "1/2", 2)}), g, true,
            trusted));
        auto* delay = firstNodeOf<DelayModule>(g);
        auto* adsr = firstNodeOf<ADSRModule>(g);
        ASSERT_NE(delay, nullptr);
        ASSERT_NE(adsr, nullptr);
        EXPECT_EQ(choiceOf(*delay, "timeDiv")->getIndex(), 5) << "trusted=" << trusted;
        EXPECT_NEAR(synth::envelopeNoteDivisionSeconds(choiceOf(*delay, "timeDiv")->getIndex(), 120.0), 0.5f, 1e-6f);
        EXPECT_EQ(choiceOf(*adsr, "decayDiv")->getIndex(), 6);
        EXPECT_NEAR(synth::envelopeNoteDivisionSeconds(choiceOf(*adsr, "decayDiv")->getIndex(), 120.0), 1.0f, 1e-6f);
    }
}

// Regression test for FRO775: a plugin-host blob saved before the lists grew holds a value normalised over the old
// eight choices (and, in the oldest blobs, no saved name); it must decode over eight, not over 24 or 27.
TEST(TempoDivision, LegacyNormalisedValueDecodesOverTheOldEightChoices) {
    const auto stateWith = [](const juce::String& id, float normalised) {
        juce::ValueTree state("ModuleState");
        state.setProperty(id, normalised, nullptr);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
        return block;
    };
    const float fifth = 5.0f / 7.0f; // "1/4" of the old eight
    {
        ADSRModule adsr;
        for (const char* id : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"}) {
            const auto block = stateWith(id, fifth);
            adsr.setStateInformation(block.getData(), (int)block.getSize());
            EXPECT_EQ(choiceOf(adsr, id)->getCurrentChoiceName(), "1/4") << id;
        }
    }
    {
        DelayModule delay;
        const auto block = stateWith("timeDiv", 1.0f);
        delay.setStateInformation(block.getData(), (int)block.getSize());
        EXPECT_EQ(choiceOf(delay, "timeDiv")->getCurrentChoiceName(), "1/1") << "the old last entry stays 1/1";
    }
    {
        LFOModule lfo;
        const auto block = stateWith("rateSync", fifth);
        lfo.setStateInformation(block.getData(), (int)block.getSize());
        EXPECT_EQ(choiceOf(lfo, "rateSync")->getCurrentChoiceName(), "1/4");
    }
}

TEST(TempoDivision, SavedChoiceNameWinsOverTheNormalisedValue) {
    ADSRModule a;
    choiceOf(a, "attackDiv")->operator=(synth::envelopeNoteDivisionIndex("1/8D"));
    juce::MemoryBlock block;
    a.getStateInformation(block);
    ADSRModule b;
    b.setStateInformation(block.getData(), (int)block.getSize());
    EXPECT_EQ(choiceOf(b, "attackDiv")->getCurrentChoiceName(), "1/8D");
}

// The LFO's own list is the shared one with three longer cycles inserted after the straight values, then the
// triplets and the dotted values, so existing indices keep meaning.
TEST(TempoDivision, LfoListAppendsTheLongerCyclesThenTripletsAndDotted) {
    const auto& lfo = synth::lfoRateDivisions();
    ASSERT_EQ(lfo.size(), 27);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(lfo[i], synth::envelopeNoteDivisions()[i]);
    EXPECT_EQ(lfo[8], "2/1");
    EXPECT_EQ(lfo[9], "4/1");
    EXPECT_EQ(lfo[10], "8/1");
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(lfo[11 + i], synth::envelopeNoteDivisions()[8 + i]) << "the same triplet and dotted names and order";
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(5), 1.0f);
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(7), 4.0f);
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(8), 8.0f);
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(9), 16.0f);
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(10), 32.0f);
    EXPECT_NEAR(synth::lfoRateDivisionBeats(synth::lfoRateDivisions().indexOf("1/8T")), 1.0f / 3.0f, 1e-6f);
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(synth::lfoRateDivisions().indexOf("1/1D")), 6.0f);
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(-1), 0.03125f) << "clamps low";
    EXPECT_FLOAT_EQ(synth::lfoRateDivisionBeats(99), 6.0f) << "clamps to the last entry";
    EXPECT_EQ(synth::kLfoStraightRateDivisionCount, 11);
    for (int i = 0; i < synth::kLfoStraightRateDivisionCount; ++i)
        EXPECT_EQ(synth::noteKindOf(lfo[i]), synth::NoteKind::straight);
}

// A stage dragged in the graph keeps its kind: a triplet stage lands on the nearest triplet, never on a straight value
// that happens to be closer in time.
TEST(TempoDivision, ADragSnapsOnlyAmongTheDivisionsOfTheStagesOwnKind) {
    const auto& names = synth::envelopeNoteDivisions();
    constexpr double bpm = 120.0;
    const double halfSecond = 0.5; // exactly 1/4 at 120 bpm
    EXPECT_EQ(names[synth::nearestEnvelopeDivisionIndex(halfSecond, bpm, names.indexOf("1/8"))], "1/4");
    EXPECT_EQ(names[synth::nearestEnvelopeDivisionIndex(halfSecond, bpm, names.indexOf("1/8T"))], "1/2T")
        << "0.667 s is the closer triplet; the straight 1/4 is not a candidate";
    EXPECT_EQ(names[synth::nearestEnvelopeDivisionIndex(halfSecond, bpm, names.indexOf("1/16D"))], "1/8D")
        << "0.375 s is the closer dotted value";
}

TEST(TempoDivision, TheLongestStageCeilingIsADottedWholeNote) {
    EXPECT_DOUBLE_EQ(synth::envelopeLongestDivisionBeats(), 6.0);
    EXPECT_EQ(synth::envelopeNoteDivisions()[synth::envelopeNoteDivisions().size() - 1], "1/1D");
}
