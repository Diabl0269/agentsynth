// MainComponentFocusCommandRows.cpp -- the focus-region, open-context-menu and help rows of the
// MainComponent command table (MainComponentCommandTable.cpp appends them in menu order).
#include "MainComponent.h"
#include "UI/Layout/KeyboardContextMenu.h"

std::vector<MainComponent::CommandSpec> MainComponent::buildFocusAndHelpCommandRows() {
    return {
        // Suppressed while the launch overlay is up front -- every region it would cycle to
        // is sitting behind it, so there is nowhere useful for Tab to land.
        {AppCommands::focusNextRegion, "Focus Next Region",
         "Move keyboard focus to the next open panel (Library, Canvas, Timeline, AI Panel, Mod Matrix)", "General",
         "focusNextRegion", [](const MainComponent& m) { return m.isWelcomeScreenHidden(); },
         [](MainComponent& m) {
             m.focusRegions_.cycleFocus(true);
             return true;
         }},
        {AppCommands::focusPrevRegion, "Focus Previous Region", "Move keyboard focus to the previous open panel",
         "General", "focusPrevRegion", [](const MainComponent& m) { return m.isWelcomeScreenHidden(); },
         [](MainComponent& m) {
             m.focusRegions_.cycleFocus(false);
             return true;
         }},
        {AppCommands::focusTimeline,
         "Focus Timeline",
         "Open (if needed) and focus the Timeline panel",
         "General",
         "focusTimeline",
         {},
         [](MainComponent& m) {
             m.focusRegions_.focusRegionById("timeline");
             return true;
         }},
        {AppCommands::focusLibrary,
         "Focus Library",
         "Open (if needed) and focus the Module Library",
         "General",
         "focusLibrary",
         {},
         [](MainComponent& m) {
             m.focusRegions_.focusRegionById("library");
             return true;
         }},
        {AppCommands::focusLibrarySearch,
         "Focus Library Search",
         "Open (if needed) and focus the Module Library's search field",
         "General",
         "focusLibrarySearch",
         {},
         [](MainComponent& m) {
             // Deliberately NOT focusRegions_.focusRegionById("library") -- that grabs the region
             // ROOT (moduleLibrary itself, per FocusRegion.h's contract), and this shortcut's whole
             // point is to land on the search field specifically. Same "open if closed" behaviour
             // as focusLibrary.
             if (!m.isLibraryVisible)
                 m.setLibraryVisible(true);
             m.moduleLibrary.focusSearchField();
             return true;
         }},
        // Same gate as the focus cycle: with the launch overlay up front the focused thing is
        // behind it and a menu would open over nothing.
        {AppCommands::openContextMenu, "Open Context Menu",
         "Open the right-click menu of the item that has keyboard focus", "General", "openContextMenu",
         [](const MainComponent& m) { return m.isWelcomeScreenHidden(); },
         [](MainComponent& m) {
             return synth::ui::openContextMenuForFocusedComponent(m.getFocusedComponentForMenu());
         }},
        // Registered unconditionally (unlike checkForUpdates below) -- neither command needs OS
        // integration, only ownedAudioEngine != nullptr, which is fixed for this instance's whole
        // lifetime.
        {AppCommands::showWelcomeScreen, "Show Welcome Screen", "Reopen the welcome screen", "Help", nullptr,
         [](const MainComponent& m) { return m.ownedAudioEngine != nullptr; },
         [](MainComponent& m) {
             m.showWelcomeScreen();
             return true;
         }},
        {AppCommands::whatsNew,
         "What's New...",
         "See what's changed recently",
         "Help",
         nullptr,
         {},
         [](MainComponent& m) {
             m.showWhatsNewDialog();
             return true;
         }},
#if JUCE_MAC || JUCE_WINDOWS
        {AppCommands::checkForUpdates, "Check for Updates...", "Check for a newer version of the app", "Help", nullptr,
         [](const MainComponent& m) { return m.updateManager.isAvailable(); },
         [](MainComponent& m) {
             m.updateManager.checkForUpdates();
             return true;
         }},
#endif
        // Menu-only, always active, no chord. Opens the contribute page in the browser.
        {AppCommands::contribute,
         "Contribute to Agent Synth...",
         "Opens agentsynth.app/contribute in your browser: ways to help build Agent Synth.",
         "Help",
         nullptr,
         {},
         [](MainComponent& m) {
             m.openContributePage();
             return true;
         }},
    };
}
