// SectionStripGeometryTests.cpp — the Sections strip's pure geometry and wording: bar snapping of the make-drag, the
// words a screen reader and a tooltip use, and the unique-name rule.
#include "UI/Timeline/TimelineSectionStrip/SectionStripGeometry.h"
#include <gtest/gtest.h>

using namespace synth::ui::sectionstrip;

TEST(SectionStripGeometryTest, DragSnapsOutwardToWholeBarsAndNeverUnderOneBar) {
    const auto span = makeDragSpan(5.0, 9.5, 4.0, {});
    EXPECT_DOUBLE_EQ(span.start, 4.0);
    EXPECT_DOUBLE_EQ(span.end, 12.0);
    const auto tiny = makeDragSpan(5.0, 5.2, 4.0, {});
    EXPECT_DOUBLE_EQ(tiny.start, 4.0);
    EXPECT_DOUBLE_EQ(tiny.end, 8.0) << "a click-sized drag still makes one whole bar";
    const auto backwards = makeDragSpan(9.5, 5.0, 4.0, {});
    EXPECT_DOUBLE_EQ(backwards.start, 4.0);
    EXPECT_DOUBLE_EQ(backwards.end, 12.0);
}

TEST(SectionStripGeometryTest, DragStopsAtTheNeighbouringBlocks) {
    const std::vector<Span> blocks{{0.0, 8.0}, {16.0, 24.0}};
    const auto span = makeDragSpan(10.0, 30.0, 4.0, blocks);
    EXPECT_DOUBLE_EQ(span.start, 8.0);
    EXPECT_DOUBLE_EQ(span.end, 16.0) << "the drag never covers the block to its right";
    const auto left = makeDragSpan(10.0, 2.0, 4.0, blocks);
    EXPECT_DOUBLE_EQ(left.start, 8.0) << "nor the block to its left";
}

TEST(SectionStripGeometryTest, WordsReadBarsOneBasedAndEndInclusive) {
    EXPECT_EQ(barsPhrase(48.0, 32.0, 4.0), "bars 13 to 20");
    EXPECT_EQ(barsPhrase(4.0, 4.0, 4.0), "bar 2");
    EXPECT_EQ(playsPhrase(1), "plays once");
    EXPECT_EQ(playsPhrase(3), "plays 3 times");
    EXPECT_EQ(blockTitle("Chorus", 48.0, 32.0, 4.0, 1, 3), "Chorus, bars 13 to 20, 1 of 3");
    const auto tip = blockTooltip("Chorus", 32.0, 4.0, 3, "Cmd + D");
    EXPECT_TRUE(tip.contains("8 bars")) << tip;
    EXPECT_TRUE(tip.contains("plays 3 times")) << tip;
    EXPECT_TRUE(tip.contains("Cmd + D")) << tip;
}

TEST(SectionStripGeometryTest, UniqueNameTakesTheNextFreeNumberFromTwo) {
    juce::StringArray taken;
    taken.add("Chorus");
    EXPECT_EQ(uniqueSectionName("Chorus", taken), "Chorus 2");
    taken.add("Chorus 2");
    EXPECT_EQ(uniqueSectionName("Chorus", taken), "Chorus 3");
    EXPECT_EQ(uniqueSectionName("Chorus 2", taken), "Chorus 3") << "a trailing number is replaced, not stacked";
    EXPECT_EQ(uniqueSectionName("Verse", taken), "Verse 2");
}
