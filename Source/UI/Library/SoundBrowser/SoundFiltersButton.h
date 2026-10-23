#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The Filters button beside the search: Space or Return presses it (a plain button only reacts to Space when the key
 * is physically down, which a screen reader or a test never produces). */
class SoundFiltersButton : public juce::TextButton {
public:
    SoundFiltersButton()
        : juce::TextButton("Filters") {}

    bool keyPressed(const juce::KeyPress& key) override {
        if (!key.getModifiers().isAnyModifierKeyDown() &&
            (key.isKeyCode(juce::KeyPress::spaceKey) || key.isKeyCode(juce::KeyPress::returnKey))) {
            if (onClick)
                onClick();
            else
                triggerClick();
            return true;
        }
        return juce::TextButton::keyPressed(key);
    }
};

} // namespace synth::ui
