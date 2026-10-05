// SearchMatchTests.cpp -- the shared search matcher, ranking and highlight spans every search box uses
// (Source/UI/Layout/SearchMatch.h).

#include "UI/Layout/SearchMatch.h"
#include <gtest/gtest.h>

using synth::ui::searchHighlightSpans;
using synth::ui::searchMatches;
using synth::ui::searchScore;

TEST(SearchMatch, EveryWordMustAppearInAnyOrderIgnoringCase) {
    EXPECT_TRUE(searchMatches("Oscillator 8", "osc 8"));
    EXPECT_TRUE(searchMatches("Oscillator 8 - Cutoff", "cut  OSC"));
    EXPECT_TRUE(searchMatches("Oscillator", "OSC"));
    EXPECT_FALSE(searchMatches("Oscillator 7", "osc 8"));
    EXPECT_FALSE(searchMatches("Filter 1", "osc"));
}

TEST(SearchMatch, BlankQueryMatchesEverything) {
    EXPECT_TRUE(searchMatches("Anything", {}));
    EXPECT_TRUE(searchMatches("Anything", "   "));
    EXPECT_TRUE(searchMatches({}, "\t "));
}

TEST(SearchMatch, ScoreRanksPrefixBeforeWordStartBeforeInWord) {
    EXPECT_EQ(searchScore("Oscillator", "osc"), 0);
    EXPECT_EQ(searchScore("Sub Oscillator", "osc"), 1);
    EXPECT_EQ(searchScore("Position", "osc"), -1);
    EXPECT_EQ(searchScore("Reverb Oscar", "osc"), 1);
    EXPECT_EQ(searchScore("Dosc", "osc"), 2);
    EXPECT_EQ(searchScore("Filter", "osc"), -1);
    EXPECT_LT(searchScore("Rod Bank", "rod"), searchScore("Pro Rod", "rod"));
    EXPECT_LT(searchScore("Pro Rod", "rod"), searchScore("Prodrod", "rod"));
}

TEST(SearchMatch, ScoreUsesTheBestOccurrenceAndTheWorstWord) {
    EXPECT_EQ(searchScore("Dosc Osc", "osc"), 1) << "the best occurrence of a word counts";
    EXPECT_EQ(searchScore("Oscillator 8", "osc 8"), 1) << "'8' starts a word, not the text";
    EXPECT_EQ(searchScore("Oscillator x8", "osc 8"), 2) << "one weak word weakens the match";
    EXPECT_EQ(searchScore("Oscillator 7", "osc 8"), -1);
    EXPECT_EQ(searchScore("Anything", "  "), 0);
}

TEST(SearchMatch, SpansCoverEveryWordAndRepeatedHits) {
    const auto spans = searchHighlightSpans("Oscillator 8", "osc 8");
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0].start, 0);
    EXPECT_EQ(spans[0].length, 3);
    EXPECT_EQ(spans[1].start, 11);
    EXPECT_EQ(spans[1].length, 1);

    const auto repeated = searchHighlightSpans("Midi MIDI", "midi");
    ASSERT_EQ(repeated.size(), 2u);
    EXPECT_EQ(repeated[0].start, 0);
    EXPECT_EQ(repeated[1].start, 5);

    const auto mid = searchHighlightSpans("Parametric EQ", "eq");
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_EQ(mid[0].start, 11);
}

TEST(SearchMatch, SpansAreSortedAndOverlapsMerged) {
    // Query order does not decide span order.
    const auto sorted = searchHighlightSpans("Oscillator 8", "8 osc");
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0].start, 0);

    // "oscil" (0..5) and "cilla" (2..7) overlap: one merged span 0..7.
    const auto merged = searchHighlightSpans("Oscillator", "oscil cilla");
    ASSERT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].start, 0);
    EXPECT_EQ(merged[0].length, 7);
}

TEST(SearchMatch, SpansAreEmptyForABlankQueryOrNoHit) {
    EXPECT_TRUE(searchHighlightSpans("Oscillator", {}).empty());
    EXPECT_TRUE(searchHighlightSpans("Oscillator", "   ").empty());
    EXPECT_TRUE(searchHighlightSpans("Oscillator", "reverb").empty());
    EXPECT_TRUE(searchHighlightSpans({}, "osc").empty());
}

TEST(SearchMatch, ModuleAliasesNameTheEnvelopeUnderEveryOtherSynthsWord) {
    for (const char* name : {"ADSR", "ADSR 3", "Amp Env", "Filter Env 2"}) {
        const auto haystack = juce::String(name) + " " + synth::ui::moduleSearchAliases(name);
        for (const char* query : {"env", "envelope", "eg", "contour"})
            EXPECT_TRUE(searchMatches(haystack, query)) << name << " / " << query;
    }
    EXPECT_TRUE(searchMatches("Oscillator " + synth::ui::moduleSearchAliases("Oscillator"), "vco"));
    EXPECT_TRUE(searchMatches("Filter " + synth::ui::moduleSearchAliases("Filter 2"), "vcf"));
    EXPECT_TRUE(searchMatches("VCA " + synth::ui::moduleSearchAliases("VCA"), "amp"));
}

TEST(SearchMatch, ModulesWithoutAliasesAndCustomTitlesGetNone) {
    EXPECT_TRUE(synth::ui::moduleSearchAliases("Reverb").isEmpty());
    EXPECT_TRUE(synth::ui::moduleSearchAliases("My lead envelope").isEmpty());
    EXPECT_TRUE(synth::ui::moduleSearchAliases({}).isEmpty());
}
