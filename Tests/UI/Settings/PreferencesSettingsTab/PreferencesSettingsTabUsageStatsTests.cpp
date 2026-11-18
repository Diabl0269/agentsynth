#include "../../../TestWait.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "PreferencesSettingsTabTestFixture.h"
#include "Telemetry/TelemetryIdStore.h"
#include "Telemetry/TelemetryRecorder.h"
#include "Telemetry/UsageStatsChoice.h"
#include "UI/Layout/ReducedMotion.h"
#include "UserSettings.h"

// Also here: the visible plain-words line under the toggle and the usage statistics card pinned above the category
// picker (the Welcome screen's own card, answered from either place).
//
// Topic: the Privacy group -- the opt-in usage statistics toggle (off by default), the id row that exists only
// while opted in, the "What we collect" link and the Copy ID button.

namespace {
using Category = PreferencesSettingsTab::Category;

juce::TextButton* findButtonByText(PreferencesSettingsTab& tab, const juce::String& text) {
    for (auto* child : descendantsOf(tab))
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
            if (b->getButtonText() == text)
                return b;
    return nullptr;
}

// What a mouse click does, synchronously (Button::triggerClick only posts a message).
void click(juce::Button& button) {
    if (button.getClickingTogglesState())
        button.setToggleState(!button.getToggleState(), juce::dontSendNotification);
    if (button.onClick)
        button.onClick();
}

juce::Label* findIdLabel(PreferencesSettingsTab& tab) {
    for (auto* child : descendantsOf(tab))
        if (auto* l = dynamic_cast<juce::Label*>(child))
            if (l->getText().startsWith("Your usage statistics ID"))
                return l;
    return nullptr;
}

class UsageStatsTabTest : public PreferencesSettingsTabTest {
protected:
    void SetUp() override {
        PreferencesSettingsTabTest::SetUp();
        cleanFiles();
    }
    void TearDown() override {
        cleanFiles();
        PreferencesSettingsTabTest::TearDown();
    }
    static void cleanFiles() {
        synth::telemetry::TelemetryIdStore().erase();
        synth::telemetry::TelemetryRecorder::defaultQueueFile().deleteFile();
    }
    static juce::String storedId() { return synth::telemetry::TelemetryIdStore().load(); }

    std::unique_ptr<PreferencesSettingsTab> makeTab() {
        auto tab = std::make_unique<PreferencesSettingsTab>(appProperties);
        tab->setSize(500, 900);
        tab->setSelectedCategory(Category::Privacy);
        return tab;
    }

    bool settingIsOn() {
        return appProperties.getUserSettings()->getBoolValue(synth::kShareUsageStatsSettingKey, false);
    }
};
} // namespace

TEST_F(UsageStatsTabTest, IsOffByDefaultWithNoIdAndNoIdRow) {
    auto tab = makeTab();
    auto* toggle = findToggleByText(*tab, "Share anonymous usage statistics");
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->getToggleState());
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_FALSE(tab->isShareUsageStatsEnabled());
    EXPECT_FALSE(settingIsOn());
    EXPECT_TRUE(storedId().isEmpty());
    EXPECT_EQ(findIdLabel(*tab), nullptr) << "no id text while off";
    EXPECT_FALSE(findButtonByText(*tab, "Copy ID")->isVisible());
}

TEST_F(UsageStatsTabTest, TurningItOnSavesTheSettingCreatesTheIdAndShowsTheRow) {
    auto tab = makeTab();
    auto* toggle = findToggleByText(*tab, "Share anonymous usage statistics");
    ASSERT_NE(toggle, nullptr);
    click(*toggle);

    EXPECT_TRUE(settingIsOn());
    const auto id = storedId();
    ASSERT_EQ(id.length(), 36);
    auto* label = findIdLabel(*tab);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->getText(), "Your usage statistics ID: " + id);
    EXPECT_TRUE(label->isVisible());
    auto* copy = findButtonByText(*tab, "Copy ID");
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->isVisible());
}

TEST_F(UsageStatsTabTest, TurningItOffDeletesTheIdAndTheQueueAndHidesTheRow) {
    auto tab = makeTab();
    tab->setShareUsageStatsEnabled(true);
    synth::telemetry::TelemetryRecorder::defaultQueueFile().replaceWithText("{\"days\":[]}");
    ASSERT_TRUE(storedId().isNotEmpty());

    click(*findToggleByText(*tab, "Share anonymous usage statistics"));

    EXPECT_FALSE(settingIsOn());
    EXPECT_TRUE(storedId().isEmpty());
    EXPECT_FALSE(synth::telemetry::TelemetryRecorder::defaultQueueFile().existsAsFile());
    auto* copy = findButtonByText(*tab, "Copy ID");
    ASSERT_NE(copy, nullptr);
    EXPECT_FALSE(copy->isVisible());
    EXPECT_TRUE(findIdLabel(*tab) == nullptr || !findIdLabel(*tab)->isVisible());
}

TEST_F(UsageStatsTabTest, ChoiceAndIdSurviveAReload) {
    juce::String id;
    {
        auto tab = makeTab();
        tab->setShareUsageStatsEnabled(true);
        id = storedId();
    }
    auto reloaded = makeTab();
    EXPECT_TRUE(reloaded->isShareUsageStatsEnabled());
    ASSERT_NE(findIdLabel(*reloaded), nullptr);
    EXPECT_TRUE(findIdLabel(*reloaded)->getText().endsWith(id));
    EXPECT_TRUE(findButtonByText(*reloaded, "Copy ID")->isVisible());
    EXPECT_EQ(storedId(), id) << "reopening Settings never replaces the id";
}

TEST_F(UsageStatsTabTest, TheIdRowIsHiddenWhileOffEvenInTheAllView) {
    auto tab = makeTab();
    tab->setSelectedCategory(Category::All);
    EXPECT_FALSE(findButtonByText(*tab, "Copy ID")->isVisible());
    tab->setShareUsageStatsEnabled(true);
    EXPECT_TRUE(findButtonByText(*tab, "Copy ID")->isVisible());
}

TEST_F(UsageStatsTabTest, WhatWeCollectOpensThePrivacyPage) {
    auto tab = makeTab();
    juce::String opened;
    tab->setUrlOpener([&opened](const juce::URL& url) { opened = url.toString(true); });
    auto* link = findButtonByText(*tab, "What we collect");
    ASSERT_NE(link, nullptr);
    click(*link);
    EXPECT_EQ(opened, "https://agentsynth.app/privacy#telemetry");
}

TEST_F(UsageStatsTabTest, CopyIdPutsTheIdOnTheClipboard) {
    auto tab = makeTab();
    tab->setShareUsageStatsEnabled(true);
    juce::String copied;
    tab->setClipboardWriterForTest([&copied](const juce::String& text) { copied = text; });
    click(*findButtonByText(*tab, "Copy ID"));
    EXPECT_EQ(copied, storedId());
}

TEST_F(UsageStatsTabTest, EveryControlIsReachableNamedAndHasATooltip) {
    auto tab = makeTab();
    tab->setShareUsageStatsEnabled(true);
    auto* toggle = findToggleByText(*tab, "Share anonymous usage statistics");
    auto* link = findButtonByText(*tab, "What we collect");
    auto* copy = findButtonByText(*tab, "Copy ID");
    ASSERT_TRUE(toggle != nullptr && link != nullptr && copy != nullptr);
    EXPECT_EQ(toggle->getTooltip(),
              "Sends a daily summary of which features you use, with no audio, projects, prompts, names or files. Off "
              "unless you turn it on.");
    for (juce::Button* b :
         {static_cast<juce::Button*>(toggle), static_cast<juce::Button*>(link), static_cast<juce::Button*>(copy)}) {
        EXPECT_TRUE(b->getWantsKeyboardFocus()) << b->getButtonText();
        EXPECT_TRUE(b->getTooltip().isNotEmpty()) << b->getButtonText();
        EXPECT_TRUE(b->getTitle().isNotEmpty() || b->getButtonText().isNotEmpty());
    }
    auto* label = findIdLabel(*tab);
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(label->getTooltip().isNotEmpty());
    EXPECT_TRUE(label->getTitle().isNotEmpty());
}

TEST_F(UsageStatsTabTest, TheFilterFindsTheToggleFromAnyCategory) {
    auto tab = makeTab();
    tab->setSelectedCategory(Category::Graph);
    tab->setSearchFilterForTest("usage statistics");
    auto* toggle = findToggleByText(*tab, "Share anonymous usage statistics");
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isVisible());
}

// An answer given here is an answer: the Welcome screen's card never asks after it, either way.
TEST_F(UsageStatsTabTest, TurningTheToggleEitherWayMarksTheWelcomeQuestionAnswered) {
    auto tab = makeTab();
    auto* toggle = findToggleByText(*tab, "Share anonymous usage statistics");
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(appProperties.getUserSettings()->getBoolValue(synth::kUsageStatsAskedSettingKey, false));
    click(*toggle);
    EXPECT_TRUE(appProperties.getUserSettings()->getBoolValue(synth::kUsageStatsAskedSettingKey, false));
    appProperties.getUserSettings()->setValue(synth::kUsageStatsAskedSettingKey, false);
    click(*toggle);
    EXPECT_FALSE(settingIsOn());
    EXPECT_TRUE(appProperties.getUserSettings()->getBoolValue(synth::kUsageStatsAskedSettingKey, false));
}

// The id row has a fade of its own: it comes in with the toggle and leaves with it, and the group around it
// follows without a jump.
TEST_F(UsageStatsTabTest, TheIdRowFadesInAndOutWithTheToggle) {
    auto tab = makeTab();
    auto* copy = findButtonByText(*tab, "Copy ID");
    ASSERT_NE(copy, nullptr);
    ASSERT_FALSE(copy->isVisible());

    FadeAnimateGuard guard;
    tab->setShareUsageStatsEnabled(true);
    EXPECT_TRUE(copy->isVisible());
    EXPECT_FLOAT_EQ(copy->getAlpha(), 0.0f);
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    tab->flushLayoutForTest();
    EXPECT_NEAR(copy->getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    tab->flushLayoutForTest();
    EXPECT_FLOAT_EQ(copy->getAlpha(), 1.0f);
    EXPECT_GT(copy->getHeight(), 0);

    tab->setShareUsageStatsEnabled(false);
    EXPECT_TRUE(copy->isVisible()) << "stays while it fades out";
    EXPECT_FALSE(interceptsClicks(*copy));
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    tab->flushLayoutForTest();
    EXPECT_FALSE(copy->isVisible());
}

// ---- The visible line under the toggle ---------------------------------------------------------------------------

TEST_F(UsageStatsTabTest, ThePlainWordsLineIsVisibleUnderTheToggleAndTheLabelIsUnchanged) {
    auto tab = makeTab();
    auto& toggle = tab->getShareUsageStatsToggleForTest();
    auto& line = tab->getUsageStatsPlainLineForTest();
    EXPECT_EQ(toggle.getButtonText(), "Share anonymous usage statistics") << "the privacy page names this label";
    EXPECT_EQ(line.getText(), "Off unless you turn it on. Sends one anonymous summary a day of which features you use. "
                              "Never your audio, projects, prompts, names or files.");
    EXPECT_TRUE(line.isVisible());
    EXPECT_GE(line.getY(), toggle.getBottom());
    EXPECT_FALSE(toggle.getTooltip().isEmpty()) << "the tooltip stays";
}

TEST_F(UsageStatsTabTest, ThePlainLineWrapsAtNarrowWidthsWithoutCuttingOrOverlapping) {
    for (const int width : {700, 420, 300}) {
        auto tab = makeTab();
        tab->setSize(width, 900);
        auto& line = tab->getUsageStatsPlainLineForTest();
        auto* learnMore = findButtonByText(*tab, "What we collect");
        ASSERT_NE(learnMore, nullptr);
        juce::AttributedString text;
        text.append(line.getText(), line.getFont());
        juce::TextLayout layout;
        layout.createLayout(text, (float)line.getWidth());
        EXPECT_GE(line.getHeight(), (int)std::ceil(layout.getHeight())) << "wrapped text fits at width " << width;
        EXPECT_LE(line.getBottom(), learnMore->getY()) << "no overlap with the link at width " << width;
        EXPECT_LE(line.getRight(), tab->getContentHostForTest().getWidth());
    }
}

TEST_F(UsageStatsTabTest, ThePrivacyCategoryAndASearchForItStillShowTheToggleAndTheLine) {
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    EXPECT_TRUE(tab->getShareUsageStatsToggleForTest().isVisible());
    EXPECT_TRUE(tab->getUsageStatsPlainLineForTest().isVisible());
    tab->setSelectedCategory(Category::Graph);
    tab->setSearchFilterForTest("anonymous");
    EXPECT_TRUE(tab->getShareUsageStatsToggleForTest().isVisible());
    EXPECT_TRUE(tab->getUsageStatsPlainLineForTest().isVisible());
}

// ---- The card above the picker -----------------------------------------------------------------------------------

TEST_F(UsageStatsTabTest, TheCardShowsOnlyWhileUnansweredNotSharingAndAllowed) {
    {
        auto tab = makeTab();
        EXPECT_FALSE(tab->isUsageStatsCardShown()) << "not allowed (a plugin, or no owner opt-in): never shows";
        tab->setUsageStatsCardAllowed(true);
        EXPECT_TRUE(tab->isUsageStatsCardShown());
        EXPECT_TRUE(tab->getUsageStatsCardForTest().isVisible());
        EXPECT_GT(tab->getUsageStatsCardSlotHeightForTest(), 0);
    }
    appProperties.getUserSettings()->setValue(synth::kUsageStatsAskedSettingKey, true);
    {
        auto tab = makeTab();
        tab->setUsageStatsCardAllowed(true);
        EXPECT_FALSE(tab->isUsageStatsCardShown()) << "answered";
        EXPECT_FALSE(tab->getUsageStatsCardForTest().isVisible());
        EXPECT_EQ(tab->getUsageStatsCardSlotHeightForTest(), 0);
    }
    appProperties.getUserSettings()->setValue(synth::kUsageStatsAskedSettingKey, false);
    appProperties.getUserSettings()->setValue(synth::kShareUsageStatsSettingKey, true);
    {
        auto tab = makeTab();
        tab->setUsageStatsCardAllowed(true);
        EXPECT_FALSE(tab->isUsageStatsCardShown()) << "already sharing";
    }
}

TEST_F(UsageStatsTabTest, TheCardSitsAboveThePickerInEveryCategoryViewAndUnderASearch) {
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    for (auto category : {Category::All, Category::Graph, Category::Privacy}) {
        tab->setSelectedCategory(category);
        auto& card = tab->getUsageStatsCardForTest();
        EXPECT_TRUE(card.isVisible());
        EXPECT_LE(card.getBottom(), tab->getCategoryComboForTest().getY());
        EXPECT_LE(card.getBottom(), tab->getContentViewportForTest().getY());
        EXPECT_GT(card.getHeight(), 0);
    }
    tab->setSearchFilterForTest("zoom");
    EXPECT_TRUE(tab->getUsageStatsCardForTest().isVisible()) << "a filter does not hide the question";
    tab->setSearchFilterForTest("no row has this text");
    EXPECT_TRUE(tab->getUsageStatsCardForTest().isVisible());
}

TEST_F(UsageStatsTabTest, ShareOnTheCardTurnsTheToggleOnWritesBothKeysAndCreatesTheId) {
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    ASSERT_FALSE(tab->isShareUsageStatsEnabled());
    tab->getUsageStatsCardForTest().getShareButtonForTest().triggerClick();
    synth::test::waitUntil([&] { return settingIsOn(); });
    EXPECT_TRUE(settingIsOn());
    EXPECT_TRUE(appProperties.getUserSettings()->getBoolValue(synth::kUsageStatsAskedSettingKey, false));
    EXPECT_TRUE(tab->isShareUsageStatsEnabled()) << "the toggle below followed the card";
    EXPECT_EQ(storedId().length(), 36);
    ASSERT_NE(findIdLabel(*tab), nullptr);
    synth::test::waitUntil([&] { return !tab->isUsageStatsCardShown(); });
    EXPECT_FALSE(tab->isUsageStatsCardShown());
}

TEST_F(UsageStatsTabTest, NoThanksOnTheCardLeavesTheToggleOffWritesAskedOnlyAndHidesTheCard) {
    synth::ui::setReducedMotionForTest(true);
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    tab->getUsageStatsCardForTest().getNoThanksButtonForTest().triggerClick();
    synth::test::waitUntil([&] { return !tab->isUsageStatsCardShown(); });
    EXPECT_FALSE(tab->isUsageStatsCardShown());
    EXPECT_EQ(tab->getUsageStatsCardSlotHeightForTest(), 0) << "Reduced Motion: no collapse frames";
    EXPECT_FALSE(tab->isShareUsageStatsEnabled());
    EXPECT_FALSE(settingIsOn());
    EXPECT_TRUE(appProperties.getUserSettings()->getBoolValue(synth::kUsageStatsAskedSettingKey, false));
    EXPECT_TRUE(storedId().isEmpty());
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST_F(UsageStatsTabTest, TurningTheToggleAnswersTheCardToo) {
    synth::ui::setReducedMotionForTest(true);
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    ASSERT_TRUE(tab->isUsageStatsCardShown());
    click(tab->getShareUsageStatsToggleForTest());
    EXPECT_FALSE(tab->isUsageStatsCardShown());
    EXPECT_TRUE(settingIsOn());
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST_F(UsageStatsTabTest, AnAnswerGivenElsewhereRemovesTheCardAndFollowsTheToggle) {
    synth::ui::setReducedMotionForTest(true);
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    ASSERT_TRUE(tab->isUsageStatsCardShown());
    // What the Welcome screen's card does (the one writer); the settings file broadcasts it.
    synth::telemetry::applyShareUsageStatsChoice(*appProperties.getUserSettings(), true);
    synth::test::waitUntil([&] { return !tab->isUsageStatsCardShown(); });
    EXPECT_FALSE(tab->isUsageStatsCardShown());
    EXPECT_TRUE(tab->isShareUsageStatsEnabled());
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST_F(UsageStatsTabTest, WhatWeCollectOnTheCardIsNotAnAnswerAndOpensThePrivacyPage) {
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    juce::String opened;
    tab->setUrlOpener([&opened](const juce::URL& url) { opened = url.toString(true); });
    tab->getUsageStatsCardForTest().getLearnMoreButtonForTest().triggerClick();
    synth::test::waitUntil([&] { return opened.isNotEmpty(); });
    EXPECT_EQ(opened, "https://agentsynth.app/privacy#telemetry");
    EXPECT_TRUE(tab->isUsageStatsCardShown());
    EXPECT_FALSE(appProperties.getUserSettings()->getBoolValue(synth::kUsageStatsAskedSettingKey, false));
}

// Keyboard: the card's buttons are Tab stops with names, and the card is first in the tab's Tab order.
TEST_F(UsageStatsTabTest, TheCardIsFirstInTheTabOrderAndItsButtonsAreNamedTabStops) {
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    auto& card = tab->getUsageStatsCardForTest();
    EXPECT_EQ(tab->getIndexOfChildComponent(&card), 0) << "the question is the first Tab stop in the tab";
    for (juce::Button* b :
         {&card.getShareButtonForTest(), &card.getNoThanksButtonForTest(), &card.getLearnMoreButtonForTest()}) {
        EXPECT_TRUE(b->getWantsKeyboardFocus());
        EXPECT_TRUE(b->getTitle().isNotEmpty());
        EXPECT_TRUE(b->getTooltip().isNotEmpty());
    }
    EXPECT_EQ(card.getTitle(), "Help make Agent Synth better");
}

TEST_F(UsageStatsTabTest, TheCardShareAndNoThanksKeepEqualSizeHere) {
    auto tab = makeTab();
    tab->setUsageStatsCardAllowed(true);
    auto& card = tab->getUsageStatsCardForTest();
    EXPECT_EQ(card.getShareButtonForTest().getWidth(), card.getNoThanksButtonForTest().getWidth());
    EXPECT_EQ(card.getShareButtonForTest().getHeight(), card.getNoThanksButtonForTest().getHeight());
}
