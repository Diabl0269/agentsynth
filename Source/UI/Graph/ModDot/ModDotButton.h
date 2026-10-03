#pragma once

// The keyboard and screen-reader face of a knob's mod dot. The canvas paints the dot and the knob
// handles the pointer, so this button is transparent to the mouse and draws nothing but the accent
// focus ring around the dot. What it adds is a Tab stop right after its knob, a name ("Cutoff
// modulation, 2 sources"), a tooltip, and the keys: Up/Down step the last-chosen source's amount by one
// percent (Shift ten), Return/Space open the dot's menu. docs/layout/module-card.md#modulation-dot.

#include "UI/Layout/FocusRing.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ModDotButton final : public juce::Button {
public:
    static constexpr int kSize = 16; // the button's side; the dot itself is 7 px
    static constexpr float kStepPercent = 0.01f;
    static constexpr float kShiftStepPercent = 0.10f;

    ModDotButton(int destChannel, juce::Slider& knob)
        : juce::Button({})
        , destChannel_(destChannel)
        , knob_(&knob) {
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(false, false);
    }

    /** One key step of `delta` (-1..1 of the amount's range) of the last-chosen source. */
    std::function<void(float delta)> onStep;
    /** Return / Space. */
    std::function<void()> onActivate;

    /** While the dot's panel is open the dot carries the accent ring, focused or not. */
    void setMenuOpen(bool open) {
        if (menuOpen_ != open) {
            menuOpen_ = open;
            repaint();
        }
    }
    bool isMenuOpen() const noexcept { return menuOpen_; }

    int getDestChannel() const noexcept { return destChannel_; }
    /** The knob or fader this dot belongs to; Tab visits the dot right after it. */
    juce::Slider* getKnob() const noexcept { return knob_.getComponent(); }

    bool keyPressed(const juce::KeyPress& key) override {
        if (!isEnabled())
            return false;
        const auto mods = key.getModifiers();
        if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
            return false;
        if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
            const float step = mods.isShiftDown() ? kShiftStepPercent : kStepPercent;
            if (onStep)
                onStep(key.isKeyCode(juce::KeyPress::upKey) ? step : -step);
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
            if (onActivate)
                onActivate();
            return true;
        }
        return false;
    }

    void paintButton(juce::Graphics& g, bool, bool) override {
        // Around the 7 px dot, which sits at the centre.
        const auto ring = getLocalBounds().toFloat().withSizeKeepingCentre(11.0f, 11.0f);
        if (menuOpen_)
            paintFocusRingAlways(g, ring, *this, 5.5f);
        else
            paintFocusRing(g, ring, *this, 5.5f);
    }

private:
    int destChannel_;
    bool menuOpen_ = false;
    juce::Component::SafePointer<juce::Slider> knob_;
};

} // namespace synth::ui
