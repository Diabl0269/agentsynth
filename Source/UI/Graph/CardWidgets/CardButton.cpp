// CardButton.cpp -- the Macros button's Toggle and Momentary behaviour, its keys and its names.
#include "CardButton.h"

namespace synth::ui {

CardButton::CardButton() {
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setSize(kWidth, kHeight);
    refreshState();
}

void CardButton::setDisplayName(const juce::String& name) {
    name_ = name;
    refreshState();
}

void CardButton::setMomentary(bool momentary) {
    momentary_ = momentary;
    setClickingTogglesState(!momentary); // a held button lights from its press, not from a click
    refreshState();
}

void CardButton::setLit(bool on) {
    setToggleState(on, juce::dontSendNotification);
    refreshState();
}

// What a screen reader and the tooltip say: the name and the state, and how it behaves.
void CardButton::refreshState() {
    const bool on = getToggleState();
    setButtonText(on ? "On" : "Off");
    setTitle(name_ + (on ? ", on" : ", off"));
    setTooltip(name_ + (momentary_ ? ": on while held. Press and hold, or hold Space or Enter"
                                   : ": click, or press Space or Enter, to switch on or off"));
    setDescription(momentary_ ? "Momentary button: on while pressed" : "Toggle button");
    repaint();
}

bool CardButton::isActivationKey(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    return key.isKeyCode(juce::KeyPress::spaceKey) || key.isKeyCode(juce::KeyPress::returnKey);
}

void CardButton::press() {
    if (held_)
        return;
    held_ = true;
    setLit(true);
    if (onPress)
        onPress();
}

// The one way a hold ends: whatever ended it (mouse up anywhere, key up, focus lost, hidden), the value goes back off.
void CardButton::release() {
    if (!held_)
        return;
    held_ = false;
    setLit(false);
    if (onRelease)
        onRelease();
}

void CardButton::flip() {
    setToggleState(!getToggleState(), juce::dontSendNotification);
    refreshState();
    if (onClick)
        onClick();
}

void CardButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || (refusesEdit && refusesEdit()))
        return;
    TextButton::mouseDown(e);
    if (momentary_)
        press();
}

// Mouse events go to the component that took the press, so a release outside the button still arrives here.
void CardButton::mouseUp(const juce::MouseEvent& e) {
    TextButton::mouseUp(e);
    if (momentary_)
        release();
}

bool CardButton::keyPressed(const juce::KeyPress& key) {
    if (!isActivationKey(key))
        return TextButton::keyPressed(key);
    if (refusesEdit && refusesEdit())
        return true;
    if (keyDown_)
        return true; // auto-repeat of a key already acted on
    keyDown_ = true;
    if (momentary_)
        press();
    else
        flip(); // not triggerClick(): that posts a message, so the value would land a turn late
    return true;
}

bool CardButton::keyStateChanged(bool) {
    if (keyDown_ && !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey) &&
        !juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::returnKey)) {
        keyDown_ = false;
        if (momentary_)
            release();
    }
    return false;
}

void CardButton::focusLost(FocusChangeType cause) {
    TextButton::focusLost(cause);
    keyDown_ = false;
    release();
}

void CardButton::visibilityChanged() {
    TextButton::visibilityChanged();
    if (!isShowing())
        release();
}

} // namespace synth::ui
