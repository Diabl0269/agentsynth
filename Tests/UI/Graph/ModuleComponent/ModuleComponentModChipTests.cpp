// The hover chip's text formatting and row list (formatModHoverChipText, modHoverChipRows,
// ModuleComponentModChip.h) -- no ModuleComponent, no LookAndFeel, no graph. See ModuleComponentModBandTests.cpp for
// the sibling depth-band geometry test and its own file-header note on the split.

#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include <gtest/gtest.h>

using synth::ui::formatModHoverChipText;
using synth::ui::KnobModSource;
using synth::ui::modHoverChipRows;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {
KnobModSource source(juce::uint32 id, const juce::String& name, float amount, bool bypassed = false) {
    KnobModSource s;
    s.attenuverterId = NodeID(id);
    s.sourceName = name;
    s.amount = amount;
    s.bypassed = bypassed;
    return s;
}
} // namespace

TEST(ModHoverChipTextTest, PositiveAmountGetsAnExplicitPlusSign) {
    EXPECT_EQ(formatModHoverChipText("LFO", 0.63f), juce::String::fromUTF8("LFO \xC2\xB7 +63%"));
}

TEST(ModHoverChipTextTest, NegativeAmountKeepsItsOwnMinusSign) {
    EXPECT_EQ(formatModHoverChipText("Env 2", -0.40f), juce::String::fromUTF8("Env 2 \xC2\xB7 -40%"));
}

TEST(ModHoverChipTextTest, ZeroAmountHasNoSign) {
    EXPECT_EQ(formatModHoverChipText("LFO", 0.0f), juce::String::fromUTF8("LFO \xC2\xB7 0%"));
}

TEST(ModHoverChipTextTest, RoundsToTheNearestPercent) {
    // 0.126 -> 12.6% rounds to 13%.
    EXPECT_EQ(formatModHoverChipText("LFO", 0.126f), juce::String::fromUTF8("LFO \xC2\xB7 +13%"));
}

TEST(ModHoverChipRowsTest, OneRowPerSourceWithTheChosenOneBold) {
    const auto rows = modHoverChipRows({source(1, "LFO 1", 0.5f), source(2, "LFO 2", -0.25f)}, NodeID(2));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].text, formatModHoverChipText("LFO 1", 0.5f));
    EXPECT_FALSE(rows[0].bold);
    EXPECT_TRUE(rows[0].positive);
    EXPECT_EQ(rows[1].text, formatModHoverChipText("LFO 2", -0.25f));
    EXPECT_TRUE(rows[1].bold);
    EXPECT_FALSE(rows[1].positive);
}

TEST(ModHoverChipRowsTest, ASingleSourceIsOneRow) {
    EXPECT_EQ(modHoverChipRows({source(1, "LFO", 0.1f)}, NodeID(1)).size(), 1u);
    EXPECT_TRUE(modHoverChipRows({}, NodeID()).empty());
}

TEST(ModHoverChipRowsTest, MoreThanFourSourcesEndInAMoreRow) {
    std::vector<KnobModSource> sources;
    for (juce::uint32 i = 1; i <= 5; ++i)
        sources.push_back(source(i, "LFO " + juce::String(i), 0.1f));
    const auto rows = modHoverChipRows(sources, NodeID(1));
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_FALSE(rows[3].isMore);
    EXPECT_TRUE(rows[4].isMore);
    EXPECT_EQ(rows[4].text, "+1 more");

    sources.push_back(source(6, "LFO 6", 0.1f));
    EXPECT_EQ(modHoverChipRows(sources, NodeID(1)).back().text, "+2 more");
}

TEST(ModHoverChipRowsTest, TheChosenSourceStaysListedPastTheCap) {
    std::vector<KnobModSource> sources;
    for (juce::uint32 i = 1; i <= 6; ++i)
        sources.push_back(source(i, "LFO " + juce::String(i), 0.1f));
    const auto rows = modHoverChipRows(sources, NodeID(6));
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_TRUE(rows[3].bold);
    EXPECT_EQ(rows[3].text, formatModHoverChipText("LFO 6", 0.1f));
}

TEST(ModHoverChipRowsTest, BypassedAndUnnamedSourcesAreSkippedAndNotCounted) {
    std::vector<KnobModSource> sources{source(1, "LFO 1", 0.5f, true), source(2, "", 0.5f), source(3, "LFO 3", 0.5f)};
    for (juce::uint32 i = 4; i <= 6; ++i)
        sources.push_back(source(i, "LFO " + juce::String(i), 0.5f));
    const auto rows = modHoverChipRows(sources, NodeID(3));
    ASSERT_EQ(rows.size(), 4u) << "six listed sources would have meant a more row";
    EXPECT_EQ(rows[0].text, formatModHoverChipText("LFO 3", 0.5f));
}
