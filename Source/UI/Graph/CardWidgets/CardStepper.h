#pragma once

#include "UI/Layout/NonModalLabel.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * A small integer as "-" value "+". The two buttons are separate Tab stops titled after the parameter
 * ("Octave down", "Octave up"); with either focused, Up/Right step up and Down/Left step down. A right
 * click on any part is left to the card's mouse listener. docs/layout/module-card.md#faders-switches-and-steppers.
 */
class CardStepper
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    /** `name` is the parameter's display name. */
    explicit CardStepper(const juce::String& name);

    /** Fired with -1 or +1 when the user steps (a button or a key). */
    std::function<void(int delta)> onStep;

    /** Fired with the text the user typed into the value (see setTypable); the owner applies it and sets the value
     *  text again, whether or not it was accepted. */
    std::function<void(const juce::String& text)> onTypedValue;
    /** Lets the user type the value: a double-click on it, or a digit key while a button has focus, opens an editor;
     *  Return applies, Escape cancels. For a range too wide to walk one step at a time. */
    void setTypable(bool typable);

    /** The value as the parameter's text; also what the value label reads to a screen reader. */
    void setValueText(const juce::String& text);
    juce::String getValueText() const { return value_.getText(); }

    juce::Button& getDownButton() noexcept { return down_; }
    juce::Button& getUpButton() noexcept { return up_; }

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusOfChildComponentChanged(FocusChangeType cause) override;

private:
    void step(int delta);

    juce::String name_;
    synth::ui::midilearn::RightClickSafeButton<juce::TextButton> down_{"-"};
    synth::ui::midilearn::RightClickSafeButton<juce::TextButton> up_{"+"};
    synth::ui::NonModalLabel value_;
    bool typable_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardStepper)
};

} // namespace synth::ui
