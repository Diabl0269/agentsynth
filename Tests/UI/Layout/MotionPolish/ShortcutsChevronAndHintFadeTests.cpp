#include "MotionStep.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Settings/ShortcutsSettingsTab.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the Keyboard Shortcuts tab's section chevrons turn and its "No matching shortcuts" line fades
// (docs/layout/animation.md#fading-things-in-and-out). Headless: the animated path is forced with FadeAnimateGuard
// and stepped by hand.

namespace {
using synth::ui::AnimationMode;

class ShortcutChevronAndHintTest : public ::testing::Test {
protected:
    void SetUp() override {
        tab = std::make_unique<ShortcutsSettingsTab>(manager);
        tab->setSize(600, 900);
    }
    ShortcutManager manager;
    std::unique_ptr<ShortcutsSettingsTab> tab;
};
} // namespace

TEST_F(ShortcutChevronAndHintTest, AFoldTurnsTheChevronOverTimeAndAReversalStartsFromWhereItIs) {
    FadeAnimateGuard guard;
    EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::General), 1.0f);

    tab->setSectionCollapsed(ShortcutCategory::General, true);
    EXPECT_TRUE(tab->isChevronTurningForTest(ShortcutCategory::General));
    EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::General), 1.0f) << "nothing has moved at frame 0";
    stepMotion(0.5f);
    const float mid = tab->getChevronOpennessForTest(ShortcutCategory::General);
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, 1.0f);

    tab->setSectionCollapsed(ShortcutCategory::General, false);
    EXPECT_NEAR(tab->getChevronOpennessForTest(ShortcutCategory::General), mid, 1.0e-4f);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::General), 1.0f);
    EXPECT_FALSE(tab->isChevronTurningForTest(ShortcutCategory::General));

    tab->setSectionCollapsed(ShortcutCategory::General, true);
    stepMotion(1.0f);
    EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::General), 0.0f);
}

TEST_F(ShortcutChevronAndHintTest, ReduceMotionAnimationsOffAndOffScreenTurnTheChevronAtOnce) {
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        tab->setSectionCollapsed(ShortcutCategory::General, true);
        EXPECT_FALSE(tab->isChevronTurningForTest(ShortcutCategory::General));
        EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::General), 0.0f);
        tab->setSectionCollapsed(ShortcutCategory::General, false);
        stepMotion(1.0f);
    }
    {
        FadeAnimateGuard off(AnimationMode::off);
        tab->setSectionCollapsed(ShortcutCategory::General, true);
        EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::General), 0.0f);
        tab->setSectionCollapsed(ShortcutCategory::General, false);
    }
    tab->setSectionCollapsed(ShortcutCategory::Graph, true); // not on screen and not forced
    EXPECT_FALSE(tab->isChevronTurningForTest(ShortcutCategory::Graph));
    EXPECT_FLOAT_EQ(tab->getChevronOpennessForTest(ShortcutCategory::Graph), 0.0f);
}

TEST_F(ShortcutChevronAndHintTest, ASectionRestoredAsFoldedStartsFoldedWithoutTurning) {
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "ShortcutChevronRestoreTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);
    props.getUserSettings()->clear();
    {
        FadeAnimateGuard guard;
        ShortcutsSettingsTab first(manager, &props);
        first.setSize(600, 900);
        first.setSectionCollapsed(ShortcutCategory::Graph, true);
        stepMotion(1.0f);
    }
    FadeAnimateGuard guard;
    ShortcutsSettingsTab reopened(manager, &props);
    reopened.setSize(600, 900);
    EXPECT_FLOAT_EQ(reopened.getChevronOpennessForTest(ShortcutCategory::Graph), 0.0f);
    EXPECT_FALSE(reopened.isChevronTurningForTest(ShortcutCategory::Graph))
        << "what was there from the start does not turn";
    props.getUserSettings()->clear();
}

TEST_F(ShortcutChevronAndHintTest, NoMatchingShortcutsFadesInUnderTheLeavingRowsAndOutWhenTheSearchClears) {
    FadeAnimateGuard guard;
    auto& hint = tab->getNoMatchHintForTest();
    EXPECT_FALSE(hint.isVisible());
    const int heightFull = tab->getContentHeightForTest();

    tab->setSearchText("zzzqqq");
    EXPECT_TRUE(hint.isVisible());
    EXPECT_FLOAT_EQ(hint.getAlpha(), 0.0f) << "frame 0 is the first fade frame";
    stepMotion(0.5f);
    EXPECT_GT(hint.getAlpha(), 0.0f);
    EXPECT_LT(hint.getAlpha(), 1.0f);
    stepMotion(1.0f);
    EXPECT_TRUE(hint.isVisible());
    EXPECT_FLOAT_EQ(hint.getAlpha(), 1.0f);
    EXPECT_FALSE(tab->anyFadeRunningForTest());
    const int heightEmpty = tab->getContentHeightForTest();
    EXPECT_LT(heightEmpty, heightFull);
    EXPECT_GT(heightEmpty, 1) << "the line takes a row of room";

    tab->setSearchText({});
    EXPECT_TRUE(hint.isVisible()) << "it stays on screen while it fades out";
    stepMotion(0.5f);
    EXPECT_LT(hint.getAlpha(), 1.0f);
    EXPECT_GT(hint.getAlpha(), 0.0f);
    stepMotion(1.0f);
    EXPECT_FALSE(hint.isVisible());
    EXPECT_FLOAT_EQ(hint.getAlpha(), 1.0f);
    EXPECT_FALSE(tab->anyFadeRunningForTest());
}

TEST_F(ShortcutChevronAndHintTest, TheNoMatchLineReduceMotionFadesAndOffAndOffScreenAreInstant) {
    auto& hint = tab->getNoMatchHintForTest();
    tab->setSearchText("zzzqqq"); // not on screen and not forced
    EXPECT_TRUE(hint.isVisible());
    EXPECT_FALSE(tab->anyFadeRunningForTest());
    tab->setSearchText({});
    EXPECT_FALSE(hint.isVisible());
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        tab->setSearchText("zzzqqq");
        EXPECT_TRUE(tab->getNoMatchFadeForTest().isFading());
        stepMotion(1.0f);
        tab->setSearchText({});
        stepMotion(1.0f);
    }
    FadeAnimateGuard off(AnimationMode::off);
    tab->setSearchText("zzzqqq");
    EXPECT_FALSE(tab->anyFadeRunningForTest());
    EXPECT_TRUE(hint.isVisible());
    EXPECT_FLOAT_EQ(hint.getAlpha(), 1.0f);
}
