#include "../../Layout/FadeVisibilityTestGuard.h"
#include "PreferencesSettingsTabTestFixture.h"

// Topic: the fade of a preference group that a fold or a search filter takes out or brings back, and the height
// tween that goes with it (docs/layout/animation.md, "Fading things in and out"). Headless: the animated path is
// forced with FadeAnimateGuard and stepped by hand; the layout a frame asks for is flushed with flushLayoutForTest.

namespace {
using Category = PreferencesSettingsTab::Category;
using synth::ui::FadeVisibility;

void pickAll(PreferencesSettingsTab& tab) {
    tab.getCategoryComboForTest().setSelectedId(static_cast<int>(Category::All) + 1, juce::sendNotificationSync);
}

void step(PreferencesSettingsTab& tab, float t) {
    FadeVisibility::stepAllForTest(t);
    tab.flushLayoutForTest();
}
} // namespace

TEST_F(PreferencesSettingsTabTest, FoldingASectionFadesItsRowsWhileTheRowsBelowSlideUp) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 1000);
    pickAll(tab);
    auto* guides = findToggleByText(tab, "Show Alignment Guides");
    auto* midi = findToggleByText(tab, "MIDI badges");
    ASSERT_NE(guides, nullptr);
    ASSERT_NE(midi, nullptr);
    const int midiOpen = midi->getY();
    const int hostOpen = tab.getContentHostForTest().getHeight();
    const int headerOpen = tab.getSectionHeaderForTest(Category::Timeline).getY();

    FadeAnimateGuard guard;
    tab.setSectionCollapsed(Category::Graph, true);
    EXPECT_TRUE(guides->isVisible()) << "the rows stay on screen while they fade";
    EXPECT_TRUE(tab.anyGroupFadingForTest());
    EXPECT_FALSE(interceptsClicks(*guides)) << "a leaving row takes no clicks";
    EXPECT_EQ(midi->getY(), midiOpen) << "nothing has moved at frame 0";

    step(tab, 0.5f);
    EXPECT_LT(guides->getAlpha(), 1.0f);
    EXPECT_GT(guides->getAlpha(), 0.0f);
    const int midiMid = midi->getY();
    const int hostMid = tab.getContentHostForTest().getHeight();
    const int headerMid = tab.getSectionHeaderForTest(Category::Timeline).getY();
    EXPECT_LT(midiMid, midiOpen) << "the rows below slide up with the fade";
    EXPECT_LT(hostMid, hostOpen);
    EXPECT_LT(headerMid, headerOpen) << "so does the next section's header";
    EXPECT_GE(guides->getHeight(), 0);

    step(tab, 1.0f);
    EXPECT_FALSE(guides->isVisible());
    EXPECT_FLOAT_EQ(guides->getAlpha(), 1.0f);
    EXPECT_FALSE(tab.anyGroupFadingForTest());
    EXPECT_LT(midi->getY(), midiMid);
    EXPECT_LT(tab.getContentHostForTest().getHeight(), hostMid);
    EXPECT_LT(tab.getSectionHeaderForTest(Category::Timeline).getY(), headerMid);

    // Unfolding does the reverse: back to the open layout exactly.
    tab.setSectionCollapsed(Category::Graph, false);
    EXPECT_TRUE(guides->isVisible());
    EXPECT_FLOAT_EQ(guides->getAlpha(), 0.0f);
    step(tab, 0.5f);
    EXPECT_GT(midi->getY(), midiOpen - 1000);
    EXPECT_GT(tab.getContentHostForTest().getHeight(), 0);
    step(tab, 1.0f);
    EXPECT_TRUE(guides->isVisible());
    EXPECT_FLOAT_EQ(guides->getAlpha(), 1.0f);
    EXPECT_EQ(midi->getY(), midiOpen);
    EXPECT_EQ(tab.getContentHostForTest().getHeight(), hostOpen);
}

TEST_F(PreferencesSettingsTabTest, CollapseAllFadesEverySectionAndTheContentShrinksToTheHeaders) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 1000);
    pickAll(tab);
    const int hostOpen = tab.getContentHostForTest().getHeight();

    FadeAnimateGuard guard;
    tab.setAllSectionsCollapsed(true);
    EXPECT_TRUE(tab.anyGroupFadingForTest());
    EXPECT_EQ(tab.getContentHostForTest().getHeight(), hostOpen);
    step(tab, 0.5f);
    const int hostMid = tab.getContentHostForTest().getHeight();
    EXPECT_LT(hostMid, hostOpen);
    step(tab, 1.0f);
    const int hostFolded = tab.getContentHostForTest().getHeight();
    EXPECT_LT(hostFolded, hostMid);
    EXPECT_FALSE(tab.anyGroupFadingForTest());
}

TEST_F(PreferencesSettingsTabTest, ASearchFilterFadesTheNonMatchingRowsAndTheMatchSlidesUp) {
    PreferencesSettingsTab tab(appProperties); // opens on Graph
    tab.setSize(520, 700);
    auto* guides = findToggleByText(tab, "Show Alignment Guides");
    auto* doubleClick = findToggleByText(tab, "Double-click port to disconnect");
    ASSERT_NE(guides, nullptr);
    ASSERT_NE(doubleClick, nullptr);
    ASSERT_TRUE(doubleClick->isVisible());
    const int guidesBefore = guides->getY();

    FadeAnimateGuard guard;
    tab.setSearchFilterForTest("alignment guides");
    EXPECT_TRUE(doubleClick->isVisible()) << "a row the filter drops fades out first";
    EXPECT_FALSE(interceptsClicks(*doubleClick));
    EXPECT_EQ(guides->getY(), guidesBefore);

    step(tab, 0.5f);
    EXPECT_NEAR(doubleClick->getAlpha(), 0.5f, 0.01f);
    const int guidesMid = guides->getY();
    EXPECT_LT(guidesMid, guidesBefore);

    step(tab, 1.0f);
    EXPECT_FALSE(doubleClick->isVisible());
    EXPECT_TRUE(guides->isVisible());
    EXPECT_LT(guides->getY(), guidesMid);

    // Clearing the filter brings the row back by a fade, and the match slides down again.
    tab.setSearchFilterForTest({});
    EXPECT_TRUE(doubleClick->isVisible());
    EXPECT_FLOAT_EQ(doubleClick->getAlpha(), 0.0f);
    step(tab, 0.5f);
    EXPECT_NEAR(doubleClick->getAlpha(), 0.5f, 0.01f);
    step(tab, 1.0f);
    EXPECT_FLOAT_EQ(doubleClick->getAlpha(), 1.0f);
    EXPECT_EQ(guides->getY(), guidesBefore);
}

TEST_F(PreferencesSettingsTabTest, AFadeReversedMidWayContinuesFromWhereItIs) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 700);
    auto* doubleClick = findToggleByText(tab, "Double-click port to disconnect");
    ASSERT_NE(doubleClick, nullptr);

    FadeAnimateGuard guard;
    tab.setSearchFilterForTest("alignment guides");
    step(tab, 0.5f);
    const float mid = doubleClick->getAlpha();
    tab.setSearchFilterForTest({});
    EXPECT_FLOAT_EQ(doubleClick->getAlpha(), mid) << "the reversal starts from the current opacity";
    step(tab, 1.0f);
    EXPECT_FLOAT_EQ(doubleClick->getAlpha(), 1.0f);
    EXPECT_TRUE(doubleClick->isVisible());
}

TEST_F(PreferencesSettingsTabTest, WithAnimationsOffAFoldLandsAtOnce) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 1000);
    pickAll(tab);
    auto* guides = findToggleByText(tab, "Show Alignment Guides");
    ASSERT_NE(guides, nullptr);

    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    tab.setSectionCollapsed(Category::Graph, true);
    EXPECT_FALSE(guides->isVisible());
    EXPECT_FALSE(tab.anyGroupFadingForTest());
}
