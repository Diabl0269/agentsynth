// DrumMachineComponent: the keyboard. The grid is one Tab stop; the arrows move a focus cell, and the cell's verbs
// (toggle a hit, toggle accent, louder, softer, play the clip) are rebindable actions resolved through
// matchesAction. Delete/Backspace, the arrows, Home/End and Escape are fixed conventions.

#include "DrumMachineComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include <algorithm>

namespace synth::ui {

// A manager that has never heard of an id answers with an invalid binding, which means "no key"; the fallback is only
// for a grid with no manager at all.
bool DrumMachineComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                         const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    const auto binding = shortcuts_->getBinding(actionId);
    return binding.isValid() && ShortcutManager::keyPressMatches(binding, key);
}

bool DrumMachineComponent::moveFocus(int dRow, int dStep) {
    if (slots_.empty())
        return false;
    setFocusCell({focus_.row + dRow, focus_.step + dStep});
    return true;
}

bool DrumMachineComponent::nudgeFocusedVelocity(int delta) {
    const int pitch = getLanePitch(focus_.row);
    const int current = getCellVelocity(focus_.row, focus_.step);
    if (pitch < 0 || current == 0)
        return false; // an empty cell has no loudness to change
    setCellVelocity(pitch, focus_.step, current + delta);
    announceFocus();
    return true;
}

void DrumMachineComponent::announceFocus() {
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

bool DrumMachineComponent::keyPressed(const juce::KeyPress& key) {
    if (!clipId_.isValid())
        return false;
    const auto none = juce::ModifierKeys();
    const auto shift = juce::ModifierKeys(juce::ModifierKeys::shiftModifier);
    const int pitch = getLanePitch(focus_.row);

    // The most specific chords first, so Shift+Return is not taken for Return.
    if (matchesAction(key, "drumMachineToggleAccent", juce::KeyPress(juce::KeyPress::returnKey, shift, 0))) {
        if (pitch >= 0)
            toggleHit(pitch, focus_.step, true);
        announceFocus();
        return true;
    }
    if (matchesAction(key, "drumMachinePlayClip", juce::KeyPress(juce::KeyPress::spaceKey, none, 0))) {
        toggleClipPreview();
        return true;
    }
    if (matchesAction(key, "drumMachineToggleHit", juce::KeyPress(juce::KeyPress::returnKey, none, 0))) {
        if (pitch >= 0)
            toggleHit(pitch, focus_.step, false);
        announceFocus();
        return true;
    }
    if (matchesAction(key, "drumMachineLouder", juce::KeyPress(juce::KeyPress::upKey, shift, 0)))
        return nudgeFocusedVelocity(kVelocityKeyStep) || true;
    if (matchesAction(key, "drumMachineSofter", juce::KeyPress(juce::KeyPress::downKey, shift, 0)))
        return nudgeFocusedVelocity(-kVelocityKeyStep) || true;

    if (key == juce::KeyPress::escapeKey) {
        if (gesture_.kind != Gesture::Kind::None) {
            endGesture(false);
            return true;
        }
        requestClose();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (pitch >= 0)
            removeHits(pitch, focus_.step);
        announceFocus();
        return true;
    }
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (key == juce::KeyPress::leftKey)
        return moveFocus(0, -1);
    if (key == juce::KeyPress::rightKey)
        return moveFocus(0, 1);
    if (key == juce::KeyPress::upKey)
        return moveFocus(-1, 0);
    if (key == juce::KeyPress::downKey)
        return moveFocus(1, 0);
    if (key == juce::KeyPress::homeKey)
        return moveFocus(0, -focus_.step);
    if (key == juce::KeyPress::endKey)
        return moveFocus(0, getStepCount());
    return false;
}

} // namespace synth::ui
