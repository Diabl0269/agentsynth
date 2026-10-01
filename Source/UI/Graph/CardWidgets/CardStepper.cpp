// CardStepper.cpp -- a small integer as "-" value "+": the two titled buttons, the value label between
// them, the arrow keys while either button has focus, and each button's focus ring.
#include "CardStepper.h"
#include "UI/Layout/FocusRing.h"

namespace synth::ui {

namespace {

constexpr int kButtonWidth = 28;

} // namespace

CardStepper::CardStepper(const juce::String& name)
    : name_(name) {
    setTitle(name);
    down_.setTitle(name + " down");
    down_.setTooltip(name + " down (Down or Left arrow)");
    up_.setTitle(name + " up");
    up_.setTooltip(name + " up (Up or Right arrow)");
    down_.setConnectedEdges(juce::Button::ConnectedOnRight);
    up_.setConnectedEdges(juce::Button::ConnectedOnLeft);
    down_.setWantsKeyboardFocus(true);
    up_.setWantsKeyboardFocus(true);
    down_.onClick = [this] { step(-1); };
    up_.onClick = [this] { step(+1); };
    value_.setJustificationType(juce::Justification::centred);
    value_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(down_);
    addAndMakeVisible(value_);
    addAndMakeVisible(up_);
}

void CardStepper::setValueText(const juce::String& text) {
    value_.setText(text, juce::dontSendNotification);
    value_.setTitle(name_ + ": " + text);
}

void CardStepper::step(int delta) {
    if (isEnabled() && onStep)
        onStep(delta);
}

void CardStepper::resized() {
    auto area = getLocalBounds();
    down_.setBounds(area.removeFromLeft(kButtonWidth));
    up_.setBounds(area.removeFromRight(kButtonWidth));
    value_.setBounds(area);
}

void CardStepper::paintOverChildren(juce::Graphics& g) {
    for (auto* button : {static_cast<juce::Component*>(&down_), static_cast<juce::Component*>(&up_)})
        if (button->hasKeyboardFocus(false))
            paintFocusRingAlways(g, button->getBounds().toFloat(), *this, 4.0f);
}

// A key a focused button does not use (it handles Return and Space itself) arrives here.
bool CardStepper::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) {
        step(+1);
        return true;
    }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) {
        step(-1);
        return true;
    }
    return false;
}

void CardStepper::focusOfChildComponentChanged(FocusChangeType) { repaint(); }

} // namespace synth::ui
