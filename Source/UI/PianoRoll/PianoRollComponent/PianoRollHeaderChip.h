#pragma once

#include "UI/Layout/FocusRing.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

namespace synth::ui {

// The keyboard and screen-reader face of one painted header chip. PianoRollComponent paints the chip
// and handles the pointer itself, so this button is transparent to the mouse and draws nothing but the
// accent focus ring; what it adds is a Tab stop, Return activation (onClick, run at once rather than
// through juce::Button's asynchronous trigger; Space stays the global play/stop), a screen-reader
// name and role (a toggle chip reports its on state through setToggleState), and a tooltip that is
// re-read from `tooltip` on every query so a rebound shortcut shows at once.
class PianoRollHeaderChip final : public juce::Button {
public:
    static constexpr float kRingCornerRadius = 4.0f;

    PianoRollHeaderChip(const juce::String& title, std::function<juce::String()> tooltip)
        : juce::Button(title)
        , tooltip_(std::move(tooltip)) {
        setTitle(title);
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(false, false);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (!isEnabled() || !key.isKeyCode(juce::KeyPress::returnKey))
            return false;
        if (onClick)
            onClick();
        return true;
    }

    juce::String getTooltip() override { return tooltip_ ? tooltip_() : juce::String(); }

    void paintButton(juce::Graphics& g, bool, bool) override {
        paintFocusRing(g, getLocalBounds().toFloat(), *this, kRingCornerRadius);
    }

private:
    std::function<juce::String()> tooltip_;
};

} // namespace synth::ui
