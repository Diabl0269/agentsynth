#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// SpaceClaimingButton.h (docs/development/accessibility.md#space-on-a-focused-button): a juce::Button that
// presses on Space and consumes the key, so a focused button never also toggles playback.
namespace synth::ui {

/** True for a bare Space (Shift allowed); Command, Control and Alt Space are other shortcuts' keys. */
inline bool isPlainSpace(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    return key.isKeyCode(juce::KeyPress::spaceKey) && !mods.isCommandDown() && !mods.isCtrlDown() && !mods.isAltDown();
}

/** Presses `button` for a bare Space and returns true (consumed); false for any other key. */
bool pressButtonOnSpace(juce::Button& button, const juce::KeyPress& key);

/** Wraps any juce::Button subclass `Base`; keeps Base's constructors and Return handling. */
template <class Base>
class SpaceClaimingButton : public Base {
public:
    using Base::Base;

    bool keyPressed(const juce::KeyPress& key) override {
        return pressButtonOnSpace(*this, key) || Base::keyPressed(key);
    }
};

} // namespace synth::ui
