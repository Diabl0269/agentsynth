#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// DialogMenuKeys.h: the macOS menu keys (Cmd+Q, Cmd+H) inside a dialog PopupMotion::launchDialog opened.
// While any modal component is up, JUCE's macOS peer marks every key-down it did not use as used, so the key never
// reaches the native menu that owns it and Quit and Hide do nothing. A dialog's key listener sees such a
// key (it bubbled up unused) and does the menu's job itself.
namespace synth::ui::dialog_menu_keys {

// Cmd+M is not here: the app binds it (Toggle Mod Matrix), so the menu never owned it.
struct Actions {
    std::function<void()> quit; // the app's own quit request (asks about unsaved changes)
    std::function<void()> hide; // hides the application
};

/** Does the menu's job for Cmd+Q / Cmd+H pressed in `window`. True when it did; always false off macOS, where
 *  the native menu is not involved. */
bool handle(const juce::KeyPress& key, juce::DocumentWindow& window);

/** The actions handle() runs: the real ones by default. Tests replace them and restore the result of this call. */
Actions& actions();
Actions defaultActions();

} // namespace synth::ui::dialog_menu_keys
