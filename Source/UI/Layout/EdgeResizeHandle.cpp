// Concern: EdgeResizeHandle's hover/drag state, its hairline, and its optional keyboard path.
#include "UI/Layout/EdgeResizeHandle.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

EdgeResizeHandle::EdgeResizeHandle(Axis axis)
    : axis_(axis) {
    setMouseCursor(axis == Axis::Vertical ? juce::MouseCursor::UpDownResizeCursor
                                          : juce::MouseCursor::LeftRightResizeCursor);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void EdgeResizeHandle::setKeyboardFocusable(bool focusable) { setWantsKeyboardFocus(focusable); }

// Idle it draws nothing (the seam it sits on already has its own border); hovered or dragging, an
// accent hairline down its middle with a faint wash, which is the whole affordance.
void EdgeResizeHandle::paint(juce::Graphics& g) {
    juce::Colour accent = juce::Colours::white;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;
    if (isHighlighted()) {
        g.fillAll(accent.withAlpha(0.18f));
        g.setColour(accent);
        if (axis_ == Axis::Vertical)
            g.fillRect(0, getHeight() / 2, getWidth(), 1);
        else
            g.fillRect(getWidth() / 2, 0, 1, getHeight());
    }
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, 2.0f);
}

void EdgeResizeHandle::mouseEnter(const juce::MouseEvent&) {
    if (hovered_)
        return;
    hovered_ = true;
    repaint();
}

void EdgeResizeHandle::mouseExit(const juce::MouseEvent&) {
    if (!hovered_)
        return;
    hovered_ = false;
    if (!dragging_)
        repaint(); // stays lit mid-drag even once the strip has moved out from under the pointer
}

// Measured in SCREEN pixels from the press: the owner moves this strip on every step, so a
// component-relative position would chase itself.
void EdgeResizeHandle::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;
    dragging_ = true;
    moved_ = false;
    pressScreen_ = alongAxis(e.getScreenPosition());
    if (onDragStarted)
        onDragStarted();
    repaint();
}

void EdgeResizeHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    const int delta = alongAxis(e.getScreenPosition()) - pressScreen_;
    if (delta == 0 && !moved_)
        return;
    moved_ = true;
    if (onDragged)
        onDragged(delta);
}

void EdgeResizeHandle::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    repaint();
    if (moved_ && onDragEnded)
        onDragEnded(); // a stray click commits nothing
}

void EdgeResizeHandle::mouseDoubleClick(const juce::MouseEvent&) {
    if (onResetRequested)
        onResetRequested();
}

bool EdgeResizeHandle::keyPressed(const juce::KeyPress& key) {
    if (!getWantsKeyboardFocus() || key.getModifiers().isAnyModifierKeyDown())
        return false;
    const bool vertical = axis_ == Axis::Vertical;
    const int grow = vertical ? juce::KeyPress::downKey : juce::KeyPress::rightKey;
    const int shrink = vertical ? juce::KeyPress::upKey : juce::KeyPress::leftKey;
    if ((key.isKeyCode(grow) || key.isKeyCode(shrink)) && onKeyboardStep) {
        onKeyboardStep(key.isKeyCode(grow) ? 1 : -1);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey) && onResetRequested) {
        onResetRequested();
        return true;
    }
    return false;
}

void EdgeResizeHandle::focusGained(FocusChangeType) { repaint(); }
void EdgeResizeHandle::focusLost(FocusChangeType) { repaint(); }

} // namespace synth::ui
