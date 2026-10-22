// MainComponentLibraryViews.cpp -- the left column's two views as MainComponent drives them: the Cmd+B / Cmd+Opt+B
// state machine and the toolbar toggle's tooltip. The views themselves live in LibrarySidebarViews.
#include "MainComponent.h"

// Closed: open on the view asked for. Open on the other view: switch to it and stay open. Open on this view: close.
void MainComponent::toggleLibraryView(synth::ui::SidebarView view) {
    if (!isLibraryVisible) {
        librarySidebar_->setView(view, false); // the column itself slides open; the view is simply there
        setLibraryVisible(true);
    } else if (librarySidebar_->getView() != view) {
        librarySidebar_->setView(view, true);
    } else {
        setLibraryVisible(false);
    }
}

// The toolbar button shows or hides the column on the view it last showed, and its tooltip names both view keys.
void MainComponent::refreshLibraryToggleTooltip() {
    const auto key = [this](const char* action) {
        return ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(action));
    };
    const auto modulesKey = key("toggleLibrary");
    const auto soundsKey = key("toggleSoundBrowser");
    librarySidebar_->setShortcutHints(modulesKey, soundsKey);
    juce::String keys;
    if (modulesKey.isNotEmpty())
        keys += modulesKey + " Modules";
    if (soundsKey.isNotEmpty())
        keys += (keys.isEmpty() ? "" : ", ") + soundsKey + " Sounds";
    toggleLibraryButton.setTooltip(
        synth::ui::formatShortcutHint(isLibraryVisible ? "Hide Library" : "Show Library", keys));
}
