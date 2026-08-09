// ModuleLibraryScrollTests.cpp
// Tests for the library sidebar's vertical scrolling.
//
//   • visibility     — the bar appears only when the rows outgrow the panel, and hides again
//   • clamping       — the offset never leaves [0, overflow], including after resize/collapse
//   • hit-testing    — component-space y maps through the offset to the right row
//   • pinned strip   — the COLLAPSE ALL chrome is never a row hit, however far the rows scrolled
//   • paint          — drawing a scrolled panel does not crash

#include "../Source/UI/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

// A panel short enough that the fully expanded library (9 sections, ~30 rows) overflows it.
constexpr int kShortPanelHeight = 400;
constexpr int kPanelWidth = 200;

/** juce::ScrollBar notifies its listeners through an AsyncUpdater, so the offset only catches up
 *  once the message queue runs. */
void pumpMessageQueue() {
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(20);
}

void simulateMouseWheel(ModuleLibraryComponent& comp, int y, float deltaY) {
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(5.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &comp, &comp, juce::Time::getCurrentTime(), juce::Point<float>(5.0f, (float)y),
                         juce::Time::getCurrentTime(), 0, false);
    juce::MouseWheelDetails wheel{};
    wheel.deltaX = 0.0f;
    wheel.deltaY = deltaY;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;

    comp.mouseWheelMove(evt, wheel);
    pumpMessageQueue();
}

void simulateMouseMoveAt(ModuleLibraryComponent& comp, int y) {
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(5.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &comp, &comp, juce::Time::getCurrentTime(), juce::Point<float>(5.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    comp.mouseMove(evt);
}

} // namespace

// ============================================================================
// Visibility — the whole point of the change: overflow must become scrollable
// ============================================================================

TEST(ModuleLibraryScroll, ScrollBarAppearsWhenRowsOverflowThePanel) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);

    ASSERT_GT(comp.getTotalContentHeight(), kShortPanelHeight) << "test premise: the library must overflow 400 px";
    EXPECT_TRUE(comp.isScrollBarVisible()) << "overflowing content must expose a scrollbar";
    EXPECT_GT(comp.getMaxScrollOffset(), 0);
}

TEST(ModuleLibraryScroll, ScrollBarStaysHiddenWhenEverythingFits) {
    ModuleLibraryComponent comp;
    // Taller than the fully expanded library.
    comp.setSize(kPanelWidth, 4000);

    ASSERT_LE(comp.getTotalContentHeight(), 4000) << "test premise: the library must fit in 4000 px";
    EXPECT_FALSE(comp.isScrollBarVisible()) << "content that fits must not show a scrollbar";
    EXPECT_EQ(comp.getMaxScrollOffset(), 0);
}

TEST(ModuleLibraryScroll, MaxScrollOffsetIsExactlyTheOverflow) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);

    // Scrolled to the bottom, the last row's bottom edge sits at the panel's bottom padding — no
    // more and no less. That is what makes the last module reachable.
    EXPECT_EQ(comp.getMaxScrollOffset(), comp.getTotalContentHeight() - kShortPanelHeight);
}

TEST(ModuleLibraryScroll, ScrollBarSitsBelowThePinnedStripAtTheRightEdge) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);
    ASSERT_TRUE(comp.isScrollBarVisible());

    // Found by walking children rather than exposing the bar: the sidebar owns exactly one.
    const juce::ScrollBar* bar = nullptr;
    for (int i = 0; i < comp.getNumChildComponents(); ++i)
        if (auto* sb = dynamic_cast<juce::ScrollBar*>(comp.getChildComponent(i)))
            bar = sb;
    ASSERT_NE(bar, nullptr);

    EXPECT_EQ(bar->getRight(), kPanelWidth) << "the bar hugs the right edge";
    EXPECT_EQ(bar->getY(), ModuleLibraryComponent::kTopStripHeight) << "the bar starts below the pinned strip";
    EXPECT_EQ(bar->getBottom(), kShortPanelHeight);
    EXPECT_GT(bar->getWidth(), 0);
}

// ============================================================================
// Clamping — the offset must never point past the end of the content
// ============================================================================

TEST(ModuleLibraryScroll, ScrollOffsetIsClampedToRange) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);

    comp.setScrollOffset(999999);
    EXPECT_EQ(comp.getScrollOffset(), comp.getMaxScrollOffset()) << "over-scrolling pins to the bottom";

    comp.setScrollOffset(-500);
    EXPECT_EQ(comp.getScrollOffset(), 0) << "negative offsets pin to the top";
}

TEST(ModuleLibraryScroll, GrowingThePanelReclampsTheOffset) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);
    comp.setScrollOffset(comp.getMaxScrollOffset());
    ASSERT_GT(comp.getScrollOffset(), 0);

    // A taller panel shows more content, so a bottom-pinned view must scroll back up rather than
    // leave a gap under the last row.
    comp.setSize(kPanelWidth, 900);
    EXPECT_EQ(comp.getScrollOffset(), comp.getMaxScrollOffset());
    EXPECT_LE(comp.getScrollOffset(), comp.getTotalContentHeight() - 900);
}

TEST(ModuleLibraryScroll, CollapsingEverythingHidesTheBarAndResetsTheOffset) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);
    comp.setScrollOffset(comp.getMaxScrollOffset());
    ASSERT_GT(comp.getScrollOffset(), 0);

    comp.setAllSectionsCollapsed(true);

    ASSERT_LE(comp.getTotalContentHeight(), kShortPanelHeight) << "test premise: headers alone must fit";
    EXPECT_EQ(comp.getScrollOffset(), 0) << "content that now fits must snap back to the top";
    EXPECT_FALSE(comp.isScrollBarVisible());
}

// ============================================================================
// Wheel — the way the offset actually moves in the running app
// ============================================================================

TEST(ModuleLibraryScroll, MouseWheelScrollsTheRows) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);
    ASSERT_TRUE(comp.isScrollBarVisible());

    // Negative deltaY is a downward scroll.
    simulateMouseWheel(comp, kShortPanelHeight / 2, -1.0f);
    EXPECT_GT(comp.getScrollOffset(), 0) << "wheeling down must move the view down the list";

    const int afterDown = comp.getScrollOffset();
    simulateMouseWheel(comp, kShortPanelHeight / 2, 1.0f);
    EXPECT_LT(comp.getScrollOffset(), afterDown) << "wheeling up must move back toward the top";
}

TEST(ModuleLibraryScroll, MouseWheelDoesNothingWhenEverythingFits) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, 4000);
    ASSERT_FALSE(comp.isScrollBarVisible());

    simulateMouseWheel(comp, 100, -5.0f);
    EXPECT_EQ(comp.getScrollOffset(), 0) << "a panel with no overflow must not scroll";
}

// ============================================================================
// Hit-testing — a scrolled row must respond where it is actually drawn
// ============================================================================

TEST(ModuleLibraryScroll, HitTestingFollowsTheScrollOffset) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);

    const auto rows = comp.buildRows();
    ASSERT_FALSE(rows.empty());
    const auto& last = rows.back();
    ASSERT_GT(last.y, kShortPanelHeight) << "test premise: the last row starts off-screen unscrolled";

    comp.setScrollOffset(comp.getMaxScrollOffset());
    const int screenY = last.y + last.height / 2 - comp.getScrollOffset();
    ASSERT_GE(screenY, ModuleLibraryComponent::kTopStripHeight);
    ASSERT_LT(screenY, kShortPanelHeight);

    EXPECT_EQ(comp.getEntryIndexAtComponentY(screenY), last.entryIndex)
        << "the bottom row must be hit where the scrolled view draws it";

    simulateMouseMoveAt(comp, screenY);
    EXPECT_EQ(comp.getHoveredIndex(), last.entryIndex) << "hover must land on the row under the cursor, not above it";
}

TEST(ModuleLibraryScroll, TheSameScreenYHitsDifferentRowsAtDifferentOffsets) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);

    const int probeY = kShortPanelHeight / 2;
    const int unscrolled = comp.getEntryIndexAtComponentY(probeY);
    ASSERT_GE(unscrolled, 0);

    comp.setScrollOffset(comp.getMaxScrollOffset());
    const int scrolled = comp.getEntryIndexAtComponentY(probeY);
    ASSERT_GE(scrolled, 0);

    EXPECT_NE(unscrolled, scrolled) << "hit-testing must go through the offset, not ignore it";
}

TEST(ModuleLibraryScroll, ContentSpaceHitTestingIsUnaffectedByTheOffset) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);

    // getEntryIndexAt takes content-space y (what buildRows reports), so existing callers that
    // pair it with getRowCentreY keep working no matter where the view is scrolled.
    const auto rows = comp.buildRows();
    comp.setScrollOffset(comp.getMaxScrollOffset());
    for (const auto& row : rows)
        EXPECT_EQ(comp.getEntryIndexAt(row.y + row.height / 2), row.entryIndex);
}

TEST(ModuleLibraryScroll, PinnedTopStripIsNeverARowHitWhileScrolled) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);
    comp.setScrollOffset(comp.getMaxScrollOffset());
    ASSERT_GT(comp.getScrollOffset(), 0);

    // Scrolled far enough that rows now pass *under* the strip; the strip still owns those pixels.
    for (int y = 0; y < ModuleLibraryComponent::kTopStripHeight; ++y)
        EXPECT_EQ(comp.getEntryIndexAtComponentY(y), -1) << "y=" << y << " is pinned chrome, not a row";

    simulateMouseMoveAt(comp, 2);
    EXPECT_EQ(comp.getHoveredIndex(), -1) << "the strip must not hover a row that scrolled beneath it";
}

// ============================================================================
// Paint smoke — scrolled drawing goes through a clip + origin shift
// ============================================================================

TEST(ModuleLibraryScroll, PaintWhileScrolledDoesNotCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kShortPanelHeight);
    comp.setScrollOffset(comp.getMaxScrollOffset());

    juce::Image img(juce::Image::ARGB, kPanelWidth, kShortPanelHeight, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));

    bool hasPixel = false;
    for (int y = 0; y < img.getHeight() && !hasPixel; ++y)
        for (int x = 0; x < img.getWidth() && !hasPixel; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasPixel = true;
    EXPECT_TRUE(hasPixel) << "a scrolled panel must still paint something";
}
