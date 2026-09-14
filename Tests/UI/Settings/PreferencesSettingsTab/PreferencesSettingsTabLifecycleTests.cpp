#include "PreferencesSettingsTabTestFixture.h"

// Topic: construction/paint, keyboard-focus interception, the live search filter, divider
// collapse, hint-label layout, and the scrolled content viewport.

TEST_F(PreferencesSettingsTabTest, PaintDoesNotCrash) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 400);
    juce::Image img(juce::Image::ARGB, 500, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(tab.paint(g));
    EXPECT_NO_THROW(tab.resized());
}

// ---- Round 4 follow-up: the search field must not auto-grab focus on open ---------------------
//
// The actual bug (searchField stealing OS keyboard focus the instant the Settings DialogWindow's
// peer is first shown) needs a real ComponentPeer — juce::Component::grabKeyboardFocus() is a
// documented no-op without one (see TimelineClipLaneArea.cpp's identical caveat), and nothing in
// this suite calls addToDesktop(). So this pins the code-level fix instead of the runtime
// behaviour it prevents: the tab itself now wants keyboard focus (matching
// ShortcutsSettingsTab's identical fix for its own sibling search box), which is what makes it —
// not the search field — the target the very first, unsolicited focus grab lands on.
TEST_F(PreferencesSettingsTabTest, TabItselfWantsKeyboardFocusSoItInterceptsTheOpeningFocusGrab) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.getWantsKeyboardFocus())
        << "without this, the search field is the first focus-wanting descendant and silently "
           "steals keyboard focus the moment the Settings window is first shown";
}

// An untouched filter must reproduce the exact unfiltered layout — the empty-query fast path
// groupMatches()/resized() rely on, and also the state every existing bounds-sensitive test above
// (e.g. DualIOGroupIsOneLineWithADividerBelow) assumes.
TEST_F(PreferencesSettingsTabTest, EmptySearchFilterShowsEveryRow) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    EXPECT_TRUE(tab.getSearchFilterForTest().isEmpty());

    for (const juce::String& label : {"Double-click port to disconnect", "Show Alignment Guides",
                                      "Split Left/Right jacks on new modules", "Label every key"}) {
        auto* toggle = findToggleByText(tab, label);
        ASSERT_NE(toggle, nullptr) << label;
        EXPECT_TRUE(toggle->isVisible()) << label << " should be visible with no filter applied";
    }
}

TEST_F(PreferencesSettingsTabTest, SearchFilterHidesNonMatchingRowsByLabelText) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("alignment");
    EXPECT_TRUE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Double-click port to disconnect")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

// A query that only appears in a TOOLTIP (not the visible label) must still surface the row — the
// spec is "label/tooltip text", not "label text alone".
TEST_F(PreferencesSettingsTabTest, SearchFilterMatchesByTooltipText) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    auto* alignmentToggle = findToggleByText(tab, "Show Alignment Guides");
    ASSERT_NE(alignmentToggle, nullptr);
    ASSERT_TRUE(alignmentToggle->getTooltip().containsIgnoreCase("dragging"))
        << "test premise: 'dragging' only appears in this row's tooltip, not its label";

    tab.setSearchFilterForTest("dragging");
    EXPECT_TRUE(alignmentToggle->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

// The two loop-locator toggles are one filterable group (no divider between them — see their
// declaration comments): a query matching either keeps both visible together.
TEST_F(PreferencesSettingsTabTest, SearchFilterKeepsAGroupedPairTogether) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("locator");
    EXPECT_TRUE(findToggleByText(tab, "Timeline: P (loop selection)")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "double-click inside the locators")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Label every key")->isVisible());
}

TEST_F(PreferencesSettingsTabTest, ClearingSearchFilterRestoresEveryRow) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("alignment");
    ASSERT_FALSE(findToggleByText(tab, "Label every key")->isVisible());

    tab.setSearchFilterForTest("");
    EXPECT_TRUE(findToggleByText(tab, "Label every key")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
}

// Esc clears the field — driven by invoking the real onEscapeKey callback (see
// TimelinePanelTests.cpp's identical "call onEscapeKey() directly" idiom), not a synthetic key
// event, which a headless run cannot dispatch.
TEST_F(PreferencesSettingsTabTest, EscapeClearsTheSearchFilter) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("alignment");
    ASSERT_EQ(tab.getSearchFilterForTest(), "alignment");
    ASSERT_FALSE(findToggleByText(tab, "Label every key")->isVisible());

    tab.triggerSearchEscapeForTest();
    EXPECT_TRUE(tab.getSearchFilterForTest().isEmpty());
    EXPECT_TRUE(findToggleByText(tab, "Label every key")->isVisible());
}

// Dividers must collapse sensibly: when only ONE group matches, there is nothing left for a
// hairline to separate, so none should be drawn at all.
TEST_F(PreferencesSettingsTabTest, NoDividersLeftOverWhenOnlyOneGroupMatches) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    ASSERT_GT(tab.getDividerBoundsForTest().size(), 0u) << "test premise: the unfiltered tab has dividers";

    tab.setSearchFilterForTest("label every key");
    EXPECT_TRUE(tab.getDividerBoundsForTest().empty()) << "a single matching group has no neighbour to separate from";
}

// A query matching two NON-adjacent groups — "double-click" is in both group 2's toggle text
// ("Double-click port to disconnect") and group 5's second toggle text ("Timeline: double-click
// inside the locators spans them"), with groups 3 and 4 (neither mentioning it) filtered out in
// between — must draw exactly ONE divider between the two survivors: not zero (the filtered-out
// groups must not swallow it) and not two (an orphan hairline on each side of the gap).
TEST_F(PreferencesSettingsTabTest, ExactlyOneDividerBetweenTwoSurvivingNonAdjacentGroups) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    tab.setSearchFilterForTest("double-click");
    EXPECT_TRUE(findToggleByText(tab, "Double-click port to disconnect")->isVisible());
    EXPECT_TRUE(findToggleByText(tab, "double-click inside the locators")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Show Alignment Guides")->isVisible());
    EXPECT_FALSE(findToggleByText(tab, "Split Left/Right")->isVisible());
    EXPECT_EQ(tab.getDividerBoundsForTest().size(), 1u);
}

// ---- Round 5: hint-label layout (cramped/narrow rendering), kept through round 6's revert -------
//
// naturalScrollingHint and zoomScrollUpZoomsInHint shared the same bug: fixed at 18px tall, room
// for barely one line, so a hint wider than the row got horizontally squeezed instead of wrapping.
// Both are fixed at the shared spot (PreferencesSettingsTab::styleMutedHintLabel); this pins the
// observable parts of that fix without depending on exact pixel values, which the font/theme could
// legitimately move.
TEST_F(PreferencesSettingsTabTest, HintLabelsGetTwoLinesOfHeightAndNeverSqueezeHorizontally) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);

    juce::Label* naturalHint = nullptr;
    juce::Label* zoomHint = nullptr;
    for (auto* child : descendantsOf(tab)) {
        if (auto* l = dynamic_cast<juce::Label*>(child)) {
            if (l->getText().containsIgnoreCase("graph canvas pans"))
                naturalHint = l;
            if (l->getText().contains("wheel zoom"))
                zoomHint = l;
        }
    }
    ASSERT_NE(naturalHint, nullptr);
    ASSERT_NE(zoomHint, nullptr);

    for (auto* hint : {naturalHint, zoomHint}) {
        EXPECT_GT(hint->getHeight(), 18) << "must be taller than the old one-line height";
        EXPECT_GE(hint->getMinimumHorizontalScale(), 1.0f)
            << "must never shrink text horizontally to fit - wrap onto a second line instead";
        EXPECT_FALSE(hint->getBounds().isEmpty());
    }
}

// T157: when the window is too short to show every group, the group stack lives inside a scroll
// view, so the bottom rows stay reachable via a scrollbar instead of being clipped out (the bug the
// tab used to have). A window tall enough to hold the whole stack does not overflow; a short window
// does. Overflow is a function of the window height versus the group stack, independent of the live
// search filter.
TEST_F(PreferencesSettingsTabTest, ContentScrollsWhenItOutgrowsTheWindow) {
    PreferencesSettingsTab tab(appProperties);

    tab.setSize(600, 1500);
    EXPECT_FALSE(tab.contentOverflowsViewportForTest())
        << "a window tall enough for every group keeps the content host inside the viewport";

    tab.setSize(600, 140);
    EXPECT_TRUE(tab.contentOverflowsViewportForTest())
        << "a window shorter than the group stack makes the content host outgrow the viewport";

    // Scrolling off-screen is not the same as being filtered out: when the window is short and the
    // search is empty, every group is still present in the component tree, so a top and a middle
    // group are both still findable.
    ASSERT_FALSE(descendantsOf(tab).empty()) << "the group controls must survive a short window";
    EXPECT_NE(findToggleByText(tab, "Split"), nullptr) << "a top group is never dropped";
    EXPECT_NE(findToggleByText(tab, "Show Alignment Guides"), nullptr) << "a middle group is never dropped";
}
