#pragma once

#include "UI/MidiRemote/MidiLearnMenu.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * A module card's button: a Macros knob shown as a switch. It is the app's own text button (the same pill, hover,
 * press and focus ring as every TextButton) that reads On or Off and lights with the accent while on.
 *
 * Two modes. Toggle (the default): a click, Space or Enter flips it, and `onClick` carries the new state. Momentary:
 * it is on while held and falls back off the moment it is let go (the mouse released anywhere, the key released,
 * keyboard focus lost, the button hidden): `onPress` and `onRelease` bracket the hold, so the owner writes one
 * gesture. It edits nothing itself; the owner gives it the value's state (`setLit`) and writes what it reports.
 * A right-click is swallowed here and reaches the card (MIDI Learn, Automate).
 * docs/layout/module-card-layout.md#the-button.
 */
class CardButton final : public synth::ui::midilearn::RightClickSafeButton<juce::TextButton> {
public:
    static constexpr int kHeight = 28;
    static constexpr int kWidth = 56;

    CardButton();

    /** The control's name, as its title and tooltip say it. */
    void setDisplayName(const juce::String& name);
    /** Momentary or Toggle; changes nothing already held. */
    void setMomentary(bool momentary);
    bool isMomentary() const noexcept { return momentary_; }
    /** Follows the value (on or off) without reporting a change. */
    void setLit(bool on);
    bool isLit() const noexcept { return getToggleState(); }
    /** True between a Momentary press and its release. */
    bool isHeld() const noexcept { return held_; }

    /** Momentary only: the press and the release that ends it. */
    std::function<void()> onPress;
    std::function<void()> onRelease;
    /** Asked before a press or key: true (having said why) swallows it. Unset never refuses. */
    std::function<bool()> refusesEdit;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType cause) override;
    void visibilityChanged() override;
    void clicked() override { refreshState(); }

private:
    void press();
    void release();
    void flip(); ///< A Toggle key press: switches state and runs onClick at once.
    void refreshState();
    static bool isActivationKey(const juce::KeyPress& key);

    juce::String name_{"Button"};
    bool momentary_ = false;
    bool held_ = false;    ///< Momentary: on until the release.
    bool keyDown_ = false; ///< An activation key is down: its auto-repeat acts once.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardButton)
};

} // namespace synth::ui
