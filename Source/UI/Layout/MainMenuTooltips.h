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
// Sets toolTip on the items of the main menu now, and on every item added to a menu later: JUCE refills a menu when
// it opens and replaces it outright when the menu model changes, and either drops anything set earlier. Call after
// MenuBarModel::setMacMainMenu.
void installMainMenuTooltips(MenuTooltips tooltips);

// Stops watching added items and forgets the table; the app never needs it (the hints live as long as the menu), a
// test calls it so nothing it installed outlives it.
void uninstallMainMenuTooltips();

// The toolTip macOS holds for `itemTitle` in the main-menu entry `menuTitle`, read after the menu is refreshed the
// way opening it refreshes it (when it has a refresh delegate); empty if there is none.
juce::String mainMenuTooltipForTest(const juce::String& menuTitle, const juce::String& itemTitle);
#else
inline void installMainMenuTooltips(MenuTooltips) {}
inline void uninstallMainMenuTooltips() {}
#endif

} // namespace synth::ui
