#pragma once

#include "UI/Layout/NonModalLabel.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The one place a drag-to-move gesture picks its mouse cursor: the grabbing hand for a move, the
// copy cursor for an Option/Alt copy. A small grab handle sets dragGrabCursor() permanently; a
// reorderable tab, row or column header calls followDragCursor() on every drag event, and a
// tool-driven canvas calls showDragCursor() once its move has really started; both call
// endDragCursor() when the gesture ends.
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

/** For a reorder handle that keeps the normal arrow on hover and press: call on every mouseDrag with
 *  whether its owner's reorder is really dragging (past the threshold, not cancelled by Esc). Call
 *  endDragCursor() on release, before any hook that can destroy the handle. */
inline void followDragCursor(juce::Component& handle, bool dragging) {
    if (dragging)
        showDragCursor(handle);
    else
        endDragCursor(handle);
}

/** A label covering part of a drag handle that shows the handle's cursor, so the two read as one. */
class CursorDelegatingLabel : public NonModalLabel {
public:
    void setCursorSource(juce::Component* source) noexcept { cursorSource_ = source; }
    juce::MouseCursor getMouseCursor() override {
        return cursorSource_ != nullptr ? cursorSource_->getMouseCursor() : juce::Label::getMouseCursor();
    }

private:
    juce::Component::SafePointer<juce::Component> cursorSource_;
};

} // namespace synth::ui
