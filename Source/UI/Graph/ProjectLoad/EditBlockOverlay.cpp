// EditBlockOverlay.cpp -- following the covered surface, refusing clicks, passing scroll and zoom through.

#include "EditBlockOverlay.h"

namespace synth::ui {

EditBlockOverlay::EditBlockOverlay(juce::Component& covered, std::function<void()> onRefused)
    : covered_(covered)
    , onRefused_(std::move(onRefused)) {
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    covered_.addComponentListener(this);
}

EditBlockOverlay::~EditBlockOverlay() { covered_.removeComponentListener(this); }

void EditBlockOverlay::setBlocking(bool blocking) {
    if (blocking) {
        follow();
        toFront(false);
    }
    setVisible(blocking);
}

void EditBlockOverlay::follow() {
    auto* parent = getParentComponent();
    if (parent == nullptr || !parent->isParentOf(&covered_))
        return;
    setBounds(parent->getLocalArea(covered_.getParentComponent(), covered_.getBounds()));
}

void EditBlockOverlay::mouseDown(const juce::MouseEvent&) {
    if (onRefused_)
        onRefused_();
}

// The deepest component of the covered surface under the pointer; its own wheel handling (or its parents', as JUCE
// passes an unhandled wheel up) scrolls exactly as if the overlay were not there.
juce::Component& EditBlockOverlay::targetUnder(const juce::MouseEvent& e) {
    const auto local = covered_.getLocalPoint(this, e.position).roundToInt();
    auto* target = covered_.getComponentAt(local);
    return target != nullptr ? *target : covered_;
}

void EditBlockOverlay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    auto& target = targetUnder(e);
    target.mouseWheelMove(e.getEventRelativeTo(&target), wheel);
}

void EditBlockOverlay::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    auto& target = targetUnder(e);
    target.mouseMagnify(e.getEventRelativeTo(&target), scaleFactor);
}

std::unique_ptr<juce::AccessibilityHandler> EditBlockOverlay::createAccessibilityHandler() {
    return createIgnoredAccessibilityHandler(*this);
}

} // namespace synth::ui
