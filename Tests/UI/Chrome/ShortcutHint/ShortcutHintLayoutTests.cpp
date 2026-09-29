// Concern: the pure placement rules for the Cmd-hold shortcut hints (no Components involved).
#include "UI/Chrome/ShortcutHint/ShortcutHintLayout.h"
#include <gtest/gtest.h>

namespace {
using juce::Point;
using juce::Rectangle;
namespace hint = synth::ui::hint;

const Rectangle<int> kWindow{0, 0, 800, 600};
} // namespace

TEST(ShortcutHintLayout, BubbleIsCentredUnderTheButtonOverlappingItsBottomEdge) {
    const auto bubble = hint::placeBubble({{100, 10, 40, 40}, {30, 16}, {}}, kWindow);
    ASSERT_TRUE(bubble.has_value());
    EXPECT_EQ(bubble->getCentreX(), 120);
    EXPECT_EQ(bubble->getY(), 50 - hint::kBubbleOverlap);
    EXPECT_EQ(bubble->getWidth(), 30);
    EXPECT_EQ(bubble->getHeight(), 16);
}

TEST(ShortcutHintLayout, BubbleFlipsAboveWhenItWouldLeaveTheWindow) {
    const auto bubble = hint::placeBubble({{100, 570, 40, 24}, {30, 16}, {}}, kWindow);
    ASSERT_TRUE(bubble.has_value());
    EXPECT_EQ(bubble->getBottom(), 570 + hint::kBubbleOverlap);
    EXPECT_LT(bubble->getY(), 570);
}

TEST(ShortcutHintLayout, BubbleFlipsAboveWhenItWouldLeaveItsContainer) {
    // The button sits at the bottom of a panel; below it would spill past the panel's edge.
    const Rectangle<int> panel{0, 300, 800, 100};
    const auto bubble = hint::placeBubble({{100, 350, 40, 40}, {30, 16}, panel}, kWindow);
    ASSERT_TRUE(bubble.has_value());
    EXPECT_LE(bubble->getBottom(), panel.getBottom());
    EXPECT_LT(bubble->getY(), 390);
}

TEST(ShortcutHintLayout, BubbleIsNudgedSidewaysToStayInTheWindow) {
    const auto bubble = hint::placeBubble({{0, 10, 20, 20}, {40, 16}, {}}, kWindow);
    ASSERT_TRUE(bubble.has_value());
    EXPECT_EQ(bubble->getX(), 0);
}

TEST(ShortcutHintLayout, BubbleIsOmittedWhenNeitherSideFits) {
    const Rectangle<int> tiny{0, 0, 800, 20};
    EXPECT_FALSE(hint::placeBubble({{100, 0, 20, 20}, {30, 16}, {}}, tiny).has_value());
}

TEST(ShortcutHintLayout, LaterBubbleSlidesSidewaysByTheOverlap) {
    // Two 30-wide bubbles whose centres are 20 apart overlap by 10; the later one moves right by 10.
    const std::vector<hint::BubbleRequest> requests{{{100, 10, 20, 20}, {30, 16}, {}},
                                                    {{120, 10, 20, 20}, {30, 16}, {}}};
    const auto placed = hint::placeBubbles(requests, kWindow);
    ASSERT_TRUE(placed[0].has_value());
    ASSERT_TRUE(placed[1].has_value());
    EXPECT_EQ(placed[1]->getX(), placed[0]->getRight());
    EXPECT_FALSE(placed[0]->intersects(*placed[1]));
}

TEST(ShortcutHintLayout, LaterBubbleSlidesLeftWhenItSitsLeftOfTheEarlierOne) {
    const std::vector<hint::BubbleRequest> requests{{{120, 10, 20, 20}, {30, 16}, {}},
                                                    {{100, 10, 20, 20}, {30, 16}, {}}};
    const auto placed = hint::placeBubbles(requests, kWindow);
    ASSERT_TRUE(placed[1].has_value());
    EXPECT_EQ(placed[1]->getRight(), placed[0]->getX());
}

TEST(ShortcutHintLayout, LaterBubbleIsDroppedWhenSlidingHalfItsWidthIsNotEnough) {
    // Identical anchors: the overlap is the whole width, but a slide is capped at half.
    const std::vector<hint::BubbleRequest> requests{{{100, 10, 20, 20}, {30, 16}, {}},
                                                    {{100, 10, 20, 20}, {30, 16}, {}}};
    const auto placed = hint::placeBubbles(requests, kWindow);
    EXPECT_TRUE(placed[0].has_value());
    EXPECT_FALSE(placed[1].has_value());
}

TEST(ShortcutHintLayout, NonOverlappingBubblesStayPut) {
    const std::vector<hint::BubbleRequest> requests{{{100, 10, 20, 20}, {30, 16}, {}},
                                                    {{300, 10, 20, 20}, {30, 16}, {}}};
    const auto placed = hint::placeBubbles(requests, kWindow);
    ASSERT_TRUE(placed[0].has_value());
    ASSERT_TRUE(placed[1].has_value());
    EXPECT_EQ(placed[1]->getCentreX(), 310);
}

TEST(ShortcutHintLayout, DockTabBubbleSitsSixPixelsAfterTheNameAndCentredVertically) {
    const Rectangle<int> tab{0, 4, 200, 18};
    const auto bubble = hint::placeInsideTab(tab, 120, {24, 14});
    ASSERT_TRUE(bubble.has_value());
    EXPECT_EQ(bubble->getX(), 120 + hint::kTabNameGap);
    EXPECT_EQ(bubble->getCentreY(), tab.getCentreY());
    EXPECT_EQ(bubble->getHeight(), 14);
}

TEST(ShortcutHintLayout, DockTabBubbleIsOmittedWhenItDoesNotFitInsideTheTab) {
    EXPECT_FALSE(hint::placeInsideTab({0, 4, 100, 18}, 90, {24, 14}).has_value());
}

TEST(ShortcutHintLayout, HiddenDockRowIsCentredInOrderAboveTheStatusBar) {
    const auto row = hint::layoutHiddenRow({60, 80, 100}, hint::kPillHeight, kWindow, 576);
    ASSERT_EQ(row.size(), 3u);
    for (const auto& pill : row)
        EXPECT_EQ(pill.getBottom(), 576 - hint::kRowBottomMargin);
    EXPECT_EQ(row[0].getWidth(), 60);
    EXPECT_EQ(row[2].getWidth(), 100);
    EXPECT_EQ(row[1].getX(), row[0].getRight() + hint::kPillGap);
    EXPECT_EQ(row[2].getX(), row[1].getRight() + hint::kPillGap);
    // Centred: equal space either side.
    EXPECT_NEAR(row[0].getX(), kWindow.getWidth() - row[2].getRight(), 1);
}

TEST(ShortcutHintLayout, HiddenDockRowOfNothingIsEmpty) {
    EXPECT_TRUE(hint::layoutHiddenRow({}, hint::kPillHeight, kWindow, 576).empty());
}
