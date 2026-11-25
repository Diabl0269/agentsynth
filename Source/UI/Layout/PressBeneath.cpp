// PressBeneath.cpp -- lands a press a popup's window took outside its shape on the window under it.
#include "UI/Layout/PressBeneath.h"

namespace synth::ui {

namespace {

// The topmost visible window under `screen` that takes clicks, skipping `except`: the order of Desktop's list is the
// order windows were raised in, the same order juce::Desktop::findComponentAt reads.
juce::Component* windowUnder(juce::Point<int> screen, const juce::Component* except) {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;) {
        auto* window = desktop.getComponent(i);
        if (window == nullptr || window == except || !window->isVisible() || window->getPeer() == nullptr)
            continue;
        if (window->contains(window->getLocalPoint(nullptr, screen)))
            return window;
    }
    return nullptr;
}

void sendClick(juce::Point<int> screen, juce::ModifierKeys mods, const juce::Component* except) {
    auto* window = windowUnder(screen, except);
    if (window == nullptr)
        return;
    auto* peer = window->getPeer();
    const auto inPeer = peer->getComponent().getLocalPoint(nullptr, screen).toFloat();
    const auto buttons = mods.getRawFlags() & juce::ModifierKeys::allMouseButtonModifiers;
    const auto keys = mods.withoutMouseButtons();
    const juce::Component::SafePointer<juce::Component> alive(window);
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, inPeer,
                           keys.withFlags(buttons != 0 ? buttons : juce::ModifierKeys::leftButtonModifier),
                           juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                           juce::Time::currentTimeMillis());
    // The press can close or replace the window it landed in (a button that opens a dialog, say).
    if (alive == nullptr || alive->getPeer() != peer)
        return;
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, inPeer, keys,
                           juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                           juce::Time::currentTimeMillis());
}

} // namespace

// Held until the release: the popup's window is still on screen and modal while the button is down, so the release
// reaches it and the mouse source ends the press there. A click sent while the source still held the press would be
// read as a drag of the popup's window.
void PressOutsideShape::pressed(const juce::MouseEvent& e) {
    held_ = true;
    screen_ = e.getScreenPosition();
    mods_ = e.mods;
}

bool PressOutsideShape::released() {
    const bool was = held_;
    held_ = false;
    return was;
}

void PressOutsideShape::land(const juce::Component& popupWindow) const { clickBeneath(screen_, mods_, &popupWindow); }

// On the next turn: the release that ended the press is still being delivered to the popup's window, and the popup's
// own close (leaving modal state) has to have happened, or the landed press would only reach the popup's
// inputAttemptWhenModal.
void clickBeneath(juce::Point<int> screen, juce::ModifierKeys mods, const juce::Component* except) {
    // A popup deleted by now is no longer on the desktop, so there is nothing left to skip.
    juce::Component::SafePointer<juce::Component> skip(const_cast<juce::Component*>(except));
    juce::MessageManager::callAsync([screen, mods, skip] { sendClick(screen, mods, skip.getComponent()); });
}

} // namespace synth::ui
