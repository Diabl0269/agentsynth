#include "AI/ProjectGenerate/LocalProjectPrompt.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::projectgenerate;

namespace {
juce::StringArray types(const juce::String& request, const juce::StringArray& patchTypes = {}) {
    return moduleTypesForRequest(request, patchTypes, AIStateMapper::authorableModuleTypes());
}
} // namespace

// The prompt is the whole point of the ticket: a small model re-reads it before every answer.
TEST(LocalProjectPromptTest, TheRulesStayWithinAFewKilobytesAndLeaveNoPlaceholder) {
    const juce::String rules = localProjectRules("Agent Synth");
    EXPECT_LT(rules.getNumBytesAsUTF8(), 6000);
    EXPECT_FALSE(rules.contains("{{"));
    EXPECT_TRUE(rules.startsWith("You are Agent Synth AI"));
    for (const char* mustTeach : {"addInstrumentTrack", "placeClips", "writeLane", "modulations", "destParam"})
        EXPECT_TRUE(rules.contains(mustTeach)) << mustTeach;
}

TEST(LocalProjectPromptTest, TheRulesAreAscii) {
    const juce::String rules = localProjectRules("Agent Synth");
    for (auto c : rules)
        EXPECT_LT((int)c, 128);
}

TEST(LocalProjectPromptTest, AlwaysCarriesTheCoreModules) {
    const auto picked = types("make it nicer");
    for (const char* core : {"Oscillator", "Filter", "LFO", "Distortion"})
        EXPECT_TRUE(picked.contains(core)) << core;
}

TEST(LocalProjectPromptTest, AddsAModuleTheRequestNamesOrHintsAt) {
    EXPECT_TRUE(types("add some reverb").contains("Reverb"));
    EXPECT_TRUE(types("an echo on the lead").contains("Delay"));
    EXPECT_FALSE(types("a plucky lead").contains("Reverb"));
}

TEST(LocalProjectPromptTest, ShortHintWordsMatchWholeWordsOnly) {
    EXPECT_FALSE(types("what is that request").contains("Parametric EQ"));
    EXPECT_TRUE(types("boost the eq a bit").contains("Parametric EQ"));
    EXPECT_FALSE(types("they hated it").contains("Compressor"));
}

TEST(LocalProjectPromptTest, WordingNeverPullsInTheModulesATrackBuildsItself) {
    const auto picked = types("a plucky lead with a filter envelope and an amp env");
    for (const char* leftOut : {"Pluck", "Filter Env", "Amp Env", "ADSR", "Audio Output"})
        EXPECT_FALSE(picked.contains(leftOut)) << leftOut;
}

TEST(LocalProjectPromptTest, KeepsEveryModuleTheProjectAlreadyUses) {
    EXPECT_TRUE(types("a plucky lead", {"Chorus"}).contains("Chorus"));
}

TEST(LocalProjectPromptTest, TheTableCarriesOnlyTheChosenModules) {
    const juce::StringArray chosen{"Oscillator", "Filter"};
    const juce::String table = AIStateMapper::getModuleSchema(&chosen);
    EXPECT_TRUE(table.contains("#### Oscillator"));
    EXPECT_TRUE(table.contains("#### Filter"));
    EXPECT_FALSE(table.contains("#### Reverb"));
    EXPECT_LT(table.length(), AIStateMapper::getModuleSchema().length() / 4);
}

TEST(LocalProjectPromptTest, TheWholePromptIsRulesThenTable) {
    const juce::StringArray chosen{"Oscillator"};
    const juce::String prompt = buildLocalProjectPrompt("Agent Synth", chosen);
    EXPECT_TRUE(prompt.startsWith(localProjectRules("Agent Synth")));
    EXPECT_TRUE(prompt.contains("#### Oscillator"));
    EXPECT_FALSE(prompt.contains("Modulation Targets"));
}
