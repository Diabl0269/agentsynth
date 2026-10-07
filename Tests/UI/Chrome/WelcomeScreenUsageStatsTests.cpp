#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/AppCommands.h"
#include "Telemetry/TelemetryIdStore.h"
#include "Telemetry/TelemetryRecorder.h"
#include "Telemetry/TelemetryService.h"
#include "Telemetry/UsageStatsChoice.h"
#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UserSettings.h"

// Topic: the Welcome screen's one-time usage statistics card: when it shows, what each button writes and
// does to the running service, that it never returns after an answer, that the card height tracks the panel, that
// "What we collect" goes through the URL seam, and that Reduced Motion lays out at once.

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

struct Stored {
    bool share = false;
    bool asked = false;
};

Stored readStored() {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    auto* s = props.getUserSettings();
    return {s->getBoolValue(synth::kShareUsageStatsSettingKey, false),
            s->getBoolValue(synth::kUsageStatsAskedSettingKey, false)};
}

void writeStored(bool share, bool asked) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    props.getUserSettings()->setValue(synth::kShareUsageStatsSettingKey, share);
    props.getUserSettings()->setValue(synth::kUsageStatsAskedSettingKey, asked);
    props.getUserSettings()->saveIfNeeded();
}

class WelcomeUsageStatsTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        keyGuard = std::make_unique<synth::test::PersistedKeysGuard>(
            juce::StringArray{synth::kShareUsageStatsSettingKey, synth::kUsageStatsAskedSettingKey});
        cleanFiles();
        writeStored(false, false);
    }
    void TearDown() override {
        synth::ui::setReducedMotionForTest(std::nullopt);
        keyGuard.reset();
        cleanFiles();
        MainComponentTest::TearDown();
    }
    static void cleanFiles() {
        synth::telemetry::TelemetryIdStore().erase();
        synth::telemetry::TelemetryRecorder::defaultQueueFile().deleteFile();
    }
    std::unique_ptr<synth::test::PersistedKeysGuard> keyGuard;
};

} // namespace

TEST_F(WelcomeUsageStatsTest, CardShowsOnlyWhileUnansweredAndNotSharing) {
    {
        MainComponent mc(std::make_unique<MockProvider>());
        EXPECT_TRUE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
        EXPECT_TRUE(mc.getWelcomeScreenForTest()->getUsageStatsPromptForTest().isVisible());
    }
    writeStored(false, true);
    {
        MainComponent mc(std::make_unique<MockProvider>());
        EXPECT_FALSE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
        EXPECT_FALSE(mc.getWelcomeScreenForTest()->getUsageStatsPromptForTest().isVisible());
    }
    writeStored(true, false); // an existing opt-in is never asked again
    {
        MainComponent mc(std::make_unique<MockProvider>());
        EXPECT_FALSE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
    }
}

TEST_F(WelcomeUsageStatsTest, ShareWritesBothKeysCreatesTheIdAndTurnsTheRunningServiceOn) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto* service = mc.getTelemetryServiceForTest();
    ASSERT_NE(service, nullptr);
    ASSERT_FALSE(service->getRecorder().isEnabled());

    mc.getWelcomeScreenForTest()->getUsageStatsPromptForTest().getShareButtonForTest().triggerClick();
    pump(); // the click is posted; the settings change it causes is posted again

    const auto stored = readStored();
    EXPECT_TRUE(stored.share);
    EXPECT_TRUE(stored.asked);
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isNotEmpty());
    pump();
    EXPECT_TRUE(service->getRecorder().isEnabled()) << "the settings change reached TelemetryService";
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
}

TEST_F(WelcomeUsageStatsTest, NoThanksWritesAskedOnlyAndLeavesNoId) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.getWelcomeScreenForTest()->getUsageStatsPromptForTest().getNoThanksButtonForTest().triggerClick();
    pump();
    pump();
    const auto stored = readStored();
    EXPECT_FALSE(stored.share);
    EXPECT_TRUE(stored.asked);
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isEmpty());
    EXPECT_FALSE(mc.getTelemetryServiceForTest()->getRecorder().isEnabled());
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
}

TEST_F(WelcomeUsageStatsTest, TheCardDoesNotReturnAfterAnAnswer) {
    {
        MainComponent mc(std::make_unique<MockProvider>());
        mc.getWelcomeScreenForTest()->getUsageStatsPromptForTest().getNoThanksButtonForTest().triggerClick();
        pump();
    }
    MainComponent again(std::make_unique<MockProvider>());
    EXPECT_FALSE(again.getWelcomeScreenForTest()->isUsageStatsPromptShown());
    ASSERT_TRUE(again.getCommandManager().invokeDirectly(AppCommands::showWelcomeScreen, false));
    EXPECT_FALSE(again.getWelcomeScreenForTest()->isUsageStatsPromptShown());
}

TEST_F(WelcomeUsageStatsTest, AnAnswerInPreferencesClosesTheCardToo) {
    MainComponent mc(std::make_unique<MockProvider>());
    ASSERT_TRUE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
    synth::telemetry::applyShareUsageStatsChoice(*mc.getAppPropertiesForTest().getUserSettings(), true);
    pump();
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown());
}

TEST_F(WelcomeUsageStatsTest, SharedChoiceFunctionSetsAskedForBothAnswers) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    synth::telemetry::applyShareUsageStatsChoice(*props.getUserSettings(), true);
    EXPECT_TRUE(readStored().share);
    EXPECT_TRUE(readStored().asked);
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isNotEmpty());
    synth::telemetry::applyShareUsageStatsChoice(*props.getUserSettings(), false);
    EXPECT_FALSE(readStored().share);
    EXPECT_TRUE(readStored().asked);
    EXPECT_TRUE(synth::telemetry::TelemetryIdStore().load().isEmpty());
    EXPECT_FALSE(synth::telemetry::shouldAskAboutUsageStats(props.getUserSettings()));
}

TEST_F(WelcomeUsageStatsTest, WhatWeCollectOpensThePrivacySectionThroughTheUrlSeam) {
    MainComponent mc(std::make_unique<MockProvider>());
    std::vector<juce::String> opened;
    mc.setUrlOpenerForTest([&opened](const juce::URL& u) { opened.push_back(u.toString(true)); });
    mc.getWelcomeScreenForTest()->getUsageStatsPromptForTest().getLearnMoreButtonForTest().triggerClick();
    pump();
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], "https://agentsynth.app/privacy#telemetry");
    EXPECT_TRUE(mc.getWelcomeScreenForTest()->isUsageStatsPromptShown()) << "reading about it is not an answer";
}

TEST_F(WelcomeUsageStatsTest, TheOtherStartButtonsStayUsableWhileTheCardIsUp) {
    synth::ui::WelcomeScreenComponent ws;
    ws.setSize(1600, 900);
    ws.setUsageStatsPromptShown(true);
    int fired = 0;
    ws.onNewProject = [&fired] { ++fired; };
    ws.getNewProjectButtonForTest().triggerClick();
    pump();
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(ws.isUsageStatsPromptShown());
}

TEST_F(WelcomeUsageStatsTest, TheCardHeightShrinksByExactlyThePanelAndItsGap) {
    synth::ui::WelcomeScreenComponent ws;
    ws.setSize(1600, 900);
    const int without = ws.getCardHeightForTest();
    ws.setUsageStatsPromptShown(true);
    const int with = ws.getCardHeightForTest();
    EXPECT_EQ(with - without, ws.getUsageStatsSlotHeightForTest());
    EXPECT_EQ(with - without, synth::ui::UsageStatsPromptComponent::kHeight + 14);

    auto& panel = ws.getUsageStatsPromptForTest();
    EXPECT_EQ(panel.getHeight(), synth::ui::UsageStatsPromptComponent::kHeight);
    // The panel sits between the subtitle and the start buttons, with the gap in between.
    EXPECT_EQ(ws.getNewProjectButtonForTest().getY() - panel.getBottom(), 14);
    const int cardTopWith = 450 - with / 2;
    const int footerBottomWith = ws.getWhatsNewButtonForTest().getBottom();
    EXPECT_EQ(footerBottomWith, cardTopWith + with - 20) << "the layout sums agree: no stray space below the footer";

    ws.setUsageStatsPromptShown(false); // not on screen: lands at once
    EXPECT_EQ(ws.getCardHeightForTest(), without);
    EXPECT_FALSE(panel.isVisible());
    EXPECT_EQ(ws.getUsageStatsSlotHeightForTest(), 0);
    const int cardTopWithout = 450 - without / 2;
    EXPECT_EQ(ws.getWhatsNewButtonForTest().getBottom(), cardTopWithout + without - 20);
}

TEST_F(WelcomeUsageStatsTest, HeadlessAndReducedMotionLayOutAtOnceAndNeverReopen) {
    synth::ui::setReducedMotionForTest(true);
    synth::ui::WelcomeScreenComponent ws;
    ws.setSize(1600, 900);
    ws.setUsageStatsPromptShown(true);
    std::vector<bool> choices;
    ws.onUsageStatsChoice = [&choices](bool share) { choices.push_back(share); };
    const int open = ws.getCardHeightForTest();

    ws.getUsageStatsPromptForTest().getShareButtonForTest().triggerClick();
    pump();

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_TRUE(choices[0]);
    EXPECT_EQ(ws.getUsageStatsSlotHeightForTest(), 0) << "no collapse frames, no thanks line";
    EXPECT_EQ(ws.getCardHeightForTest(), open - (synth::ui::UsageStatsPromptComponent::kHeight + 14));
    EXPECT_FALSE(ws.getUsageStatsPromptForTest().isVisible());
    ws.setUsageStatsPromptShown(true);
    EXPECT_EQ(ws.getUsageStatsSlotHeightForTest(), 0) << "an answered card stays answered";
}

TEST_F(WelcomeUsageStatsTest, ShareAndNoThanksAreEqualAndEveryControlIsNamedAndTooltipped) {
    synth::ui::WelcomeScreenComponent ws;
    ws.setSize(1600, 900);
    ws.setUsageStatsPromptShown(true);
    auto& p = ws.getUsageStatsPromptForTest();
    EXPECT_EQ(p.getShareButtonForTest().getWidth(), p.getNoThanksButtonForTest().getWidth());
    EXPECT_EQ(p.getShareButtonForTest().getHeight(), p.getNoThanksButtonForTest().getHeight());
    EXPECT_EQ(p.getShareButtonForTest().getY(), p.getNoThanksButtonForTest().getY());
    EXPECT_EQ(p.getShareButtonForTest().getTooltip(), "Send a daily anonymous summary of feature use");
    EXPECT_EQ(p.getNoThanksButtonForTest().getTooltip(), "Don't share usage statistics");
    EXPECT_EQ(p.getLearnMoreButtonForTest().getTooltip(), "Open the privacy policy section on usage statistics");
    for (auto* b : {&p.getShareButtonForTest(), &p.getNoThanksButtonForTest(), &p.getLearnMoreButtonForTest()}) {
        EXPECT_TRUE(b->getTitle().isNotEmpty());
        EXPECT_TRUE(b->getWantsKeyboardFocus());
    }
    EXPECT_EQ(p.getTitle(), "Help make Agent Synth better");
    EXPECT_TRUE(p.getDescription().startsWith("Share anonymous usage statistics"));
}

TEST_F(WelcomeUsageStatsTest, TabReachesShareNoThanksAndTheLinkBeforeTheStartButtons) {
    synth::ui::WelcomeScreenComponent ws;
    ws.setSize(1600, 900);
    ws.setUsageStatsPromptShown(true);
    auto& p = ws.getUsageStatsPromptForTest();
    const auto order = ws.createKeyboardFocusTraverser()->getAllComponents(&ws);
    auto indexOf = [&order](juce::Component* c) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == c)
                return (int)i;
        return -1;
    };
    const int share = indexOf(&p.getShareButtonForTest());
    const int noThanks = indexOf(&p.getNoThanksButtonForTest());
    const int link = indexOf(&p.getLearnMoreButtonForTest());
    const int newProject = indexOf(&ws.getNewProjectButtonForTest());
    ASSERT_GE(share, 0);
    ASSERT_GE(noThanks, 0);
    ASSERT_GE(link, 0);
    ASSERT_GE(newProject, 0);
    EXPECT_LT(share, noThanks);
    EXPECT_LT(noThanks, link);
    EXPECT_LT(link, newProject);
}
