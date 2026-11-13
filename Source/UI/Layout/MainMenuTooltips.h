#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// Menu-item hints for the macOS menu bar. JUCE's PopupMenu::Item has no tooltip, and juce_MainMenu_mac.mm never sets
// NSMenuItem.toolTip, so a menu item shows nothing on hover. On macOS the system menu can show one, so this sets it
// from each command's description. The Windows menu is a JUCE popup that cannot carry one: there the hint stays on
// the Welcome button and the Cmd+K row.
namespace synth::ui {

// Menu item title (the command's short name) -> its one-line hint.
using MenuTooltips = std::map<juce::String, juce::String>;

// Every registered command that has a description, keyed by the title addCommandItem gives its menu item.
inline MenuTooltips collectCommandTooltips(juce::ApplicationCommandManager& commands) {
    MenuTooltips tips;
    for (int i = 0; i < commands.getNumCommands(); ++i)
        if (const auto* info = commands.getCommandForIndex(i))
            if (info->description.isNotEmpty())
                tips[info->shortName] = info->description;
    return tips;
}

#if JUCE_MAC
// Sets toolTip on the items of the main menu now, and again each time macOS opens a menu: JUCE rebuilds a menu's
// items when it opens, which drops anything set earlier. Call after MenuBarModel::setMacMainMenu.
void installMainMenuTooltips(MenuTooltips tooltips);

// The toolTip macOS holds for `itemTitle` in the main-menu entry `menuTitle`, read after the menu is refreshed the
// way opening it refreshes it; empty if there is none.
juce::String mainMenuTooltipForTest(const juce::String& menuTitle, const juce::String& itemTitle);
#else
inline void installMainMenuTooltips(MenuTooltips) {}
#endif

} // namespace synth::ui
