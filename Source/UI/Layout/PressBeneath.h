#pragma once

// PressBeneath.h -- a popup's native window is a rectangle, but what it draws is not: a call-out's arrow strip and a
// panel's shadow margin are inside the window and outside the shape. A press there belongs to no part of the popup, so
// it must act like a press on another window: it closes the popup and lands on what is under it
// (docs/layout/card-popovers.md#a-press-outside-what-it-draws).
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** A press the popup's window took outside the shape it draws. The window calls pressed() from mouseDown for such a
 *  press, and released() from mouseUp; when released() says true, the window closes itself and calls land(). */
class PressOutsideShape {
public:
    void pressed(const juce::MouseEvent& e);
    /** True once, on the release of a press that pressed() took. */
    bool released();
    /** Clicks what is under the press in any window but `popupWindow` (still fading), after the current event. */
    void land(const juce::Component& popupWindow) const;

private:
    bool held_ = false;
    juce::Point<int> screen_;
    juce::ModifierKeys mods_;
};

/** Sends a press and a release with `mods` at `screen` to the component under it in the topmost window other than
 *  `except`, through that window's peer (the path a real click takes), on the next message-loop turn. */
void clickBeneath(juce::Point<int> screen, juce::ModifierKeys mods, const juce::Component* except);

} // namespace synth::ui
