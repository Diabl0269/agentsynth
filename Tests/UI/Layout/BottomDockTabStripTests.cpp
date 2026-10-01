// BottomDockTabStripTests.cpp -- keyboard and screen-reader access to the dock's tab strip
// (docs/layout/chrome.md#tab-strip-keyboard-and-screen-reader-access): real juce::KeyPress objects through the
// strip's focus target, the data the tab handlers are built from, and the strip's focus region.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Layout/BottomDockComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

class BottomDockTabStripTest : public MainComponentTest {
protected:
    // Built after the base fixture's SetUp has reset the persisted panel keys, with the active-tab key
    // isolated too, so every test starts on the Timeline tab.
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        mcOwner_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mcOwner_->setSize(1400, 900);
        mcOwner_->newPatchForTest();
    }
    void TearDown() override {
        mcOwner_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }

    bool press(int keyCode, juce::ModifierKeys mods = {}) {
        return dock().getTabStripFocus().keyPressed(juce::KeyPress(keyCode, mods, 0));
    }
    MainComponent& mc() { return *mcOwner_; }
    BottomDockComponent& dock() { return mc().getBottomDock(); }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mcOwner_;
};

} // namespace

TEST_F(BottomDockTabStripTest, RightAndLeftSelectTheNextAndPreviousTab) {
    ASSERT_EQ(dock().getActiveTab(), Tab::Timeline);
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
    EXPECT_FALSE(mc().getTimelinePanel().isVisible()) << "the panel switches as a click on the tab does";
    EXPECT_TRUE(dock().getMixerPanel().isVisible());

    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::MidiRemote);
    EXPECT_TRUE(press(juce::KeyPress::leftKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);
}

TEST_F(BottomDockTabStripTest, ArrowsStopAtTheEndsAndHomeEndJump) {
    EXPECT_TRUE(press(juce::KeyPress::leftKey)) << "consumed with nowhere to go";
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);

    EXPECT_TRUE(press(juce::KeyPress::endKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::MidiRemote);
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::MidiRemote) << "no wrap";

    EXPECT_TRUE(press(juce::KeyPress::homeKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);
}

TEST_F(BottomDockTabStripTest, ArrowsFollowTheUsersTabOrderAndSkipTabsNotOffered) {
    dock().reorderTabsForTest(Tab::Timeline, Tab::MidiRemote); // strip is now Controllers, Mixer, Timeline
    dock().setActiveTab(Tab::MidiRemote);
    press(juce::KeyPress::rightKey);
    EXPECT_EQ(dock().getActiveTab(), Tab::Mixer);

    dock().setMixerTabEnabled(false); // Mixer leaves the strip
    ASSERT_EQ(dock().getActiveTab(), Tab::MidiRemote) << "falls back to the first offered tab";
    press(juce::KeyPress::rightKey);
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline) << "steps over the tab that is not offered";

    // The swap also re-keys Cmd+1/2/3 and saves them; put both back.
    dock().setMixerTabEnabled(true);
    dock().reorderTabsForTest(Tab::Timeline, Tab::MidiRemote);
}

TEST_F(BottomDockTabStripTest, ReturnMovesFocusIntoTheSelectedPanelsRegionRoot) {
    juce::Component* focused = nullptr;
    dock().setPanelFocusHookForTest([&focused](juce::Component& c) { focused = &c; });

    EXPECT_TRUE(press(juce::KeyPress::returnKey));
    EXPECT_EQ(focused, &mc().getTimelinePanel());
    press(juce::KeyPress::rightKey);
    press(juce::KeyPress::returnKey);
    EXPECT_EQ(focused, &dock().getMixerPanel());
    press(juce::KeyPress::rightKey);
    press(juce::KeyPress::returnKey);
    EXPECT_EQ(focused, &dock().getMidiRemotePanel());

    // The root it hands focus to is exactly the one the matching focus region registers.
    EXPECT_EQ(mc().getFocusRegionsForTest().findById("midiRemote")->root, focused);
}

TEST_F(BottomDockTabStripTest, ModifiedAndUnrelatedKeysAreLeftForTheAppShortcuts) {
    EXPECT_FALSE(press(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier));
    EXPECT_FALSE(press('2', juce::ModifierKeys::commandModifier)) << "Cmd+2 still reaches the shortcut table";
    EXPECT_FALSE(press(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier));
    EXPECT_FALSE(press(juce::KeyPress::tabKey)) << "Tab still cycles the focus regions";
    EXPECT_FALSE(press(juce::KeyPress::downKey));
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);
}

TEST_F(BottomDockTabStripTest, EachTabIsDescribedAsASelectableNamedItem) {
    const auto timeline = BottomDockComponent::describeTab(Tab::Timeline, Tab::Mixer);
    const auto mixer = BottomDockComponent::describeTab(Tab::Mixer, Tab::Mixer);
    const auto controllers = BottomDockComponent::describeTab(Tab::MidiRemote, Tab::Mixer);

    EXPECT_EQ(timeline.title, "Timeline");
    EXPECT_EQ(mixer.title, "Mixer");
    EXPECT_EQ(controllers.title, "Controllers");
    EXPECT_FALSE(timeline.selected);
    EXPECT_TRUE(mixer.selected);
    EXPECT_FALSE(controllers.selected);
    for (const auto& d : {timeline, mixer, controllers})
        EXPECT_EQ(d.role, juce::AccessibilityRole::radioButton) << "JUCE has no tab role; this is its selectable one";
}

TEST_F(BottomDockTabStripTest, TabButtonsAreNamedAndTheirToggleStateTracksTheSelection) {
    auto tabs = dock().getStripTabs();
    ASSERT_EQ(tabs.size(), 3u);
    for (const auto& t : tabs)
        EXPECT_EQ(t.button->getTitle(), t.name);

    dock().setActiveTab(Tab::Mixer);
    for (const auto& t : dock().getStripTabs())
        EXPECT_EQ(t.button->getToggleState(), t.name == "Mixer") << t.name;
}

TEST_F(BottomDockTabStripTest, TheStripIsOneTabStopAndTheTabButtonsAreNot) {
    auto& focus = dock().getTabStripFocus();
    EXPECT_TRUE(focus.getWantsKeyboardFocus());
    EXPECT_FALSE(focus.getTitle().isEmpty());
    bool onSelf = true, onChildren = true;
    focus.getInterceptsMouseClicks(onSelf, onChildren);
    EXPECT_FALSE(onSelf) << "mouse clicks reach the tab buttons, never this leaf";
    for (auto* b : dock().getTabButtons())
        EXPECT_FALSE(b->getWantsKeyboardFocus());
}

TEST_F(BottomDockTabStripTest, TheStripIsAFocusRegionBetweenTheCanvasAndTheTimelineThatFollowsTheDock) {
    auto& regions = mc().getFocusRegionsForTest();
    const auto* region = regions.findById("dockTabs");
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->root, &dock().getTabStripFocus());
    EXPECT_EQ(regions.regionContaining(&dock().getTabStripFocus()), region);
    EXPECT_NE(regions.regionContaining(&mc().getTimelinePanel()), region) << "the panel is its own region";

    ASSERT_TRUE(mc().getCommandManager().invokeDirectly(AppCommands::focusTimeline, false));
    EXPECT_TRUE(region->isCurrentlyOpen());
    EXPECT_EQ(regions.nextOpenRegionId("canvas", true), "dockTabs");
    EXPECT_EQ(regions.nextOpenRegionId("dockTabs", true), "timeline");
}

// ---- Cmd+T and Cmd+1/2/3 put focus on the strip -----------------------------------------------------------

namespace {
// MainComponent::keyPressed hands a matched command to the command manager asynchronously, so the
// message queue is drained before the caller looks at the result.
bool pressKey(MainComponent& mc, const juce::KeyPress& key) {
    const bool used = mc.keyPressed(key);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return used;
}

juce::KeyPress cmd(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::commandModifier, 0); }
} // namespace

class DockShortcutFocusTest : public BottomDockTabStripTest {
protected:
    // A headless window cannot hold keyboard focus, so the regions the shortcuts focus are recorded.
    void SetUp() override {
        BottomDockTabStripTest::SetUp();
        mc().getFocusRegionsForTest().onRegionFocused = [this](const juce::String& id) { focused_.add(id); };
    }
    void setDockOpen(bool open) {
        if (mc().isBottomDockConfiguredVisible() != open)
            mc().simulateToggleBottomPanelClick();
        ASSERT_EQ(mc().isBottomDockConfiguredVisible(), open);
    }
    juce::StringArray focused_;
};

TEST_F(DockShortcutFocusTest, CommandDigitsShowTheTabAndFocusTheStrip) {
    mc().showBottomDockTab(Tab::Timeline);
    for (const auto& [digit, tab] :
         {std::pair{'2', Tab::Mixer}, std::pair{'3', Tab::MidiRemote}, std::pair{'1', Tab::Timeline}}) {
        focused_.clear();
        EXPECT_TRUE(pressKey(mc(), cmd(digit))) << digit;
        EXPECT_EQ(dock().getActiveTab(), tab) << digit;
        EXPECT_EQ(focused_.joinIntoString(","), "dockTabs") << "Cmd+" << digit << " focuses the strip";
    }
}

TEST_F(DockShortcutFocusTest, CommandDigitOnAClosedDockOpensItAndFocusesTheStrip) {
    setDockOpen(false);
    focused_.clear();
    EXPECT_TRUE(pressKey(mc(), cmd('2')));
    EXPECT_TRUE(mc().isBottomDockConfiguredVisible());
    EXPECT_EQ(focused_.joinIntoString(","), "dockTabs");
}

TEST_F(DockShortcutFocusTest, ADetachedTabIsRaisedAndNothingIsFocused) {
    dock().getTimelineHost().setDetached(true);
    dock().setActiveTab(Tab::Mixer);
    focused_.clear();
    EXPECT_TRUE(pressKey(mc(), cmd('1')));
    EXPECT_EQ(focused_.joinIntoString(","), "") << "the detached window comes to the front instead";
    dock().getTimelineHost().setDetached(false);
}

TEST_F(DockShortcutFocusTest, CommandTOpensThePaneOnItsTabStrip) {
    setDockOpen(false);
    focused_.clear();
    EXPECT_TRUE(pressKey(mc(), cmd('t')));
    EXPECT_TRUE(mc().isBottomDockConfiguredVisible());
    EXPECT_EQ(focused_.joinIntoString(","), "dockTabs");
}

TEST_F(DockShortcutFocusTest, CommandTClosingThePaneAroundTheFocusMovesFocusToTheCanvas) {
    setDockOpen(true);
    mc().setFocusedComponentOverrideForTest(&dock().getTabStripFocus());
    focused_.clear();
    EXPECT_TRUE(pressKey(mc(), cmd('t')));
    EXPECT_FALSE(mc().isBottomDockConfiguredVisible());
    EXPECT_EQ(focused_.joinIntoString(","), "canvas") << "focus must not be lost with the hidden pane";
}

TEST_F(DockShortcutFocusTest, CommandTClosingThePaneWithFocusElsewhereLeavesFocusAlone) {
    setDockOpen(true);
    mc().setFocusedComponentOverrideForTest(mc().getFocusRegionsForTest().findById("library")->root);
    focused_.clear();
    EXPECT_TRUE(pressKey(mc(), cmd('t')));
    EXPECT_FALSE(mc().isBottomDockConfiguredVisible());
    EXPECT_EQ(focused_.joinIntoString(","), "");
}

// ---- Cmd+Option+Left/Right no longer step tabs ---------------------------------------------------------

TEST_F(DockShortcutFocusTest, CommandOptionArrowsAreNotBoundAnymore) {
    mc().showBottomDockTab(Tab::Timeline);
    const auto mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier;
    for (int arrow : {juce::KeyPress::rightKey, juce::KeyPress::leftKey}) {
        EXPECT_TRUE(mc().getShortcutManager()
                        .getActionsForKeyPress(juce::KeyPress(arrow, juce::ModifierKeys(mods), 0))
                        .isEmpty());
        pressKey(mc(), juce::KeyPress(arrow, juce::ModifierKeys(mods), 0));
        EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);
    }
}
