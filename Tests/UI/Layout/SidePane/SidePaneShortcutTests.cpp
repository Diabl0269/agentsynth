// SidePaneShortcutTests.cpp: the rebindable "toggleSidePane" action (Cmd+Shift+B) -- its default binding
// and registration, that it toggles the ACTIVE tab's pane, opens a hidden bottom panel first, and is a
// no-op on a tab without a pane.
#include "../../Mixer/MixerZonesTestRig.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {
using Tab = synth::ui::BottomDockComponent::Tab;

juce::KeyPress cmdShiftB() {
    return juce::KeyPress('b', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
}
} // namespace

TEST(SidePaneShortcutTests, TheActionIsRegisteredWithACmdShiftBDefault) {
    ShortcutManager shortcuts;
    EXPECT_EQ(shortcuts.getBinding("toggleSidePane"), cmdShiftB());
    EXPECT_EQ(AppCommands::getCommandForAction("toggleSidePane"), AppCommands::toggleSidePane);
    EXPECT_EQ(ShortcutManager::getActionDescription("toggleSidePane"), "Show/Hide Side Pane");
    EXPECT_NE(shortcuts.getBinding("toggleLibrary"), shortcuts.getBinding("toggleSidePane"))
        << "plain Cmd+B stays the library sidebar";
}

TEST(SidePaneShortcutTests, TheKeyIsBoundToTheActionAndTheActionTogglesTheActiveTabsPane) {
    MixerZonesRig r(1);
    r.mc.showBottomDockTab(Tab::Mixer);
    auto& pane = r.panel->getSidePane();
    ASSERT_TRUE(pane.isOpen());
    ASSERT_TRUE(r.mc.isBottomDockConfiguredVisible());

    // MainComponent::keyPressed resolves the chord through the shortcut table and dispatches the command
    // asynchronously, so the key is checked against the table and the command is invoked directly.
    const auto actions = r.mc.getShortcutManager().getActionsForKeyPress(cmdShiftB());
    EXPECT_TRUE(actions.contains("toggleSidePane"));

    EXPECT_TRUE(r.mc.getCommandManager().invokeDirectly(AppCommands::toggleSidePane, false));
    EXPECT_FALSE(pane.isOpen());
    EXPECT_TRUE(r.mc.isBottomDockConfiguredVisible()) << "hiding the pane never hides the panel";
    EXPECT_TRUE(r.mc.getCommandManager().invokeDirectly(AppCommands::toggleSidePane, false));
    EXPECT_TRUE(pane.isOpen());
}

TEST(SidePaneShortcutTests, ItShowsAHiddenBottomPanelFirstAndLeavesTheOpenPaneOpen) {
    MixerZonesRig r(1);
    r.mc.getBottomDock().setActiveTab(Tab::Mixer);
    ASSERT_FALSE(r.mc.isBottomDockConfiguredVisible()) << "the dock starts closed";
    auto& pane = r.panel->getSidePane();
    ASSERT_TRUE(pane.isOpen());

    r.mc.getCommandManager().invokeDirectly(AppCommands::toggleSidePane, false);
    EXPECT_TRUE(r.mc.isBottomDockConfiguredVisible());
    EXPECT_TRUE(pane.isOpen()) << "the press means 'let me see the pane', so it is not closed as it appears";

    r.mc.getBottomDock().setActiveTab(Tab::Mixer);
    pane.setOpen(false);
    r.mc.simulateToggleBottomPanelClick(); // hide the dock again
    ASSERT_FALSE(r.mc.isBottomDockConfiguredVisible());
    r.mc.getCommandManager().invokeDirectly(AppCommands::toggleSidePane, false);
    EXPECT_TRUE(r.mc.isBottomDockConfiguredVisible());
    EXPECT_TRUE(pane.isOpen()) << "a closed pane is opened along with the panel";
}

TEST(SidePaneShortcutTests, ItIsANoOpWhenTheActiveTabHasNoPane) {
    MixerZonesRig r(1);
    r.mc.getBottomDock().setActiveTab(Tab::Timeline);
    auto& dock = r.mc.getBottomDock();
    EXPECT_FALSE(dock.hasActiveSidePane());
    EXPECT_FALSE(dock.toggleActiveSidePane());
    ASSERT_FALSE(r.mc.isBottomDockConfiguredVisible());

    r.mc.getCommandManager().invokeDirectly(AppCommands::toggleSidePane, false);
    EXPECT_FALSE(r.mc.isBottomDockConfiguredVisible()) << "no pane, so nothing to show: the panel stays hidden";
    EXPECT_TRUE(r.panel->getSidePane().isOpen()) << "and the Mixer's pane is untouched";
}

TEST(SidePaneShortcutTests, TheViewMenuOffersShowHideSidePane) {
    MixerZonesRig r(1);
    bool found = false;
    for (const auto& spec : r.mc.getCommandTableForTest())
        if (spec.id == AppCommands::toggleSidePane) {
            found = true;
            EXPECT_EQ(std::string(spec.name), "Show/Hide Side Pane");
            EXPECT_EQ(std::string(spec.category), "View");
        }
    EXPECT_TRUE(found);
}
