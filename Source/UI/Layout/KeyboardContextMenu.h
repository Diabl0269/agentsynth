#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Opening a surface's right-click menu from the keyboard (the rebindable "openContextMenu" action,
// Shift+F10 by default). A surface opts in by implementing KeyboardContextMenuProvider on a
// component that can be, or contains, the keyboard-focused component.
namespace synth::ui {

struct KeyboardContextMenuProvider {
    virtual ~KeyboardContextMenuProvider() = default;

    /** Opens the menu the surface's right-click would open for its keyboard-focused item, anchored
     *  at that item. Returns false when there is nothing to open a menu for (no item focused, or
     *  the item has no menu), which leaves the key unhandled. Message thread only. */
    virtual bool showContextMenuForKeyboardFocus() = 0;
};

/** Asks the nearest provider at or above `focused` to open its menu. Only the nearest provider is
 *  consulted: its answer is final, an outer provider is never tried after it declines. Returns
 *  false when `focused` is null or no ancestor is a provider. */
inline bool openContextMenuForFocusedComponent(juce::Component* focused) {
    for (auto* c = focused; c != nullptr; c = c->getParentComponent())
        if (auto* provider = dynamic_cast<KeyboardContextMenuProvider*>(c))
            return provider->showContextMenuForKeyboardFocus();
    return false;
}

/** True when `key` is bound to the open-context-menu action right now, so a top-level window with
 *  no command target of its own (a detached panel) can honour a user's rebind the same way the
 *  command table does. */
inline bool isOpenContextMenuKeyPress(const juce::KeyPress& key, const ShortcutManager& shortcutManager) {
    return shortcutManager.getActionsForKeyPress(key).contains("openContextMenu");
}

} // namespace synth::ui
