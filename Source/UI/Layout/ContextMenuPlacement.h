#pragma once

// ContextMenuPlacement.h -- where a right-click (pointer-driven) context menu opens.
//
// A plain juce::PopupMenu::Options() targets an EMPTY rectangle at the mouse, and JUCE's placement
// for an empty target is "tend towards the nearer screen half": a pointer on the right half of the
// screen gets a menu that opens to the LEFT of it. A target with a non-empty area makes JUCE align
// the menu's top-left to the target instead, and clamp it at the screen edge. So every
// pointer-driven context menu goes through these helpers; anchored dropdowns keep
// withTargetComponent(&button), and keyboard-opened menus keep their anchor.

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** Options for a menu whose top-left corner sits at `screenPoint` (the pointer). */
inline juce::PopupMenu::Options contextMenuOptionsAtPoint(juce::Point<int> screenPoint) {
    return juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPoint.x, screenPoint.y, 1, 1));
}

/** Options for a menu opened by a right-click: top-left at the pointer. */
inline juce::PopupMenu::Options contextMenuOptionsAtPointer() {
    return contextMenuOptionsAtPoint(juce::Desktop::getMousePosition());
}

/** A context menu that is opened either by the pointer (no `keyboardAnchor`) or from the keyboard
 *  (anchored at the focused item's screen area). */
inline juce::PopupMenu::Options contextMenuOptions(const std::optional<juce::Rectangle<int>>& keyboardAnchor) {
    return keyboardAnchor ? juce::PopupMenu::Options().withTargetScreenArea(*keyboardAnchor)
                          : contextMenuOptionsAtPointer();
}

} // namespace synth::ui
