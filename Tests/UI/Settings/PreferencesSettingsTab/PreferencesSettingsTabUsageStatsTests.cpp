#include "PreferencesSettingsTabTestFixture.h"
#include "Telemetry/TelemetryIdStore.h"
#include "Telemetry/TelemetryRecorder.h"
#include "UserSettings.h"

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
    click(*findButtonByText(*tab, "Copy ID"));
    EXPECT_EQ(juce::SystemClipboard::getTextFromClipboard(), storedId());
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
