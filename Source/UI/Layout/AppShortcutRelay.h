#pragma once

// AppShortcutRelay.h -- lets the app's shortcuts (Space, Cmd+Z, ...) work while a tool window (shape editor, EQ,
// modulation curve) has the keyboard. The window is its own top-level window, so a key it does not use never
// reaches the main window.
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class ShortcutManager;

namespace synth::ui {

/** True when `key` is bound to an app command that makes sense from a tool window. A key bound to no command (the
 *  arrows, Delete and Backspace are not commands) is not, and neither is any command that edits or navigates the
 *  canvas selection (copy, paste, cut, duplicate, select all, repeat, group, ungroup, fold, auto-arrange, save snippet,
 *  rename and replace, next/previous module or track, zoom) or moves focus between the main window's regions. */
bool isWindowForwardableShortcut(const ShortcutManager& shortcuts, const juce::KeyPress& key);

/** The `onAppShortcut` of a window opened from `anchor` (the canvas or a card on it, or any panel of the main
 *  window): hands a forwardable key to the main window's command target, one message-loop turn later, and
 *  returns true for it; false for any other key and when the anchor, its canvas or the target is gone.
 *  `onUndoRedo`, when given, is called with false at once for a key bound to undo or redo (before the app sees it), and
 *  with true after the target has handled it. */
std::function<bool(const juce::KeyPress&)> makeAppShortcutRelay(juce::Component* anchor,
                                                                std::function<void(bool afterwards)> onUndoRedo = {});

/** The app's shortcut table as `anchor` (the canvas, a card on it or a main-window panel) sees it; null when none is
 *  reachable. */
const ShortcutManager* shortcutsReachableFrom(juce::Component* anchor);

/** The Snap key of a tool window with a Snap button: the app's "timelineSnapToggle" binding (J by default, and a
 *  rebind follows), looked up through the anchor the window was opened from. With no table reachable (a window in a
 *  test) the default J stands. The window flips its own Snap and says so; the key never reaches the timeline. */
class WindowSnapKey {
public:
    void setAnchor(juce::Component* anchor) { anchor_ = anchor; }
    bool matches(const juce::KeyPress& key) const;
    /** `text` with the bound key named after it, for the Snap button's tooltip; no key named when it is unbound. */
    juce::String tooltip(const juce::String& text) const;
    /** What a press says: "Snap on" or "Snap off". */
    static juce::String message(bool on) { return on ? "Snap on" : "Snap off"; }

private:
    juce::Component::SafePointer<juce::Component> anchor_;
};

} // namespace synth::ui
