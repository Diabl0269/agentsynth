#pragma once

// CardCallOut.h -- the popovers a card opens (a sequencer's Randomize panel, its key picker). A parentless
// juce::CallOutBox is its own top-level window, so a key its content does not use never reaches the main window:
// Cmd+Z and Space would die there. launchCardCallOut opens the popover and hands the app's shortcuts to the main
// window, the way the shape editor window does (makeAppShortcutRelay); keys the content uses (typing in a field,
// Escape) stay with the content.
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** Opens `content` in a CallOutBox pointing at `area` (screen coordinates) and relays app shortcuts to the main window
 *  that holds `anchor` (a card or any component on it, or any panel of the main window: timeline, mixer, MIDI
 * Remote...). `closeOnUndo`: the content lists project state and does not implement AppUndoAware, so the popover closes
 *  after an undo or redo rather than show it stale. Returns the box (null only when a test launcher declines). */
juce::CallOutBox* launchCardCallOut(std::unique_ptr<juce::Component> content, juce::Rectangle<int> area,
                                    juce::Component* anchor, bool closeOnUndo = false);

/** Makes `box` relay forwardable app shortcuts (see isWindowForwardableShortcut) for a card `anchor`. A key listener on
 *  the box sees a key after the focused child and every component between it and the box have declined it. Frees
 *  itself when the box goes. launchCardCallOut calls this; tests call it on a box they build headless. */
void relayAppShortcutsFromCallOut(juce::CallOutBox& box, juce::Component* anchor, bool closeOnUndo = false);

namespace test_hooks {
/** When set, launchCardCallOut hands the content here instead of opening a window (a CallOutBox needs a desktop) and
 *  relays through the box the hook returns. */
/** What a real key press gives the box's key listeners: offers `key` to the relay `box` got from
 *  relayAppShortcutsFromCallOut, as the peer does after the focused child and its parents declined it. False when the
 *  box has no relay or the relay declines the key. */
bool offerKeyToCardCallOutForTest(juce::CallOutBox& box, const juce::KeyPress& key);

using CardCallOutLauncher = std::function<juce::CallOutBox*(std::unique_ptr<juce::Component>, juce::Rectangle<int>)>;
CardCallOutLauncher& cardCallOutLauncherForTest();
} // namespace test_hooks

} // namespace synth::ui
