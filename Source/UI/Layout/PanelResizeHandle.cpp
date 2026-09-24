// Concern: FRO231 -- PanelResizeHandle's paint, hover state and the screen-coordinate drag. Lifted
// out of TimelinePanelComponent (where it was a nested class) so the bottom dock can own ONE
// handle regardless of which tab is showing.
#include "PanelResizeHandle.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// The drag is measured in SCREEN coordinates against the owner's bottom edge, not as a delta: the
// owner moves its top edge under the cursor on every callback, so a component-relative delta would
// chase itself. Both callbacks report the owner's DESIRED height; clamping belongs to whoever
// owns the layout.
PanelResizeHandle::PanelResizeHandle(juce::Component& owner)
    : owner_(owner) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void PanelResizeHandle::paint(juce::Graphics& g) {
    // Dynamic_cast with literal fallbacks: a headless test has no themed LookAndFeel installed.
    juce::Colour line;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        line = isHighlighted() ? c.accent : c.border;
    } else {
        line = isHighlighted() ? juce::Colours::white : juce::Colours::grey;
    }

    // Idle: a plain border hairline along the owner's top edge, so nothing new is visible until
    // the pointer arrives. Hovered/dragging: the hairline brightens and the strip picks up a faint
    // wash, which is the whole affordance.
    if (isHighlighted())
        g.fillAll(line.withAlpha(0.18f));
    g.setColour(line);
    g.fillRect(0, 0, getWidth(), 1);
}

void PanelResizeHandle::mouseEnter(const juce::MouseEvent&) {
    if (hovered_)
        return; // repaint only on a CHANGE
    hovered_ = true;
    repaint();
}

void PanelResizeHandle::mouseExit(const juce::MouseEvent&) {
    if (!hovered_)
        return;
    hovered_ = false;
    // Highlighted while dragging too, so the hairline doesn't dim mid-drag when the pointer leaves
    // the strip (it does, the moment the owner grows under it): only repaint when that's over.
    if (!dragging_)
        repaint();
}

int PanelResizeHandle::desiredHeightFor(const juce::MouseEvent& e) const {
    // Absolute, not a delta: the owner moves its top edge (and this handle with it) on every
    // callback, so only the owner's FIXED bottom edge is a stable reference. The grab offset keeps
    // the pixel that was grabbed under the cursor for the whole gesture.
    const int topY = e.getScreenPosition().y - grabOffsetY_;
    return owner_.getScreenBounds().getBottom() - topY;
}

void PanelResizeHandle::mouseDown(const juce::MouseEvent& e) {
    const bool wasHighlighted = isHighlighted();
    dragging_ = true;
    moved_ = false;
    grabOffsetY_ = (int)e.getEventRelativeTo(&owner_).position.y;
    lastDesiredHeight_ = desiredHeightFor(e);
    if (!wasHighlighted)
        repaint();
}

void PanelResizeHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    moved_ = true;
    lastDesiredHeight_ = desiredHeightFor(e);
    if (onResize)
        onResize(lastDesiredHeight_);
}

void PanelResizeHandle::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (!hovered_)
        repaint(); // the highlight only changes when the pointer has already left
    // Persist point for the owner -- and only for a real drag: a stray click must not write settings.
    if (moved_ && onResizeCommitted)
        onResizeCommitted(lastDesiredHeight_);
}

} // namespace synth::ui
