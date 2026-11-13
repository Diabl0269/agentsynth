// Concern: the Space press shared by every focusable button that sits under a global Space shortcut.
#include "SpaceClaimingButton.h"

namespace synth::ui {

// juce::Button presses on Return only; a Button shortcut for Space fires from keyStateChanged and leaves
// keyPressed unconsumed, so the key would bubble to the window's Space command (play/stop). Pressing here and
// returning true is the single choke point: one click, nothing left to bubble. A disabled button still
// consumes the key, so Space on a greyed control never starts the transport either.
bool pressButtonOnSpace(juce::Button& button, const juce::KeyPress& key) {
    if (!isPlainSpace(key))
        return false;
    if (button.isEnabled())
        button.triggerClick();
    return true;
}

} // namespace synth::ui
