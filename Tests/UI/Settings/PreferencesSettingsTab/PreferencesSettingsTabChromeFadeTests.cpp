#include "../../Layout/MotionPolish/MotionStep.h"
#include "PreferencesSettingsTabTestFixture.h"

// Topic: the Preferences All view's section headers and fold-all strip fade when the view shows or hides them, the
// strip's height following the fade, and a header's chevron turns (docs/layout/animation.md#fading-things-in-and-out).
// Headless: the animated path is forced with FadeAnimateGuard and stepped by hand; the layout a frame asks for is
// flushed with flushLayoutForTest.

namespace {
using Category = PreferencesSettingsTab::Category;
using synth::ui::AnimationMode;
using synth::ui::FoldAllButton;

void pick(PreferencesSettingsTab& tab, Category category) {
    tab.getCategoryComboForTest().setSelectedId(static_cast<int>(category) + 1, juce::sendNotificationSync);
}

void step(PreferencesSettingsTab& tab, float t) {
    stepMotion(t);
    tab.flushLayoutForTest();
}
} // namespace

TEST_F(PreferencesSettingsTabTest, LeavingTheAllViewFadesTheHeadersAndTheStripAndTheRowsBelowSlide) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 1000);
    pick(tab, Category::All);
    auto& header = tab.getSectionHeaderForTest(Category::Graph);
    auto& strip = tab.getFoldAllButtonForTest();
    ASSERT_TRUE(header.isVisible());
    ASSERT_TRUE(strip.isVisible());
    EXPECT_EQ(strip.getHeight(), FoldAllButton::kStripHeight);

    FadeAnimateGuard guard;
    pick(tab, Category::Graph);
    EXPECT_TRUE(header.isVisible()) << "the header stays on screen while it fades";
    EXPECT_TRUE(strip.isVisible());
    EXPECT_TRUE(tab.areSectionHeadersFadingForTest());
    EXPECT_TRUE(tab.isFoldAllFadingForTest());
    EXPECT_FALSE(interceptsClicks(header)) << "a leaving header takes no clicks";
    EXPECT_EQ(strip.getHeight(), FoldAllButton::kStripHeight) << "nothing has moved at frame 0";

    step(tab, 0.5f);
    EXPECT_GT(header.getAlpha(), 0.0f);
    EXPECT_LT(header.getAlpha(), 1.0f);
    EXPECT_GT(strip.getAlpha(), 0.0f);
    EXPECT_LT(strip.getAlpha(), 1.0f);
    EXPECT_GT(strip.getHeight(), 0);
    EXPECT_LT(strip.getHeight(), FoldAllButton::kStripHeight) << "the strip's height follows the fade";

    step(tab, 1.0f);
    EXPECT_FALSE(header.isVisible());
    EXPECT_FALSE(strip.isVisible());
    EXPECT_FLOAT_EQ(header.getAlpha(), 1.0f);
    EXPECT_FALSE(tab.areSectionHeadersFadingForTest());

    // Coming back fades them in from nothing.
    pick(tab, Category::All);
    EXPECT_TRUE(header.isVisible());
    EXPECT_FLOAT_EQ(header.getAlpha(), 0.0f);
    EXPECT_FLOAT_EQ(strip.getAlpha(), 0.0f);
    step(tab, 0.5f);
    EXPECT_GT(strip.getHeight(), 0);
    EXPECT_LT(strip.getHeight(), FoldAllButton::kStripHeight);
    step(tab, 1.0f);
    EXPECT_FLOAT_EQ(header.getAlpha(), 1.0f);
    EXPECT_EQ(strip.getHeight(), FoldAllButton::kStripHeight);
}

TEST_F(PreferencesSettingsTabTest, TheAllViewsChromeReduceMotionFadesAndOffAndOffScreenAreInstant) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 1000);
    pick(tab, Category::All);
    auto& header = tab.getSectionHeaderForTest(Category::Graph);
    pick(tab, Category::Graph); // not on screen and not forced
    EXPECT_FALSE(header.isVisible());
    EXPECT_FALSE(tab.areSectionHeadersFadingForTest());
    pick(tab, Category::All);
    EXPECT_TRUE(header.isVisible());
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        pick(tab, Category::Graph);
        EXPECT_TRUE(tab.areSectionHeadersFadingForTest()) << "Reduce Motion is a short fade";
        step(tab, 1.0f);
        EXPECT_FALSE(header.isVisible());
        pick(tab, Category::All);
        step(tab, 1.0f);
    }
    FadeAnimateGuard off(AnimationMode::off);
    pick(tab, Category::Graph);
    EXPECT_FALSE(tab.areSectionHeadersFadingForTest());
    EXPECT_FALSE(header.isVisible());
}

TEST_F(PreferencesSettingsTabTest, AFoldTurnsTheSectionChevronAndReduceMotionFlipsIt) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(520, 1000);
    pick(tab, Category::All);
    {
        FadeAnimateGuard guard;
        EXPECT_FLOAT_EQ(tab.getSectionChevronOpennessForTest(Category::Graph), 1.0f);
        tab.setSectionCollapsed(Category::Graph, true);
        EXPECT_TRUE(tab.isSectionChevronTurningForTest(Category::Graph));
        EXPECT_FLOAT_EQ(tab.getSectionChevronOpennessForTest(Category::Graph), 1.0f);
        step(tab, 0.5f);
        const float mid = tab.getSectionChevronOpennessForTest(Category::Graph);
        EXPECT_GT(mid, 0.0f);
        EXPECT_LT(mid, 1.0f);
        tab.setSectionCollapsed(Category::Graph, false);
        EXPECT_NEAR(tab.getSectionChevronOpennessForTest(Category::Graph), mid, 1.0e-4f)
            << "a reversal turns back from where the arrow is";
        step(tab, 1.0f);
        EXPECT_FLOAT_EQ(tab.getSectionChevronOpennessForTest(Category::Graph), 1.0f);
    }
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        tab.setSectionCollapsed(Category::Graph, true);
        EXPECT_FALSE(tab.isSectionChevronTurningForTest(Category::Graph));
        EXPECT_FLOAT_EQ(tab.getSectionChevronOpennessForTest(Category::Graph), 0.0f);
        step(tab, 1.0f);
    }
    tab.setSectionCollapsed(Category::Graph, false); // not on screen and not forced
    EXPECT_FALSE(tab.isSectionChevronTurningForTest(Category::Graph));
    EXPECT_FLOAT_EQ(tab.getSectionChevronOpennessForTest(Category::Graph), 1.0f);
}
