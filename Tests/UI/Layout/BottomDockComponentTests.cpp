// BottomDockComponentTests.cpp -- FRO11 (P9-5): the dock's tab strip, the Toggle Mixer Panel
// shortcut/command, and active-tab persistence via ApplicationProperties.
#include "AI/AIProvider.h"
#include "BottomDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManager/AppCommands.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMDCT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMDCT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

TEST(BottomDockComponentTests, TabStripSwitchesBetweenTimelineAndMixerWithoutClosingTheDock) {
    // Isolates "bottomDockActiveTab" on the shared on-disk settings file -- see the guard's own
    // comment; this test asserts the "Timeline" DEFAULT, which an earlier test's Mixer-tab switch
    // (persisted to the same file) would otherwise clobber.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();

    EXPECT_EQ(dock.getActiveTab(), synth::ui::BottomDockComponent::Tab::Timeline);
    EXPECT_TRUE(mc.getTimelinePanel().isVisible());

    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_EQ(dock.getActiveTab(), synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_TRUE(dock.isMixerTabActive());
    EXPECT_FALSE(mc.getTimelinePanel().isVisible()) << "switching tabs hides the other panel, not the dock";

    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    EXPECT_FALSE(dock.isMixerTabActive());
    EXPECT_TRUE(mc.getTimelinePanel().isVisible());
}

TEST(BottomDockComponentTests, ToggleMixerCommandOpensDockOnMixerTabThenClosesOnSecondPress) {
    // Isolates "bottomDockActiveTab" (see the guard's own comment) -- this test switches to the
    // Mixer tab itself and must not leak that into a later test's "Timeline" default assumption.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    ASSERT_FALSE(mc.isBottomDockConfiguredVisible()) << "the dock starts closed";

    mc.performToggleMixerPanel();
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible()) << "closed -> open on the Mixer tab";
    EXPECT_TRUE(mc.getBottomDock().isMixerTabActive());

    mc.performToggleMixerPanel();
    EXPECT_FALSE(mc.isBottomDockConfiguredVisible()) << "open on Mixer -> close (mirrors toggleTimelineButton)";
}

TEST(BottomDockComponentTests, ToggleMixerPanelActionIdRoundTripsToItsCommand) {
    // The command table row's actionId must resolve back to AppCommands::toggleMixerPanel -- the
    // same generic tripwire EveryActionIdRoundTripsToItsOwnCommand checks for every row, pinned
    // here explicitly for this ticket's own new row.
    EXPECT_EQ(AppCommands::getCommandForAction("toggleMixerPanel"), AppCommands::toggleMixerPanel);
}

TEST(BottomDockComponentTests, ActiveTabPersistsAcrossApplicationPropertiesReload) {
    // Same AppProperties-isolation shape ChannelFlowTestFixture.h's ChannelFlowTest::resetKeys()
    // uses: a dedicated settings key, read/written directly against the SAME on-disk "Agent Synth"
    // settings file MainComponent itself uses, cleared before AND after (the guard's ctor/dtor).
    BottomDockActiveTabResetGuardMDT resetGuard;

    {
        MainComponent mc(std::make_unique<MockProviderMDCT>());
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    }

    {
        MainComponent mc2(std::make_unique<MockProviderMDCT>());
        mc2.setSize(1400, 900);
        mc2.newPatchForTest();
        EXPECT_TRUE(mc2.getBottomDock().isMixerTabActive()) << "the persisted tab must survive a relaunch";
    }
}

namespace {

// Same short-lived-ApplicationProperties idiom as BottomDockActiveTabResetGuard.h, opening the
// SAME on-disk "Agent Synth" settings file every MainComponent in this process reads at
// construction -- used here to seed/inspect the old and new bottom-dock-visible keys directly,
// since the migration this test proves runs INSIDE MainComponent's ctor (restorePanelPreferences(),
// before this test's own MainComponent even exists).
juce::PropertiesFile::Options bottomDockVisibleMigrationTestOptions() {
    juce::PropertiesFile::Options opts = synth::userSettingsOptions();
    return opts;
}

// RAII: clears both the old ("timelinePanelVisible") and new ("bottomDockVisible") keys on
// construction AND destruction, so this test's own seeded values never leak into another test's
// "the dock starts closed" default -- mirrors BottomDockActiveTabResetGuardMDT's shape.
struct BottomDockVisibleMigrationKeysGuard {
    BottomDockVisibleMigrationKeysGuard() { clearKeys(); }
    ~BottomDockVisibleMigrationKeysGuard() { clearKeys(); }

    static void clearKeys() {
        juce::ApplicationProperties props;
        props.setStorageParameters(bottomDockVisibleMigrationTestOptions());
        if (auto* s = props.getUserSettings()) {
            s->removeValue("timelinePanelVisible");
            s->removeValue("bottomDockVisible");
            s->saveIfNeeded();
        }
    }
};

} // namespace

TEST(BottomDockComponentTests, StartupMigratesTheOldTimelinePanelVisibleKeyToBottomDockVisible) {
    BottomDockVisibleMigrationKeysGuard keysGuard;
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(bottomDockVisibleMigrationTestOptions());
        auto* s = props.getUserSettings();
        ASSERT_NE(s, nullptr);
        s->setValue("timelinePanelVisible", "1");
        s->saveIfNeeded();
    }

    {
        MainComponent mc(std::make_unique<MockProviderMDCT>());
        mc.setSize(1400, 900);
        mc.newPatchForTest();

        EXPECT_TRUE(mc.isBottomDockConfiguredVisible()) << "the old key's \"1\" must carry over";
    } // MainComponent's ApplicationProperties flushes the migrated file on destruction -- read it only after

    juce::ApplicationProperties props;
    props.setStorageParameters(bottomDockVisibleMigrationTestOptions());
    auto* s = props.getUserSettings();
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->getBoolValue("bottomDockVisible", false)) << "the new key must now hold the migrated value";
    EXPECT_FALSE(s->containsKey("timelinePanelVisible")) << "the old key must be removed once migrated";
}

TEST(BottomDockComponentTests, StartupPrefersTheNewBottomDockVisibleKeyWhenBothKeysExist) {
    BottomDockVisibleMigrationKeysGuard keysGuard;
    {
        juce::ApplicationProperties props;
        props.setStorageParameters(bottomDockVisibleMigrationTestOptions());
        auto* s = props.getUserSettings();
        ASSERT_NE(s, nullptr);
        s->setValue("timelinePanelVisible", "1");
        s->setValue("bottomDockVisible", "0");
        s->saveIfNeeded();
    }

    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    EXPECT_FALSE(mc.isBottomDockConfiguredVisible()) << "the new key wins over a stale old one";
}
