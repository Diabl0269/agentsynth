// BottomDockComponentTests.cpp -- FRO11 (P9-5): the dock's tab strip, the Toggle Mixer Panel
// shortcut/command, and active-tab persistence via ApplicationProperties.
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "BottomDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <vector>

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

TEST(BottomDockComponentTests, ShowMixerTabOpensTheDockAndStaysOpenOnASecondPress) {
    // Isolates "bottomDockActiveTab" (see the guard's own comment) -- this test switches to the
    // Mixer tab itself and must not leak that into a later test's "Timeline" default assumption.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    ASSERT_FALSE(mc.isBottomDockConfiguredVisible()) << "the dock starts closed";

    mc.showBottomDockTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible()) << "closed -> open on the Mixer tab";
    EXPECT_TRUE(mc.getBottomDock().isMixerTabActive());

    // FRO333: "show tab" never closes the dock any more -- only toggleBottomPanel does.
    mc.showBottomDockTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible()) << "a second press is a no-op, not a close";
    EXPECT_TRUE(mc.getBottomDock().isMixerTabActive());
}

TEST(BottomDockComponentTests, ToggleBottomPanelOpensOnTheLastActiveTabThenClosesOnSecondPress) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);

    ASSERT_FALSE(mc.isBottomDockConfiguredVisible()) << "the dock starts closed";

    mc.simulateToggleBottomPanelClick();
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible());
    EXPECT_TRUE(mc.getBottomDock().isMixerTabActive()) << "reopens on the last tab used, not always Timeline";

    mc.simulateToggleBottomPanelClick();
    EXPECT_FALSE(mc.isBottomDockConfiguredVisible()) << "the ONE toggle still closes the whole panel";
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

// FRO329: the panel's user-facing name is "Controllers" on the dock tab, while the persisted
// action id (ShortcutManager binds by id) keeps its original spelling.
TEST(BottomDockComponentTests, ControllersTabUsesTheNewNameAndKeepsTheActionId) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    auto& dock = mc.getBottomDock();

    bool foundControllersTab = false;
    for (auto* child : dock.getChildren()) {
        if (auto* button = dynamic_cast<juce::TextButton*>(child)) {
            EXPECT_FALSE(button->getButtonText().contains("MIDI Remote")) << button->getButtonText();
            foundControllersTab = foundControllersTab || button->getButtonText() == "Controllers";
        }
    }
    EXPECT_TRUE(foundControllersTab);
    EXPECT_EQ(ShortcutManager::getActionDescription("toggleMidiRemotePanel"), "Show Controllers Tab");
}

// FRO333: Timeline/Mixer/Controllers no longer each toggle the whole dock closed -- they only ever
// show their own tab (opening the dock if it was hidden).
TEST(BottomDockComponentTests, ShowTabActionsNeverCloseTheDock) {
    EXPECT_EQ(AppCommands::getCommandForAction("toggleTimelinePanel"), AppCommands::toggleTimelinePanel);
    EXPECT_EQ(AppCommands::getCommandForAction("toggleMixerPanel"), AppCommands::toggleMixerPanel);
    EXPECT_EQ(AppCommands::getCommandForAction("toggleMidiRemotePanel"), AppCommands::toggleMidiRemotePanel);
    EXPECT_EQ(AppCommands::getCommandForAction("toggleBottomPanel"), AppCommands::toggleBottomPanel);
    EXPECT_EQ(ShortcutManager::getActionDescription("toggleTimelinePanel"), "Show Timeline Tab");
    EXPECT_EQ(ShortcutManager::getActionDescription("toggleMixerPanel"), "Show Mixer Tab");
    EXPECT_EQ(ShortcutManager::getActionDescription("toggleBottomPanel"), "Toggle Bottom Panel");
}

// FRO333: the dock's own tab order (Timeline, Mixer, Controllers by default) is what Cmd+1..3 and
// the strip's own left-to-right layout follow; getTabButtons() reports it in that same order.
TEST(BottomDockComponentTests, DefaultTabOrderIsTimelineMixerControllers) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    auto& dock = mc.getBottomDock();
    using Tab = synth::ui::BottomDockComponent::Tab;
    const std::vector<Tab> expected{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    EXPECT_EQ(dock.getTabOrderForTest(), expected);
}

// FRO333: dragging a tab past another swaps their order, persists it, and re-keys Cmd+1/2/3 so
// Cmd+1 keeps opening whichever tab now sits first.
TEST(BottomDockComponentTests, DragReorderSwapsTabOrderAndPermutesTheCmdDigitBindings) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    auto& dock = mc.getBottomDock();
    using Tab = synth::ui::BottomDockComponent::Tab;

    dock.reorderTabsForTest(Tab::Timeline, Tab::Mixer);

    const std::vector<Tab> expected{Tab::Mixer, Tab::Timeline, Tab::MidiRemote};
    EXPECT_EQ(dock.getTabOrderForTest(), expected);
    EXPECT_EQ(mc.getShortcutManager().getBinding("toggleMixerPanel"),
              juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0));
    EXPECT_EQ(mc.getShortcutManager().getBinding("toggleTimelinePanel"),
              juce::KeyPress('2', juce::ModifierKeys::commandModifier, 0));
    EXPECT_EQ(mc.getShortcutManager().getBinding("toggleMidiRemotePanel"),
              juce::KeyPress('3', juce::ModifierKeys::commandModifier, 0));
}

// FRO158/FRO333: detaching the ACTIVE tab must never leave the dock showing nothing -- it falls
// back to the next tab still offered, and hides the whole dock only once none are left.
TEST(BottomDockComponentTests, DetachingTheActiveTabFallsBackToTheNextOneAndNeverGoesBlank) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    using Tab = synth::ui::BottomDockComponent::Tab;

    mc.showBottomDockTab(Tab::Mixer);
    ASSERT_TRUE(dock.isMixerTabActive());

    dock.getMixerHost().setDetached(true);
    EXPECT_FALSE(dock.isMixerTabActive()) << "the detached tab must not stay 'active' with nothing shown for it";
    EXPECT_TRUE(dock.getActiveTab() == Tab::Timeline || dock.getActiveTab() == Tab::MidiRemote)
        << "falls back to a tab that's still offered";
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible()) << "two tabs remain -- the dock itself must stay open";

    dock.getTimelineHost().setDetached(true);
    dock.getMidiRemoteHost().setDetached(true);
    EXPECT_FALSE(dock.hasAnyVisibleTab());
    EXPECT_FALSE(mc.isBottomDockConfiguredVisible()) << "no tabs left -- the whole panel hides";

    dock.getMixerHost().setDetached(false);
    EXPECT_TRUE(mc.isBottomDockConfiguredVisible()) << "a redock while auto-hidden reopens the panel";
    EXPECT_TRUE(dock.isMixerTabActive());
}

// FRO338: add-bus/reset-meters used to be carved from the tab strip's own right edge, Mixer-tab-
// only -- switching to Mixer visibly shrank the tab strip's shared area, so every tab button's own
// bounds changed depending on which tab was active. They now live in their own toolbar row above
// the Mixer content instead, so the tab strip's width split must be identical on every tab.
TEST(BottomDockComponentTests, TabButtonBoundsAreIdenticalWhetherTimelineOrMixerIsActive) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateToggleBottomPanelClick();
    auto& dock = mc.getBottomDock();

    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    std::vector<juce::Rectangle<int>> timelineActiveBounds;
    for (auto* button : dock.getTabButtons())
        timelineActiveBounds.push_back(button->getBounds());

    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    ASSERT_EQ(dock.getTabButtons().size(), timelineActiveBounds.size());
    for (size_t i = 0; i < timelineActiveBounds.size(); ++i)
        EXPECT_EQ(dock.getTabButtons()[i]->getBounds(), timelineActiveBounds[i])
            << "tab button " << i << " moved when switching tabs";
}

// FRO338: the buttons are Mixer-only, and now that they've moved off the tab strip they must sit
// entirely below it (never sharing a pixel with a tab button).
TEST(BottomDockComponentTests, AddBusAndResetMetersAreMixerOnlyAndNeverOverlapTheTabStrip) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateToggleBottomPanelClick();
    auto& dock = mc.getBottomDock();

    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    EXPECT_FALSE(dock.getAddBusButtonForTest().isVisible());
    EXPECT_FALSE(dock.getResetMetersButtonForTest().isVisible());

    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_TRUE(dock.getAddBusButtonForTest().isVisible());
    EXPECT_TRUE(dock.getResetMetersButtonForTest().isVisible());

    const juce::Rectangle<int> tabStripArea(0, 0, dock.getWidth(), synth::ui::BottomDockComponent::kTabStripHeight);
    EXPECT_FALSE(tabStripArea.intersects(dock.getAddBusButtonForTest().getBounds()));
    EXPECT_FALSE(tabStripArea.intersects(dock.getResetMetersButtonForTest().getBounds()));
    for (auto* button : dock.getTabButtons()) {
        EXPECT_FALSE(button->getBounds().intersects(dock.getAddBusButtonForTest().getBounds()));
        EXPECT_FALSE(button->getBounds().intersects(dock.getResetMetersButtonForTest().getBounds()));
    }
}

// FRO338: a real reorder drag shows the dragging-hand cursor once it clears JUCE's own drag
// threshold, and mouseUp always restores it -- same reasoning as GraphEditor's macro-chip cursor
// (GraphEditorCanvas.cpp's mouseMove).
TEST(BottomDockComponentTests, DraggingATabShowsTheDraggingHandCursorAndMouseUpRestoresNormal) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateToggleBottomPanelClick();
    auto& dock = mc.getBottomDock();

    auto buttons = dock.getTabButtons();
    ASSERT_FALSE(buttons.empty());
    auto* button = buttons.front();

    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::NormalCursor);
    button->mouseDown(makeClickEvent(*button, {5.0f, 5.0f}));
    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::NormalCursor)
        << "no cursor change from a mouseDown alone";

    button->mouseDrag(makeDragEvent(*button, {60.0f, 5.0f}, {5.0f, 5.0f}));
    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::DraggingHandCursor);

    button->mouseUp(makeClickEvent(*button, {60.0f, 5.0f}));
    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::NormalCursor);
}
