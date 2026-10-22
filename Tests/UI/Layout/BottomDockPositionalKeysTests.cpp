// BottomDockPositionalKeysTests.cpp -- Cmd+N / Opt+Cmd+N follow the tab order and Cmd+0 focuses the main view: the
// defaults, a reorder re-keying the tabs (and the hint and tooltip texts with it), the main window being raised for a
// target that is not detached, and the keys working from inside a detached panel's window
// (docs/control/shortcuts.md#switching-tabs).
#include "BottomDockMainAreaFixture.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/IconButton.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;
using Order = std::vector<Tab>;

constexpr auto kCmd = juce::ModifierKeys::commandModifier;
constexpr auto kCmdAlt = juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier;

juce::KeyPress cmd(juce::juce_wchar digit) { return juce::KeyPress(digit, kCmd, 0); }

} // namespace

TEST(BottomDockPositionalKeyDefaults, TheDigitsBelongToThePositionalActionsAndTheViewActionsAreUnbound) {
    ShortcutManager manager;
    for (int n = 1; n <= 3; ++n) {
        const auto show = BottomDockComponent::showActionIdForPosition(n);
        const auto swap = BottomDockComponent::swapActionIdForPosition(n);
        EXPECT_EQ(manager.getBinding(show), cmd('0' + n)) << show;
        EXPECT_EQ(manager.getBinding(swap), juce::KeyPress('0' + n, kCmdAlt, 0)) << swap;
        EXPECT_NE(AppCommands::getCommandForAction(show), AppCommands::kNoCommand);
        EXPECT_NE(AppCommands::getCommandForAction(swap), AppCommands::kNoCommand);
        EXPECT_TRUE(manager.getConflictingAction(show, manager.getBinding(show)).isEmpty());
        EXPECT_TRUE(manager.getConflictingAction(swap, manager.getBinding(swap)).isEmpty());
        EXPECT_NE(ShortcutManager::getActionDescription(show), juce::String(show));
    }
    EXPECT_EQ(manager.getBinding("focusMainView"), cmd('0'));
    EXPECT_EQ(AppCommands::getCommandForAction("focusMainView"), AppCommands::focusMainView);
    for (const char* id : {"toggleTimelinePanel", "toggleMixerPanel", "toggleMidiRemotePanel", "showCanvas",
                           "swapTimelineToMain", "swapMixerToMain", "swapMidiRemoteToMain", "swapCanvasToMain"})
        EXPECT_FALSE(manager.getBinding(id).isValid()) << id << " keeps its action but not the digit";
}

class BottomDockPositionalKeysTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
    }
    // A key as the app receives it.
    // The command manager runs the matched command asynchronously, so the queue is drained before the caller looks.
    bool press(const juce::KeyPress& key) {
        const bool used = mc_->keyPressed(key);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        return used;
    }
    juce::String hintFor(Tab tab) {
        const auto& order = dock().getTabOrderForTest();
        const auto buttons = dock().getTabButtons(); // parallel to the order
        const auto* button = buttons[size_t(std::find(order.begin(), order.end(), tab) - order.begin())];
        for (const auto& strip : dock().getStripTabs())
            if (strip.button == button)
                return ShortcutManager::keyPressToDisplayString(mc_->getShortcutManager().getBinding(strip.actionId));
        return {};
    }
};

TEST_F(BottomDockPositionalKeysTest, CmdNOpensTheTabAtPositionNInTheCurrentOrder) {
    ASSERT_EQ(dock().getMainView(), Tab::Canvas);
    for (const auto& [digit, tab] :
         {std::pair{'1', Tab::Timeline}, std::pair{'2', Tab::Mixer}, std::pair{'3', Tab::MidiRemote}}) {
        EXPECT_TRUE(press(cmd(digit)));
        EXPECT_EQ(dock().getActiveTab(), tab) << "Cmd+" << digit << " in the default order";
    }
}

TEST_F(BottomDockPositionalKeysTest, ReorderingTheTabsChangesWhichViewEachNumberOpensAndWhatTheHintsShow) {
    EXPECT_EQ(hintFor(Tab::Timeline), "Cmd + 1");
    EXPECT_EQ(hintFor(Tab::Mixer), "Cmd + 2");

    dock().reorderTabsForTest(Tab::Timeline, Tab::MidiRemote); // order: Controllers, Mixer, Timeline

    EXPECT_EQ(dock().keyPositionOf(Tab::MidiRemote), 1);
    EXPECT_EQ(dock().keyPositionOf(Tab::Timeline), 3);
    EXPECT_EQ(hintFor(Tab::MidiRemote), "Cmd + 1") << "the hint overlay's key is the one that opens the tab";
    EXPECT_EQ(hintFor(Tab::Timeline), "Cmd + 3");
    EXPECT_TRUE(press(cmd('1')));
    EXPECT_EQ(dock().getActiveTab(), Tab::MidiRemote);
    EXPECT_TRUE(press(cmd('3')));
    EXPECT_EQ(dock().getActiveTab(), Tab::Timeline);

    // The swap icon's tooltip names the swap key of the tab's new place.
    EXPECT_TRUE(dock()
                    .getSwapIcon(Tab::Timeline)
                    .getTooltip()
                    .contains(ShortcutManager::keyPressToDisplayString(juce::KeyPress('3', kCmdAlt, 0))));
    EXPECT_TRUE(dock()
                    .getSwapIcon(Tab::MidiRemote)
                    .getTooltip()
                    .contains(ShortcutManager::keyPressToDisplayString(juce::KeyPress('1', kCmdAlt, 0))));

    ASSERT_TRUE(press(juce::KeyPress('1', kCmdAlt, 0)));
    EXPECT_EQ(dock().getMainView(), Tab::MidiRemote) << "Opt+Cmd+1 swaps the tab now first in the strip";
}

TEST_F(BottomDockPositionalKeysTest, ASwapRenumbersTheTabsAndTheMainViewHasNoNumberButCmdZero) {
    ASSERT_TRUE(press(juce::KeyPress('2', kCmdAlt, 0))); // Mixer into the main area; the Canvas takes its place
    EXPECT_EQ(dock().getMainView(), Tab::Mixer);
    EXPECT_EQ(dock().keyPositionOf(Tab::Mixer), 0);
    EXPECT_EQ(dock().keyPositionOf(Tab::Timeline), 1);
    EXPECT_EQ(dock().keyPositionOf(Tab::Canvas), 2);
    EXPECT_EQ(dock().keyPositionOf(Tab::MidiRemote), 3);
    EXPECT_EQ(hintFor(Tab::Canvas), "Cmd + 2");
    EXPECT_TRUE(press(cmd('2')));
    EXPECT_EQ(dock().getActiveTab(), Tab::Canvas);
}

TEST_F(BottomDockPositionalKeysTest, CmdZeroFocusesTheMainViewAndRaisesTheMainWindow) {
    ASSERT_TRUE(press(juce::KeyPress('1', kCmdAlt, 0))); // Timeline into the main area
    std::vector<juce::String> focused;
    mc_->getFocusRegionsForTest().onRegionFocused = [&](const juce::String& id) { focused.push_back(id); };
    const int raised = mc_->getMainWindowRaiseCountForTest();

    EXPECT_TRUE(press(cmd('0')));

    EXPECT_EQ(focused, std::vector<juce::String>{"timeline"});
    EXPECT_EQ(mc_->getMainWindowRaiseCountForTest(), raised + 1);
}

TEST_F(BottomDockPositionalKeysTest, ATargetThatIsNotDetachedRaisesTheMainWindowAndADetachedOneDoesNot) {
    int raised = mc_->getMainWindowRaiseCountForTest();
    EXPECT_TRUE(press(cmd('2'))); // a docked tab
    EXPECT_EQ(mc_->getMainWindowRaiseCountForTest(), ++raised);
    EXPECT_TRUE(press(juce::KeyPress('3', kCmdAlt, 0))); // a swap
    EXPECT_EQ(mc_->getMainWindowRaiseCountForTest(), ++raised);

    dock().getTimelineHost().setDetached(true);
    ASSERT_EQ(dock().keyPositionOf(Tab::Timeline), 1) << "a tab in its own window keeps its number";
    EXPECT_TRUE(press(cmd('1')));
    EXPECT_EQ(mc_->getMainWindowRaiseCountForTest(), raised) << "its own window is raised instead";
    dock().getTimelineHost().setDetached(false);
}

TEST_F(BottomDockPositionalKeysTest, TheKeysReachTheActionsFromInsideADetachedPanelsWindow) {
    dock().getMixerHost().setDetached(true);
    auto* window = dock().getMixerHost().getDetachedWindowForTest();
    ASSERT_NE(window, nullptr);
    const int raised = mc_->getMainWindowRaiseCountForTest();

    EXPECT_TRUE(window->keyPressed(cmd('3'))); // the real key path through the window's keyPressed
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(dock().getActiveTab(), Tab::MidiRemote);
    EXPECT_EQ(mc_->getMainWindowRaiseCountForTest(), raised + 1) << "the app window comes forward over the window";

    EXPECT_TRUE(window->keyPressed(cmd('0')));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(mc_->getMainWindowRaiseCountForTest(), raised + 2);
    EXPECT_TRUE(dock().getMixerHost().isDetached());
    dock().getMixerHost().setDetached(false);
}
