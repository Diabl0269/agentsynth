#include "AI/ProjectGenerate/LocalProjectPrompt.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/ProjectGenerate/SongLayout.h"
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
    EXPECT_LT(rules.getNumBytesAsUTF8(), 6500);
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

// A whole song asks for the song layout: the rules grow by the song section and one worked song, the drums are always
// offered, and the worked answer is itself a song the expander reads.
TEST(LocalProjectPromptTest, AWholeSongAddsTheSongRulesAndAWorkedSong) {
    const juce::String plain = localProjectRules("Agent Synth");
    const juce::String song = localProjectRules("Agent Synth", true);
    EXPECT_TRUE(song.startsWith(plain));
    EXPECT_FALSE(plain.contains("\"song\""));
    for (const char* mustTeach : {"\"patterns\"", "\"sections\"", "start:length:pitch:velocity", "Drum Kit pitches"})
        EXPECT_TRUE(song.contains(mustTeach)) << mustTeach;
    EXPECT_LT(song.getNumBytesAsUTF8(), 10000);
    EXPECT_FALSE(song.contains("{{"));
    for (auto c : song)
        EXPECT_LT((int)c, 128);
    EXPECT_TRUE(types("make a dark techno track", {}).contains("Drum Kit") == false);
    EXPECT_TRUE(moduleTypesForRequest("make a dark techno track", {}, AIStateMapper::authorableModuleTypes(), true)
                    .contains("Drum Kit"));
}

TEST(LocalProjectPromptTest, TheWorkedSongIsAValidSongLayout) {
    const juce::String song = localProjectRules("Agent Synth", true);
    const int start = song.indexOf("{\"nodes\": [], \"connections\": [], \"mode\": \"merge\", \"timelineOps\": "
                                   "[{\"op\": \"addInstrumentTrack\", \"name\": \"Drums\"");
    ASSERT_GE(start, 0);
    const juce::var plan = juce::JSON::parse(song.substring(start).upToFirstOccurrenceOf("\n", false, false));
    ASSERT_TRUE(plan.isObject()) << "the worked song must be valid JSON";
    juce::StringArray warnings;
    const juce::var expanded = expandSong(plan, &warnings);
    EXPECT_TRUE(warnings.isEmpty()) << warnings.joinIntoString("; ");
    EXPECT_FALSE(expanded.getProperty("song", {}).isObject()) << "expanded into ordinary operations";
    EXPECT_GT(expanded.getProperty("timelineOps", {}).size(), 4);
}
