#pragma once

// A module card's rotary knob (also the large knob: the same widget at a 60 px dial). It takes
// keyboard focus and turns from the keys, and a click/drag that lands on its modulation-ring annulus
// (or is Alt-modified) can be redirected to a "adjust this routing's attenuverter amount" gesture
// instead of moving the knob itself. Every other click behaves as a plain juce::Slider.
//
// A second, independent gesture pair (wantsCablePickupGesture/onCablePickupGesture) claims a click
// landing on the knob's cable-landing DOT instead -- the jack that gesture used to start from is
// hidden now that a knob-bound CV jack draws no gutter dot of its own. Checked AFTER the ring-amount
// gesture (the dot sits just outside the ring, never inside its annulus, so the two hit zones cannot
// overlap -- see ModuleComponent::getModTargetKnobAnchor's push-out math).
//
// Both gestures and the keyboard steps live in CardControlGestures, shared with CardFader.

#include "UI/Graph/CardWidgets/CardControlGestures.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardKnobSlider
    : public juce::Slider
    , public CardControlGestures {
public:
    CardKnobSlider() { setWantsKeyboardFocus(true); }

    /** A drag's value passes through it when set (the Filter cutoff's Snap to notes); unset keeps the drag's. */
    std::function<double(double)> snapHook;
    /** Asked first for Up/Right (+1) and Down/Left (-1) without modifiers: the value one step that way, or nullopt
     *  for the usual step. */
    std::function<std::optional<double>(double current, int direction)> stepHook;
    /** The readout for a value when set and it answers; else the parameter's own text. */
    std::function<std::optional<juce::String>(double)> readoutHook;

    /** Each press is one change gesture, the same begin/set/end a mouse drag makes. */
    bool keyPressed(const juce::KeyPress& key) override {
        if (cancelModDotOnEscape(key))
            return true;
        if (const int direction = stepDirection(key); direction != 0 && stepHook)
            if (const auto target = stepHook(getValue(), direction))
                return applyKeyTarget(*this, target);
        return applyValueKey(*this, key);
    }

    double snapValue(double value, DragMode mode) override {
        return snapHook ? snapHook(value) : juce::Slider::snapValue(value, mode);
    }

    juce::String getTextFromValue(double value) override {
        if (readoutHook)
            if (const auto text = readoutHook(value))
                return *text;
        return juce::Slider::getTextFromValue(value);
    }

    /** The value `key` would set, or nullopt when it is not a knob key. */
    std::optional<double> valueForKey(const juce::KeyPress& key) {
        return CardControlGestures::valueForKey(*this, key);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!claimMouseDown(e))
            juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!forwardClaimed(e, 1))
            juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!forwardClaimed(e, 2))
            juce::Slider::mouseUp(e);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!modDotClaimedLastPress())
            juce::Slider::mouseDoubleClick(e);
    }

    void mouseEnter(const juce::MouseEvent& e) override {
        notifyHover(true);
        juce::Slider::mouseEnter(e);
    }

    void mouseExit(const juce::MouseEvent& e) override {
        notifyHover(false);
        juce::Slider::mouseExit(e);
    }

private:
    static int stepDirection(const juce::KeyPress& key) {
        if (key.getModifiers().isAnyModifierKeyDown())
            return 0;
        if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::rightKey))
            return 1;
        if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::leftKey))
            return -1;
        return 0;
    }
};

} // namespace synth::ui
