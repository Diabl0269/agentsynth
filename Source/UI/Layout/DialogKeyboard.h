#pragma once

#include "UI/Layout/PopupMotion.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// Takes the invisible Tab stops out of a juce::TextEditor: every component inside the editor
// (its internal viewport and anything it hosts) stops asking for keyboard focus, so Tab and
// Shift+Tab land on the editor once and then on the next real control. Call it once after
// constructing each text field a dialog, tab or popup shows.
inline void removeHiddenTabStops(juce::Component& editor) {
    for (auto* child : editor.getChildren()) {
        child->setWantsKeyboardFocus(false);
        removeHiddenTabStops(*child);
    }
}

// Closes the juce::CallOutBox or juce::DialogWindow that hosts `content` the way its own close
// affordance does, fading it out first (PopupMotion::dismiss). Returns false when `content` has no such host (a
// headless test, an embedded panel), in which case nothing happens.
inline bool closeHostingWindow(juce::Component& content) {
    if (auto* box = content.findParentComponentOfClass<juce::CallOutBox>()) {
        PopupMotion::dismissCallOut(*box);
        return true;
    }
    if (auto* dialog = content.findParentComponentOfClass<juce::DialogWindow>()) {
        PopupMotion::dismiss(*dialog, [safe = juce::Component::SafePointer<juce::DialogWindow>(dialog)] {
            if (auto* d = safe.getComponent())
                d->closeButtonPressed();
        });
        return true;
    }
    return false;
}

// Offers Escape to each ancestor of `from` in turn, as a key press from any other control would
// travel, until one's keyPressed() handles it. Returns whether one did.
inline bool forwardEscapeToParents(juce::Component& from) {
    const juce::KeyPress escape(juce::KeyPress::escapeKey);
    for (auto* parent = from.getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
        if (parent->keyPressed(escape))
            return true;
    return false;
}

// A juce::TextEditor swallows Escape before it can reach the dialog around it. Call this on a text
// field inside a dialog so Escape travels up to the dialog instead (see forwardEscapeToParents).
inline void bubbleEscapeToParents(juce::TextEditor& editor) {
    editor.onEscapeKey = [&editor] { forwardEscapeToParents(editor); };
}

// Keeps the control that takes keyboard focus inside `viewport` on screen: when focus moves to a
// descendant of the viewed component that is scrolled out of view, the viewport scrolls vertically
// just far enough to reveal it. Own one per scrolling tab; it listens for as long as it lives.
class ScrollIntoViewOnFocus : private juce::FocusChangeListener {
public:
    explicit ScrollIntoViewOnFocus(juce::Viewport& viewportToFollow)
        : viewport_(viewportToFollow) {
        juce::Desktop::getInstance().addFocusChangeListener(this);
    }
    ~ScrollIntoViewOnFocus() override { juce::Desktop::getInstance().removeFocusChangeListener(this); }

    // Reveals `focused` if it is a descendant of the viewport's content. Public so a test can drive
    // it without a native window moving real focus.
    void reveal(juce::Component* focused) {
        auto* content = viewport_.getViewedComponent();
        if (focused == nullptr || content == nullptr || focused == content || !content->isParentOf(focused))
            return;
        const auto area = content->getLocalArea(focused, focused->getLocalBounds());
        const int viewTop = viewport_.getViewPositionY();
        const int viewHeight = viewport_.getMaximumVisibleHeight();
        int newTop = viewTop;
        if (area.getY() < viewTop)
            newTop = area.getY();
        else if (area.getBottom() > viewTop + viewHeight)
            newTop = area.getBottom() - viewHeight;
        if (newTop != viewTop)
            viewport_.setViewPosition(viewport_.getViewPositionX(), newTop);
    }

private:
    void globalFocusChanged(juce::Component* focused) override { reveal(focused); }

    juce::Viewport& viewport_;
};

} // namespace synth::ui
