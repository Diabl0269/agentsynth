// MainComponentFocusCommandRows.cpp -- the focus-region, open-context-menu and help rows of the
// MainComponent command table (MainComponentCommandTable.cpp appends them in menu order).
#include "Branding.h"
#include "MainComponent.h"
#include "MainComponentCommandPalette.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/AddList/AddList.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Library/LibrarySidebarViews.h"

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
        {AppCommands::focusTransportBar,
         "Focus Transport Bar",
         "Open (if needed) the Timeline and put its key ring on the transport bar's Play button",
         "General",
         "focusTransportBar",
         {},
         [](MainComponent& m) {
             m.focusRegions_.focusRegionById("timeline");
             // A closed dock slides open first and the transport bar has no bounds until the slide lands; the ring
             // can only sit on a control that shows.
             m.timelineSlide_.whenSettled(
                 [safe = juce::Component::SafePointer<MainComponent>(&m), sliding = m.timelineSlide_.isMoving()] {
                     // A slide closed again before it landed leaves nothing to ring.
                     if (safe != nullptr && (!sliding || safe->isBottomDockVisible))
                         safe->timelinePanel.focusTransportBar();
                 });
             return true;
         }},
        {AppCommands::focusLibrary,
         "Focus Library",
         "Open (if needed) and focus the Library",
         "General",
         "focusLibrary",
         {},
         [](MainComponent& m) {
             m.focusRegions_.focusRegionById("library");
             return true;
         }},
        {AppCommands::focusLibrarySearch,
         "Focus Library Search",
         "Open (if needed) and focus the search field of the Library view that is showing",
         "General",
         "focusLibrarySearch",
         {},
         [](MainComponent& m) {
             // Deliberately NOT focusRegions_.focusRegionById("library") -- that grabs the region
             // ROOT (the sidebar column), and this shortcut's whole point is to land on the search field of
             // the view showing. A closed column opens on the view it last showed.
             if (!m.isLibraryVisible)
                 m.setLibraryVisible(true);
             m.librarySidebar_->focusShowingViewSearch();
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
        {AppCommands::recolourFocusedItem, "Recolour Focused Item",
         "Open the colour picker of the item that has keyboard focus", "General", "recolourFocusedItem",
         [](const MainComponent& m) { return m.isWelcomeScreenHidden(); },
         [](MainComponent& m) { return synth::ui::openColourForFocusedComponent(m.getFocusedComponentForMenu()); }},
        // With keyboard focus on the canvas (or its toolbar) it opens the Add list from the canvas toolbar's Add
        // button.
        // Otherwise it opens the Timeline if needed (focusRegionById runs the region's open hook), then the list from
        // the Timeline's Add button once the slide has settled.
        {AppCommands::openAddTrackMenu, "Add...",
         "Open the Add list of tracks, modules and presets from the focused view's Add button", "General",
         "openAddTrackMenu", [](const MainComponent& m) { return m.isWelcomeScreenHidden(); },
         [](MainComponent& m) {
             const auto* focused = m.getFocusedComponentForMenu();
             const auto holdsFocus = [focused](const juce::Component& c) {
                 return focused != nullptr && (focused == &c || c.isParentOf(focused));
             };
             if ((holdsFocus(m.graphEditor) || holdsFocus(m.canvasToolbar_)) && m.canvasToolbar_.isVisible()) {
                 m.canvasToolbar_.focusSlot(synth::ui::CanvasToolbar::Add);
                 m.openAddList(synth::ui::AddListOrigin::Canvas, m.canvasToolbar_.addButton(), true);
                 return true;
             }
             m.focusRegions_.focusRegionById("timeline");
             // The list anchors under the Timeline's Add button, which has no place until the show slide lands.
             m.timelineSlide_.whenSettled(
                 [safe = juce::Component::SafePointer<MainComponent>(&m), sliding = m.timelineSlide_.isMoving()] {
                     // A slide closed again before it landed leaves nothing to anchor to.
                     if (safe != nullptr && (!sliding || safe->isBottomDockVisible))
                         safe->timelinePanel.openAddTrackMenuFromShortcut();
                 });
             return true;
         }},
        // The same action as the top bar's Feedback button, so the chord works from the canvas, timeline and mixer.
        {AppCommands::openFeedback,
         "Send Feedback",
         "Open the send-feedback window",
         "General",
         "openFeedback",
         {},
         [](MainComponent& m) {
             if (m.feedbackButton.onClick)
                 m.feedbackButton.onClick();
             return true;
         }},
        // The same action as the top bar's Search button; pressed again while the palette is open, it closes it.
        {AppCommands::openCommandPalette,
         "Search Everything...",
         "Open the command palette: find and run any action, view, module, track, marker or setting",
         "General",
         "openCommandPalette",
         {},
         [](MainComponent& m) {
             MainComponentCommandPalette::toggle(m);
             return true;
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
         "Help build Agent Synth...",
         "Agent Synth is free and open source, built by one person. Contributing helps me spend more time building it. "
         "Opens agentsynth.app/contribute in your browser.",
         "Help",
         nullptr,
         {},
         [](MainComponent& m) {
             m.openContributePage();
             return true;
         }},
        // Menu-only, always active, no chord. Opens the community Discord page in the browser.
        {AppCommands::joinDiscord,
         "Join the Discord...",
         "Opens the Agent Synth Discord in your browser: share patches, ask questions, give feedback.",
         "Help",
         nullptr,
         {},
         [](MainComponent& m) {
             m.urlOpener_(juce::URL(synth::branding::kDiscordUrl));
             return true;
         }},
    };
}
