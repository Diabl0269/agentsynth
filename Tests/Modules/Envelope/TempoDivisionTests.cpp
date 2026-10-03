// TempoDivisionTests.cpp -- the one app-wide tempo-division list (EnvelopeTempoSync.h): its names and order,
// the beat length of each entry, that every module offering a division picker offers exactly this list, and that
// a project keeps the choice by NAME across save/load (so old patches written when the list was longest-first
// still load the right division).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/LFOModule.h"
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

TEST(TempoDivision, ListIsTheEightNamesShortestFirst) {
    const juce::StringArray expected{"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
    EXPECT_EQ(synth::envelopeNoteDivisions(), expected);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/4"), 5);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/1"), 7);
    EXPECT_EQ(synth::envelopeNoteDivisionIndex("1/3"), -1);
}

TEST(TempoDivision, BeatsRunFromAThirtySecondOfABeatToFour) {
    const float expected[] = {0.03125f, 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    for (int i = 0; i < 8; ++i)
        EXPECT_FLOAT_EQ(synth::envelopeNoteDivisionBeats(i), expected[i]) << synth::envelopeNoteDivisions()[i];
    EXPECT_FLOAT_EQ(synth::envelopeNoteDivisionBeats(-3), 0.03125f) << "clamps low";
    EXPECT_FLOAT_EQ(synth::envelopeNoteDivisionBeats(99), 4.0f) << "clamps high";
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
    EXPECT_EQ(choiceOf(lfo, "rateSync")->choices, synth::envelopeNoteDivisions());
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
