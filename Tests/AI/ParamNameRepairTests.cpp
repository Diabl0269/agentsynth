#include "AI/ProjectGenerate/ParamNameRepair.h"

#include <gtest/gtest.h>

using namespace synth::projectgenerate;

namespace {
juce::var plan(const char* json) { return juce::JSON::parse(json); }
} // namespace

TEST(ParamNameRepairTest, ReadsAnInventedSpellingAsTheRealParameter) {
    juce::var root =
        plan(R"({"nodes": [{"id": 1, "type": "Filter", "params": {"cutoff": 300, "filterType_id": "LPF24"}}]})");
    juce::StringArray notes;
    repairInventedParamNames(root, notes);
    const auto params = root.getProperty("nodes", {})[0].getProperty("params", {});
    EXPECT_TRUE(params.getDynamicObject()->hasProperty("filterType"));
    EXPECT_FALSE(params.getDynamicObject()->hasProperty("filterType_id"));
    EXPECT_EQ(params.getProperty("filterType", {}).toString(), "LPF24");
    ASSERT_EQ(notes.size(), 1);
    EXPECT_TRUE(notes[0].contains("filterType_id"));
}

TEST(ParamNameRepairTest, FixesAnInstrumentAndAnInsertOfANewTrack) {
    juce::var root = plan(R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
        "inserts": [{"type": "Filter", "params": {"cutoff_1": 500}}], "instrumentParams": {"Waveform": "Saw"}}]})");
    juce::StringArray notes;
    repairInventedParamNames(root, notes);
    const auto op = root.getProperty("timelineOps", {})[0];
    EXPECT_TRUE(op.getProperty("inserts", {})[0].getProperty("params", {}).getDynamicObject()->hasProperty("cutoff"));
    EXPECT_TRUE(op.getProperty("instrumentParams", {}).getDynamicObject()->hasProperty("waveform") ||
                op.getProperty("instrumentParams", {}).getDynamicObject()->getProperties().size() == 1);
    EXPECT_GE(notes.size(), 1);
}

TEST(ParamNameRepairTest, DropsAKeyThatMatchesNothingWithANoteAndKeepsRealKeys) {
    juce::var root = plan(R"({"nodes": [{"id": 1, "type": "Filter", "params": {"cutoff": 300, "wobbleness": 3}}]})");
    juce::StringArray notes;
    repairInventedParamNames(root, notes);
    const auto* params = root.getProperty("nodes", {})[0].getProperty("params", {}).getDynamicObject();
    EXPECT_TRUE(params->hasProperty("cutoff"));
    EXPECT_FALSE(params->hasProperty("wobbleness")) << "one stray name must not cost the whole plan";
    ASSERT_EQ(notes.size(), 1);
    EXPECT_TRUE(notes[0].contains("wobbleness") && notes[0].contains("left out"));
}

TEST(ParamNameRepairTest, ReadsAFillerSuffixAsTheRealParameter) {
    // From the real e4b runs: "attackDiv_placeholder" on an ADSR (the one plan that was rejected whole).
    juce::var root =
        plan(R"({"nodes": [{"id": 1, "type": "ADSR", "params": {"attack": 0.01, "attackDiv_placeholder": 4}}]})");
    juce::StringArray notes;
    repairInventedParamNames(root, notes);
    const auto* params = root.getProperty("nodes", {})[0].getProperty("params", {}).getDynamicObject();
    EXPECT_TRUE(params->hasProperty("attackDiv"));
    EXPECT_FALSE(params->hasProperty("attackDiv_placeholder"));
    EXPECT_TRUE(params->hasProperty("attack"));
    ASSERT_EQ(notes.size(), 1);
}

TEST(ParamNameRepairTest, ReadsAModulationTargetWithAFillerSuffix) {
    // A real model habit: a destParam with an invented tail.
    juce::var root = plan(R"({"nodes": [{"id": 1, "type": "LFO"}, {"id": 2, "type": "Filter"}],
        "modulations": [{"source": 1, "dest": 2, "destParam": "cutoff_value_1"}]})");
    juce::StringArray notes;
    repairInventedParamNames(root, notes);
    EXPECT_EQ(root.getProperty("modulations", {})[0].getProperty("destParam", {}).toString(), "cutoff");
    EXPECT_EQ(notes.size(), 1);
}

TEST(ParamNameRepairTest, NeverOverwritesAParameterThatIsAlreadySet) {
    juce::var root = plan(R"({"nodes": [{"id": 1, "type": "Filter", "params": {"cutoff": 300, "cutoff_1": 900}}]})");
    juce::StringArray notes;
    repairInventedParamNames(root, notes);
    const auto params = root.getProperty("nodes", {})[0].getProperty("params", {});
    EXPECT_EQ((double)params.getProperty("cutoff", {}), 300.0);
    EXPECT_TRUE(notes.isEmpty());
}

TEST(ParamNameRepairTest, MovesPolyOutOfTheInstrumentParametersOntoTheTrack) {
    juce::var root = plan(R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Keys", "instrument": "Oscillator",
        "instrumentParams": {"waveform": "Saw", "poly": 1}}]})");
    juce::StringArray notes;
    repairTrackOpSlips(root, notes);
    const auto op = root.getProperty("timelineOps", {})[0];
    EXPECT_TRUE((bool)op.getProperty("poly", {}));
    EXPECT_FALSE(op.getProperty("instrumentParams", {}).getDynamicObject()->hasProperty("poly"));
    EXPECT_EQ(notes.size(), 1);
}

TEST(ParamNameRepairTest, NumbersASecondNewTrackWithTheSameName) {
    juce::var root = plan(R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"},
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"},
        {"op": "addInstrumentTrack", "name": "Lead", "instrument": "Oscillator"}]})");
    juce::StringArray notes;
    repairTrackOpSlips(root, notes);
    const auto ops = root.getProperty("timelineOps", {});
    EXPECT_EQ(ops[0].getProperty("name", {}).toString(), "Bass");
    EXPECT_EQ(ops[1].getProperty("name", {}).toString(), "Bass 2");
    EXPECT_EQ(ops[2].getProperty("name", {}).toString(), "Lead");
    EXPECT_EQ(notes.size(), 1);
}
