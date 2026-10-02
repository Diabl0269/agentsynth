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

TEST(ShortcutHintLayout, LaterBubbleStaggersIntoASecondRowWhenSlidingHalfItsWidthIsNotEnough) {
    // Identical anchors: the overlap is the whole width and a slide is capped at half, so the later bubble
    // drops one row (a row of narrow icon buttons with wide "Shift+2" key text off the Mac).
    const std::vector<hint::BubbleRequest> requests{{{100, 10, 20, 20}, {30, 16}, {}},
                                                    {{100, 10, 20, 20}, {30, 16}, {}}};
    const auto placed = hint::placeBubbles(requests, kWindow);
    ASSERT_TRUE(placed[0].has_value());
    ASSERT_TRUE(placed[1].has_value());
    EXPECT_EQ(placed[1]->getX(), placed[0]->getX());
    EXPECT_EQ(placed[1]->getY(), placed[0]->getBottom() + hint::kStaggerGap);
    EXPECT_FALSE(placed[0]->intersects(*placed[1]));
}

TEST(ShortcutHintLayout, LaterBubbleIsDroppedWhenNoSecondRowFits) {
    // The container ends right under the first row, so there is nowhere to stagger to.
    const Rectangle<int> strip{0, 0, 800, 50};
    const std::vector<hint::BubbleRequest> requests{{{100, 10, 20, 20}, {30, 16}, strip},
                                                    {{100, 10, 20, 20}, {30, 16}, strip}};
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

// ---- The entrance tween ----

TEST(ShortcutHintLayout, AnimatedBubbleStartsSmallAndCentredOnItsOrigin) {
    const Rectangle<float> target{100.0f, 50.0f, 40.0f, 20.0f};
    const Point<float> origin{30.0f, 10.0f};
    const auto start = hint::animatedBubbleBounds(target, origin, 0.0f);
    EXPECT_NEAR(start.getCentreX(), origin.x, 1e-4f);
    EXPECT_NEAR(start.getCentreY(), origin.y, 1e-4f);
    EXPECT_NEAR(start.getWidth(), 40.0f * hint::kBubbleStartScale, 1e-4f);
    EXPECT_NEAR(start.getHeight(), 20.0f * hint::kBubbleStartScale, 1e-4f);
    EXPECT_FLOAT_EQ(hint::kBubbleStartScale, 0.6f);
}

TEST(ShortcutHintLayout, AnimatedBubbleSettlesExactlyOnItsTarget) {
    const Rectangle<float> target{100.0f, 50.0f, 40.0f, 20.0f};
    const auto end = hint::animatedBubbleBounds(target, {30.0f, 10.0f}, 1.0f);
    EXPECT_NEAR(end.getX(), target.getX(), 1e-4f);
    EXPECT_NEAR(end.getY(), target.getY(), 1e-4f);
    EXPECT_NEAR(end.getWidth(), target.getWidth(), 1e-4f);
    EXPECT_NEAR(end.getHeight(), target.getHeight(), 1e-4f);
    EXPECT_EQ(hint::animatedBubbleBounds(target, {30.0f, 10.0f}, 2.0f), end) << "t clamps at 1";
    EXPECT_EQ(hint::animatedBubbleBounds(target, {30.0f, 10.0f}, -1.0f),
              hint::animatedBubbleBounds(target, {30.0f, 10.0f}, 0.0f))
        << "t clamps at 0";
}

TEST(ShortcutHintLayout, AnimatedBubbleGrowsAndTravelsMonotonically) {
    const Rectangle<float> target{100.0f, 50.0f, 40.0f, 20.0f};
    const Point<float> origin{30.0f, 10.0f};
    auto previous = hint::animatedBubbleBounds(target, origin, 0.0f);
    for (int i = 1; i <= 20; ++i) {
        const auto now = hint::animatedBubbleBounds(target, origin, (float)i / 20.0f);
        EXPECT_GE(now.getWidth(), previous.getWidth());
        EXPECT_GE(now.getHeight(), previous.getHeight());
        EXPECT_GE(now.getCentreX(), previous.getCentreX()) << "moves toward a target right of the origin";
        EXPECT_GE(now.getCentreY(), previous.getCentreY()) << "moves toward a target below the origin";
        previous = now;
    }
}

TEST(ShortcutHintLayout, BubbleGrowsOutOfTheButtonItLabels) {
    const Rectangle<int> button{100, 10, 40, 40};
    const auto below = hint::placeBubble({button, {30, 16}, {}}, kWindow);
    ASSERT_TRUE(below.has_value());
    const auto belowKind = hint::kindOfPlacedBubble(*below, button);
    EXPECT_EQ(belowKind, hint::BubbleKind::BelowAnchor);
    EXPECT_EQ(hint::bubbleOrigin(belowKind, below->toFloat(), button.toFloat()), Point<float>(120.0f, 30.0f));

    const Rectangle<int> lowButton{100, 570, 40, 24};
    const auto above = hint::placeBubble({lowButton, {30, 16}, {}}, kWindow);
    ASSERT_TRUE(above.has_value());
    const auto aboveKind = hint::kindOfPlacedBubble(*above, lowButton);
    EXPECT_EQ(aboveKind, hint::BubbleKind::AboveAnchor);
    EXPECT_EQ(hint::bubbleOrigin(aboveKind, above->toFloat(), lowButton.toFloat()), Point<float>(120.0f, 582.0f))
        << "a flipped bubble slides up out of the same anchor centre";
}

TEST(ShortcutHintLayout, TabBubbleGrowsOutOfTheTabCentre) {
    const Rectangle<int> tab{0, 5, 260, 17};
    const Rectangle<int> bubble{170, 6, 24, 15};
    EXPECT_EQ(hint::bubbleOrigin(hint::BubbleKind::InsideTab, bubble.toFloat(), tab.toFloat()),
              Point<float>(130.0f, 13.5f));
}

TEST(ShortcutHintLayout, HiddenRowPillRisesFromBelowItsSlot) {
    const Rectangle<float> pill{300.0f, 540.0f, 90.0f, 24.0f};
    const auto origin = hint::bubbleOrigin(hint::BubbleKind::HiddenRow, pill, {});
    EXPECT_FLOAT_EQ(origin.x, pill.getCentreX());
    EXPECT_FLOAT_EQ(origin.y, pill.getCentreY() + 12.0f);
    EXPECT_FLOAT_EQ(hint::kPillRisePx, 12.0f);
}

TEST(ShortcutHintLayout, ResumingAFadePicksUpFromTheCurrentValue) {
    EXPECT_FLOAT_EQ(hint::tweenUp(0.4f, 0.0f), 0.4f) << "no jump back to 0 when Cmd returns mid fade-out";
    EXPECT_FLOAT_EQ(hint::tweenUp(0.4f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(hint::tweenDown(0.4f, 0.0f), 0.4f);
    EXPECT_FLOAT_EQ(hint::tweenDown(0.4f, 1.0f), 0.0f);
    EXPECT_DOUBLE_EQ(hint::resumeDurationMs(0.75f, 160.0), 40.0) << "only the remaining share of the fade-in";
    EXPECT_DOUBLE_EQ(hint::resumeDurationMs(1.0f, 160.0), 1.0) << "never zero";
    EXPECT_DOUBLE_EQ(hint::resumeDurationMs(0.0f, 160.0), 160.0);
}

TEST(ShortcutHintLayout, HiddenPillsAre24PxTall) { EXPECT_EQ(hint::kPillHeight, 24); }
