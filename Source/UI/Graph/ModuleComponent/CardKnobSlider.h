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
    /** What a screen reader says for a value when set and it answers (the readout without its unit is not enough
     *  to speak); else the readout. Set it before the handler is first asked for, or call
     *  invalidateAccessibilityHandler(). */
    std::function<std::optional<juce::String>(double)> spokenHook;

    /** The slider's own value box: its child Label (null before the look has made one). */
    juce::Label* valueBox() const {
        for (auto* child : getChildren())
            if (auto* box = dynamic_cast<juce::Label*>(child))
                return box;
        return nullptr;
    }

    /** Lets the value box squeeze its text down to `scale` of its width before drawFittedText truncates it with
     *  "..." (juce::Label's minimum horizontal scale, which AppLookAndFeel::drawLabel hands to drawFittedText), and
     *  gives the box the id "valueBox" so a scenario can read its text. The box is rebuilt on a look change, so
     *  it is applied again there. */
    void setValueBoxMinimumScale(float scale) {
        valueBoxScale_ = scale;
        styleValueBox();
    }

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

    void lookAndFeelChanged() override {
        juce::Slider::lookAndFeelChanged();
        styleValueBox();
    }

    /** The slider's handler, except that a screen reader hears `spokenHook`'s text as the value when it is set. */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        if (!spokenHook)
            return juce::Slider::createAccessibilityHandler();
        return std::make_unique<SpokenHandler>(*this);
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
    float valueBoxScale_ = 0.0f; // 0: the Label's own default

    void styleValueBox() {
        if (valueBoxScale_ <= 0.0f)
            return;
        if (auto* box = valueBox()) {
            box->setMinimumHorizontalScale(valueBoxScale_);
            box->setComponentID("valueBox");
        }
    }

    juce::String spokenText(double value) {
        if (spokenHook)
            if (const auto text = spokenHook(value))
                return *text;
        return getTextFromValue(value);
    }

    // The Slider's own handler (a slider role and a numeric value over its range) with the spoken text.
    class SpokenHandler : public juce::AccessibilityHandler {
    public:
        explicit SpokenHandler(CardKnobSlider& s)
            : juce::AccessibilityHandler(s, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                         Interfaces{std::make_unique<Value>(s)})
            , slider_(s) {}
        juce::String getHelp() const override { return slider_.getTooltip(); }

    private:
        class Value : public juce::AccessibilityValueInterface {
        public:
            explicit Value(CardKnobSlider& s)
                : slider_(s) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return slider_.getValue(); }
            void setValue(double v) override {
                juce::Slider::ScopedDragNotification drag(slider_);
                slider_.setValue(v, juce::sendNotificationSync);
            }
            juce::String getCurrentValueAsString() const override { return slider_.spokenText(slider_.getValue()); }
            void setValueAsString(const juce::String& text) override { setValue(slider_.getValueFromText(text)); }
            AccessibleValueRange getRange() const override {
                const double step = slider_.getInterval() > 0.0
                                        ? slider_.getInterval()
                                        : (slider_.getMaximum() - slider_.getMinimum()) / 1000.0;
                return {{slider_.getMinimum(), slider_.getMaximum()}, step};
            }

        private:
            CardKnobSlider& slider_;
        };
        CardKnobSlider& slider_;
    };

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
