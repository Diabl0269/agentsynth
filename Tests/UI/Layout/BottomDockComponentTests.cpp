// BottomDockComponentTests.cpp -- the dock's tab strip, the Toggle Mixer Panel
// shortcut/command, and active-tab persistence via ApplicationProperties.
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "BottomDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
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

    // "show tab" never closes the dock any more -- only toggleBottomPanel does.
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
    // here explicitly for the mixer-panel row.
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

// The panel's user-facing name is "Controllers" on the dock tab, while the persisted
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

// Timeline/Mixer/Controllers no longer each toggle the whole dock closed -- they only ever
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

// The dock's own tab order (Timeline, Mixer, Controllers by default) is what Cmd+1..3 and
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

// Dragging a tab past another swaps their order, persists it, and re-keys Cmd+1/2/3 so
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

// The saved tab order and the Cmd+digit bindings live in separate stores, so they can disagree at
// launch; loading re-keys the digits from the restored order so the first tab is always Cmd+1.
TEST(BottomDockComponentTests, LoadingRekeysTheCmdDigitsFromTheRestoredTabOrder) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    auto& shortcuts = mc.getShortcutManager();
    const auto cmd = [](int digit) { return juce::KeyPress('0' + digit, juce::ModifierKeys::commandModifier, 0); };
    shortcuts.setBinding("toggleMixerPanel", cmd(1));
    shortcuts.setBinding("toggleTimelinePanel", cmd(2));
    auto* settings = mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->setValue("bottomDockTabOrder", "timeline,mixer,midiRemote");

    mc.getBottomDock().setApplicationProperties(&mc.getAppPropertiesForTest());

    EXPECT_EQ(shortcuts.getBinding("toggleTimelinePanel"), cmd(1));
    EXPECT_EQ(shortcuts.getBinding("toggleMixerPanel"), cmd(2));
    EXPECT_EQ(shortcuts.getBinding("toggleMidiRemotePanel"), cmd(3));

    // A binding the user moved off the Cmd+digit convention is never overridden.
    shortcuts.setBinding("toggleMixerPanel", juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0));
    shortcuts.setBinding("toggleTimelinePanel", cmd(2));
    mc.getBottomDock().setApplicationProperties(&mc.getAppPropertiesForTest());
    EXPECT_EQ(shortcuts.getBinding("toggleTimelinePanel"), cmd(2));
    EXPECT_EQ(shortcuts.getBinding("toggleMixerPanel"), juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0));

    shortcuts.setBinding("toggleMixerPanel", cmd(2));
    shortcuts.setBinding("toggleTimelinePanel", cmd(1));
    shortcuts.saveToProperties();
}

// Detaching the ACTIVE tab must never leave the dock showing nothing -- it falls
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

// The tab strip's width split must be identical on every tab: nothing Mixer-only is carved from it
// (the mixer's own toolbar row lives inside the mixer panel).
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

// A tab is a dedicated grab handle: the dragging-hand cursor shows on hover, through the drag, and
// after the release (the tab is still a grab handle).
TEST(BottomDockComponentTests, TabsShowTheDraggingHandCursorOnHoverAndThroughTheDrag) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateToggleBottomPanelClick();
    auto& dock = mc.getBottomDock();

    auto buttons = dock.getTabButtons();
    ASSERT_FALSE(buttons.empty());
    auto* button = buttons.front();

    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::DraggingHandCursor) << "hover, before any press";
    button->mouseDown(makeClickEvent(*button, {5.0f, 5.0f}));
    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::DraggingHandCursor);

    button->mouseDrag(makeDragEvent(*button, {60.0f, 5.0f}, {5.0f, 5.0f}));
    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::DraggingHandCursor);

    button->mouseUp(makeClickEvent(*button, {60.0f, 5.0f}));
    EXPECT_TRUE(button->getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
}

namespace {

// Paints the whole dock (children included) into a software-backed image: the default image type
// is GPU-backed on Windows and reads back as zeros on a headless runner.
juce::Image paintDock(synth::ui::BottomDockComponent& dock) {
    juce::Image img(juce::Image::ARGB, dock.getWidth(), dock.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    dock.paintEntireComponent(g, false);
    return img;
}

bool isCloseTo(juce::Colour actual, juce::Colour expected, int tolerance = 6) {
    return std::abs((int)actual.getRed() - (int)expected.getRed()) <= tolerance &&
           std::abs((int)actual.getGreen() - (int)expected.getGreen()) <= tolerance &&
           std::abs((int)actual.getBlue() - (int)expected.getBlue()) <= tolerance && actual.getAlpha() > 200;
}

// A lifted tab sits 4 px above its slot, so its top edge row is the slot's top minus 4.
constexpr int kLiftRise = 4;

struct LiftedTabFixture {
    // Declared first so it outlives the MainComponent (and the dock that points at it).
    synth::theme::AppLookAndFeel laf;
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderMDCT>()};
    synth::ui::BottomDockComponent* dockPtr = nullptr;
    juce::Component* timeline = nullptr;

    explicit LiftedTabFixture(const synth::theme::Theme& theme = synth::theme::makeObsidian()) {
        laf.applyTheme(theme);
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        mc.simulateToggleBottomPanelClick();
        dockPtr = &mc.getBottomDock();
        dockPtr->setLookAndFeel(&laf);
        timeline = dockPtr->getTabButtons().front();
    }
    ~LiftedTabFixture() { dockPtr->setLookAndFeel(nullptr); }
    synth::ui::BottomDockComponent& dock() { return *dockPtr; }
    const synth::theme::Theme& theme() const { return laf.getTheme(); }
    int liftedRowY() const { return timeline->getY() - kLiftRise; }
    // Dock x of the pointer after dragging by `dx` from a press 10 px into the Timeline tab.
    int pointerX(int dx) const { return timeline->getX() + 10 + dx; }
    void press() { timeline->mouseDown(makeClickEvent(*timeline, {10.0f, 8.0f})); }
    void dragBy(int dx) { timeline->mouseDrag(makeDragEvent(*timeline, {(float)(10 + dx), 8.0f}, {10.0f, 8.0f})); }
    void release(int dx) { timeline->mouseUp(makeClickEvent(*timeline, {(float)(10 + dx), 8.0f})); }
};

} // namespace

TEST(BottomDockComponentTests, DraggingATabDrawsALiftedTabAtThePointerAndMouseUpRemovesIt) {
    LiftedTabFixture f;
    const auto& colors = f.theme().colors;
    ASSERT_GE(f.liftedRowY(), 0) << "the lifted tab must fit inside the dock's own bounds";
    const int drag = 30;
    const int x = f.pointerX(drag);
    const int y = f.liftedRowY();
    const auto before = paintDock(f.dock()).getPixelAt(x, y);

    f.press();
    f.dragBy(drag);
    const auto during = paintDock(f.dock());
    EXPECT_TRUE(isCloseTo(during.getPixelAt(x, y), colors.accent)) << "the accent outline row of the lifted tab";
    EXPECT_TRUE(isCloseTo(during.getPixelAt(x, y + 3), colors.surfaceHi)) << "the surfaceHi body of the lifted tab";
    EXPECT_FALSE(isCloseTo(before, colors.accent));

    f.release(drag);
    const auto after = paintDock(f.dock()).getPixelAt(x, y);
    EXPECT_EQ(after, before) << "the lifted look is gone after mouseUp";
}

TEST(BottomDockComponentTests, LiftedTabFollowsThePointerHorizontally) {
    LiftedTabFixture f;
    const auto& colors = f.theme().colors;
    const int y = f.liftedRowY();
    f.press();
    f.dragBy(20);
    EXPECT_TRUE(isCloseTo(paintDock(f.dock()).getPixelAt(f.pointerX(20), y), colors.accent));
    // The tab is 1/3 of the strip wide, so a point one tab-width right of the press is off the lifted tab.
    const int farRight = f.timeline->getRight() + 20;
    EXPECT_FALSE(isCloseTo(paintDock(f.dock()).getPixelAt(farRight, y), colors.accent));
    f.dragBy(20 + f.timeline->getWidth() / 2);
    EXPECT_TRUE(isCloseTo(paintDock(f.dock()).getPixelAt(farRight, y), colors.accent));
    f.release(20);
}

TEST(BottomDockComponentTests, DraggedTabSlotShowsADashedBorderOutlineInsteadOfTheTab) {
    LiftedTabFixture f;
    const auto& colors = f.theme().colors;
    // A slot pixel on the outline's left edge, mid-height (a dash boundary can land on any row, so
    // scan the left edge column for at least one border-coloured pixel).
    const auto slot = f.timeline->getBounds();
    f.press();
    f.dragBy(4);
    const auto img = paintDock(f.dock());
    int borderPixels = 0;
    for (int y = slot.getY() + 4; y < slot.getBottom() - 4; ++y)
        if (isCloseTo(img.getPixelAt(slot.getX(), y), colors.border, 24))
            ++borderPixels;
    EXPECT_GT(borderPixels, 0) << "the slot draws the dashed border outline";
    EXPECT_LT(borderPixels, slot.getHeight() - 8) << "a dashed (not solid) outline leaves gaps";
    f.release(4);
}

TEST(BottomDockComponentTests, ADragKeepsTheOrderUntilReleaseThenSavesItAndClearsTheLift) {
    LiftedTabFixture f;
    using Tab = synth::ui::BottomDockComponent::Tab;
    const auto& colors = f.theme().colors;
    auto buttons = f.dock().getTabButtons();
    ASSERT_GE(buttons.size(), 2u);
    const int dx = buttons[1]->getBounds().getCentreX() - f.pointerX(0);
    const int y = f.liftedRowY();
    // The pointer's dock x is taken before the drag.
    const int pointerX = f.pointerX(dx);

    f.press();
    f.dragBy(dx);
    const std::vector<Tab> unchanged{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    EXPECT_EQ(f.dock().getTabOrderForTest(), unchanged) << "the order is not touched mid-drag";
    EXPECT_TRUE(isCloseTo(paintDock(f.dock()).getPixelAt(pointerX, y), colors.accent));

    f.release(dx);
    const std::vector<Tab> dropped{Tab::Mixer, Tab::Timeline, Tab::MidiRemote};
    EXPECT_EQ(f.dock().getTabOrderForTest(), dropped);
    EXPECT_FALSE(isCloseTo(paintDock(f.dock()).getPixelAt(pointerX, y), colors.accent));
    auto* settings = f.mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->getValue("bottomDockTabOrder"), "mixer,timeline,midiRemote");
    EXPECT_EQ(f.mc.getShortcutManager().getBinding("toggleMixerPanel"),
              juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0));
}

TEST(BottomDockComponentTests, APlainClickNeverShowsTheLiftedTab) {
    LiftedTabFixture f;
    const auto& colors = f.theme().colors;
    const int y = f.liftedRowY();
    const int x = f.pointerX(0);
    f.press();
    EXPECT_FALSE(isCloseTo(paintDock(f.dock()).getPixelAt(x, y), colors.accent));
    // A drag event below JUCE's drag threshold (mouseWasDraggedSinceMouseDown() is false) is still a click.
    f.timeline->mouseDrag(makeClickEvent(*f.timeline, {12.0f, 8.0f}));
    EXPECT_FALSE(isCloseTo(paintDock(f.dock()).getPixelAt(x, y), colors.accent));
    f.release(0);
    EXPECT_FALSE(isCloseTo(paintDock(f.dock()).getPixelAt(x, y), colors.accent));
}

namespace {

// Emulates what JUCE reports for a press at dock x `pressX`: positions and the mouse-down anchor
// are both re-derived in the pressed button's CURRENT local space at every event, so a button that
// moves under the pointer shifts the anchor exactly as it does live.
struct RealisticTabDrag {
    juce::Component& button;
    int pressX;
    float y = 8.0f;
    void down() { button.mouseDown(makeClickEvent(button, {(float)(pressX - button.getX()), y})); }
    void dragTo(int pointerX) {
        button.mouseDrag(
            makeDragEvent(button, {(float)(pointerX - button.getX()), y}, {(float)(pressX - button.getX()), y}));
    }
    void up(int pointerX) { button.mouseUp(makeClickEvent(button, {(float)(pointerX - button.getX()), y})); }
};

} // namespace

// Regression test for FRO369: the lifted tab jumped by about one slot per swap because its left
// edge came from JUCE's mouse-down position, which is re-derived in the (moving) button's local
// space. The grab offset is captured once now, so the lifted tab stays exactly under the pointer.
TEST(BottomDockComponentTests, LiftedTabStaysUnderTheGrabPointAcrossEveryInsertionChange) {
    LiftedTabFixture f;
    using Tab = synth::ui::BottomDockComponent::Tab;
    const int x0 = f.timeline->getX();
    const int w = f.timeline->getWidth();
    const int grab = 10;
    RealisticTabDrag drag{*f.timeline, x0 + grab};
    drag.down();

    for (int pointerX : {x0 + grab + w / 2, x0 + grab + w + w / 4, x0 + grab + w + w / 2 + 3, x0 + grab + w + 20,
                         x0 + grab + w * 2 - 4}) {
        drag.dragTo(pointerX);
        EXPECT_FLOAT_EQ(f.dock().getLiftedTabLeftForTest(), (float)(pointerX - grab)) << "pointer at " << pointerX;
        const std::vector<Tab> untouched{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
        EXPECT_EQ(f.dock().getTabOrderForTest(), untouched) << "the order is only applied on release";
    }

    drag.up(x0 + grab + w * 2 - 4);
    const std::vector<Tab> dropped{Tab::Mixer, Tab::Timeline, Tab::MidiRemote};
    EXPECT_EQ(f.dock().getTabOrderForTest(), dropped);
}

TEST(BottomDockComponentTests, DroppingAtTheStripEndMovesTheTabLastAndPersistsIt) {
    LiftedTabFixture f;
    using Tab = synth::ui::BottomDockComponent::Tab;
    const int x0 = f.timeline->getX();
    const int w = f.timeline->getWidth();
    const int stripRight = f.dock().getTabButtons().back()->getRight();
    RealisticTabDrag drag{*f.timeline, x0 + 10};
    drag.down();
    drag.dragTo(x0 + 10 + w);
    drag.dragTo(x0 + 10 + 3 * w);
    EXPECT_FLOAT_EQ(f.dock().getLiftedTabLeftForTest(), (float)(stripRight - w))
        << "the lifted tab is held inside the strip";
    drag.up(x0 + 10 + 3 * w);

    const std::vector<Tab> expected{Tab::Mixer, Tab::MidiRemote, Tab::Timeline};
    EXPECT_EQ(f.dock().getTabOrderForTest(), expected);
    auto* settings = f.mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->getValue("bottomDockTabOrder"), "mixer,midiRemote,timeline");
}

// Esc mid-drag: the tab returns where it was, nothing is committed or persisted, and the release
// that follows must not also switch to the tab.
TEST(BottomDockComponentTests, EscapeMidDragCancelsWithoutReorderingOrClicking) {
    LiftedTabFixture f;
    using Tab = synth::ui::BottomDockComponent::Tab;
    auto* settings = f.mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->removeValue("bottomDockTabOrder");
    const auto restingBounds = f.timeline->getBounds();
    const int x0 = f.timeline->getX();
    const int w = f.timeline->getWidth();
    f.dock().setActiveTab(Tab::Mixer);

    RealisticTabDrag drag{*f.timeline, x0 + 10};
    drag.down();
    drag.dragTo(x0 + 10 + w + w / 2);
    ASSERT_TRUE(f.dock().sendEscapeToTabDragForTest());
    EXPECT_EQ(f.timeline->getBounds(), restingBounds) << "back in its origin slot";
    EXPECT_FALSE(f.dock().sendEscapeToTabDragForTest()) << "the key listener is gone once the drag is cancelled";

    drag.up(x0 + 10 + w + w / 2);
    const std::vector<Tab> untouched{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    EXPECT_EQ(f.dock().getTabOrderForTest(), untouched);
    EXPECT_FALSE(settings->containsKey("bottomDockTabOrder")) << "no persistence for a cancelled drag";
    EXPECT_EQ(f.dock().getActiveTab(), Tab::Mixer) << "the release of a cancelled drag is not a click";
}

// The click that follows a sub-threshold press is JUCE's own Button::mouseUp, which needs a real
// mouse button state a synthesized event cannot fake; what this pins is that such a press never
// lifts, reorders or persists anything.
TEST(BottomDockComponentTests, APressBelowTheThresholdNeverLiftsOrReordersTabs) {
    LiftedTabFixture f;
    using Tab = synth::ui::BottomDockComponent::Tab;
    auto* settings = f.mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->removeValue("bottomDockTabOrder");
    auto* mixer = f.dock().getTabButtons()[1];
    const auto resting = mixer->getBounds();
    RealisticTabDrag press{*mixer, mixer->getX() + 10};
    press.down();
    press.dragTo(mixer->getX() + 12); // 2 px: below the drag threshold
    EXPECT_FALSE(f.dock().sendEscapeToTabDragForTest()) << "no drag is in progress to cancel";
    press.up(mixer->getX() + 12);
    EXPECT_EQ(mixer->getBounds(), resting);
    const std::vector<Tab> untouched{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    EXPECT_EQ(f.dock().getTabOrderForTest(), untouched);
    EXPECT_FALSE(settings->containsKey("bottomDockTabOrder"));
}
