#include "../Layout/FadeVisibilityTestGuard.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Settings/ShortcutsSettingsTab.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the Keyboard Shortcuts tab folds a section (and filters rows) with the same fade and height tween as the
// Preferences groups (docs/layout/animation.md, "Fading things in and out"). Headless: the animated path is forced with
// FadeAnimateGuard and stepped by hand.

namespace {
using synth::ui::FadeVisibility;

class ShortcutFoldMotionTest : public ::testing::Test {
protected:
    void SetUp() override {
        tab = std::make_unique<ShortcutsSettingsTab>(manager);
        tab->setSize(600, 900);
        const auto& ids = manager.getActionIds();
        for (int i = 0; i < (int)ids.size(); ++i) {
            const auto category = ShortcutManager::getCategory(ids[i]);
            if (category == ShortcutCategory::General && firstGeneral < 0)
                firstGeneral = i;
            if (category == ShortcutCategory::Graph && firstGraph < 0)
                firstGraph = i;
        }
    }

    static void step(float t) { FadeVisibility::stepAllForTest(t); }

    ShortcutManager manager;
    std::unique_ptr<ShortcutsSettingsTab> tab;
    int firstGeneral = -1;
    int firstGraph = -1;
};
} // namespace

TEST_F(ShortcutFoldMotionTest, FoldingASectionFadesItsRowsWhileTheRowsBelowSlideUp) {
    ASSERT_GE(firstGeneral, 0);
    ASSERT_GE(firstGraph, 0);
    auto& generalRow = tab->getRowButtonForTest(firstGeneral);
    auto& graphRow = tab->getRowButtonForTest(firstGraph);
    const int graphOpen = graphRow.getY();
    const int heightOpen = tab->getContentHeightForTest();

    FadeAnimateGuard guard;
    tab->setSectionCollapsed(ShortcutCategory::General, true);
    EXPECT_TRUE(generalRow.isVisible()) << "the rows stay on screen while they fade";
    EXPECT_TRUE(tab->anyFadeRunningForTest());
    EXPECT_FALSE(interceptsClicks(generalRow)) << "a leaving row takes no clicks";
    EXPECT_EQ(graphRow.getY(), graphOpen) << "nothing has moved at frame 0";

    step(0.5f);
    EXPECT_GT(generalRow.getAlpha(), 0.0f);
    EXPECT_LT(generalRow.getAlpha(), 1.0f);
    const int graphMid = graphRow.getY();
    EXPECT_LT(graphMid, graphOpen) << "the rows below slide up with the fade";
    EXPECT_LT(tab->getContentHeightForTest(), heightOpen);

    step(1.0f);
    EXPECT_FALSE(generalRow.isVisible());
    EXPECT_FLOAT_EQ(generalRow.getAlpha(), 1.0f);
    EXPECT_FALSE(tab->anyFadeRunningForTest());
    EXPECT_LT(graphRow.getY(), graphMid);

    // Unfolding does the reverse, back to the open layout exactly.
    tab->setSectionCollapsed(ShortcutCategory::General, false);
    EXPECT_TRUE(generalRow.isVisible());
    EXPECT_FLOAT_EQ(generalRow.getAlpha(), 0.0f);
    step(0.5f);
    EXPECT_LT(graphRow.getY(), graphOpen);
    step(1.0f);
    EXPECT_FLOAT_EQ(generalRow.getAlpha(), 1.0f);
    EXPECT_EQ(graphRow.getY(), graphOpen);
    EXPECT_EQ(tab->getContentHeightForTest(), heightOpen);
}

TEST_F(ShortcutFoldMotionTest, CollapseAllFadesEverySectionAndTheContentShrinksToTheHeaders) {
    const int heightOpen = tab->getContentHeightForTest();
    FadeAnimateGuard guard;
    tab->setAllSectionsCollapsed(true);
    EXPECT_TRUE(tab->anyFadeRunningForTest());
    EXPECT_EQ(tab->getContentHeightForTest(), heightOpen);
    step(0.5f);
    const int heightMid = tab->getContentHeightForTest();
    EXPECT_LT(heightMid, heightOpen);
    step(1.0f);
    EXPECT_LT(tab->getContentHeightForTest(), heightMid);
    EXPECT_FALSE(tab->anyFadeRunningForTest());
}

TEST_F(ShortcutFoldMotionTest, ReduceMotionIsAShortFadeAndAnimationsOffIsInstant) {
    auto& row = tab->getRowButtonForTest(firstGeneral);
    {
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        tab->setSectionCollapsed(ShortcutCategory::General, true);
        EXPECT_TRUE(row.isVisible()) << "still a fade, only shorter";
        step(1.0f);
        EXPECT_FALSE(row.isVisible());
        tab->setSectionCollapsed(ShortcutCategory::General, false);
        step(1.0f);
    }
    FadeAnimateGuard off(synth::ui::AnimationMode::off);
    tab->setSectionCollapsed(ShortcutCategory::General, true);
    EXPECT_FALSE(row.isVisible());
    EXPECT_FALSE(tab->anyFadeRunningForTest());
}

TEST_F(ShortcutFoldMotionTest, OffScreenFoldsLandAtOnce) {
    auto& row = tab->getRowButtonForTest(firstGeneral);
    tab->setSectionCollapsed(ShortcutCategory::General, true);
    EXPECT_FALSE(row.isVisible());
    EXPECT_FALSE(tab->anyFadeRunningForTest());
}

TEST_F(ShortcutFoldMotionTest, TheSearchFilterFadesRowsOutAndTheyStayReachableAfterwards) {
    FadeAnimateGuard guard;
    auto& other = tab->getRowButtonForTest(firstGraph);
    tab->setSearchText(tab->getRowDescription(firstGeneral));
    EXPECT_TRUE(other.isVisible()) << "a filtered-out row fades before it goes";
    EXPECT_TRUE(tab->anyFadeRunningForTest());
    EXPECT_FALSE(tab->isRowVisible(firstGraph)) << "logically gone from the first frame";
    step(1.0f);
    EXPECT_FALSE(other.isVisible());
    EXPECT_TRUE(tab->getRowButtonForTest(firstGeneral).isVisible());

    tab->setSearchText({});
    step(1.0f);
    EXPECT_TRUE(other.isVisible());
    EXPECT_FLOAT_EQ(other.getAlpha(), 1.0f);
}
