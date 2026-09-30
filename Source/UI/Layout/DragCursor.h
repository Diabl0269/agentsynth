#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The one place a drag-to-move gesture picks its mouse cursor: the grabbing hand for a move, the
// copy cursor for an Option/Alt copy. A dedicated grab handle sets dragGrabCursor() permanently;
// a tool-driven canvas calls showDragCursor() once its move has really started and
// endDragCursor() with its tool cursor when the gesture ends.
// Rule and exclusions: docs/layout/animation.md#drag-and-drop-cursor
namespace synth::ui {

inline juce::MouseCursor dragGrabCursor() { return juce::MouseCursor(juce::MouseCursor::DraggingHandCursor); }

inline juce::MouseCursor dragCopyCursor() { return juce::MouseCursor(juce::MouseCursor::CopyingCursor); }

/** Switches `c` to the grab (or copy) cursor. setMouseCursor() refreshes the on-screen cursor
 *  itself, so this takes effect mid-gesture. */
inline void showDragCursor(juce::Component& c, bool copying = false) {
    c.setMouseCursor(copying ? dragCopyCursor() : dragGrabCursor());
}

/** Puts `c` back on the cursor it had before the drag (its tool cursor, or Normal). */
inline void endDragCursor(juce::Component& c, const juce::MouseCursor& rest = juce::MouseCursor::NormalCursor) {
    c.setMouseCursor(rest);
}

} // namespace synth::ui
