#pragma once

// The gestures every continuous control on a module card carries, whatever it looks like: the
// modulation-amount drag (a press the card claims to adjust a routing's attenuverter instead of the
// value), the cable pickup (a press on the cable's landing dot re-drags that cable), the hover that
// highlights the cable landing on the control, and the keyboard steps. CardKnobSlider and CardFader
// both inherit it and route their mouse events through it first, so the two can never drift apart;
// ModuleComponent::wireCardControlGestures wires the callbacks for either.
// docs/layout/module-card.md and docs/modules/modulation.md#drag-to-knob-modulation describe the
// gestures from the user's side.

#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

class CardControlGestures {
public:
    virtual ~CardControlGestures() = default;

    /** Asked on every mouseDown before the control's own handling. True claims the whole gesture
     *  (down through up) for onModAmountGesture. Must be set before the first mouseDown. */
    std::function<bool(const juce::MouseEvent&)> wantsModAmountGesture;
    /** Down (0), drag (1), up (2) of a claimed modulation-amount gesture; the control's own value is
     *  never touched while it runs. */
    std::function<void(const juce::MouseEvent&, int phase)> onModAmountGesture;
    /** mouseEnter (true) / mouseExit (false); unset for a control with no routing. */
    std::function<void(bool entered)> onHoverChanged;
    /** Asked on mouseDown right after wantsModAmountGesture declines; true claims the gesture for
     *  onCablePickupGesture. */
    std::function<bool(const juce::MouseEvent&)> wantsCablePickupGesture;
    /** Down (0), drag (1), up (2) of a claimed cable pickup. */
    std::function<void(const juce::MouseEvent&, int phase)> onCablePickupGesture;

    /** Keyboard steps: Up/Right and Down/Left one step (Shift a fine step), Page Up/Down a coarse
     *  step, Home/End the range ends. Nullopt when `key` is not a value key. */
    static std::optional<double> valueForKey(juce::Slider& slider, const juce::KeyPress& key) {
        const auto mods = key.getModifiers();
        if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
            return std::nullopt;
        const bool shift = mods.isShiftDown();
        const int code = key.getKeyCode();
        if (!shift && code == juce::KeyPress::homeKey)
            return slider.getMinimum();
        if (!shift && code == juce::KeyPress::endKey)
            return slider.getMaximum();

        double fraction = 0.0;
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
            fraction = shift ? kFineStep : kStep;
        else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
            fraction = shift ? -kFineStep : -kStep;
        else if (!shift && code == juce::KeyPress::pageUpKey)
            fraction = kCoarseStep;
        else if (!shift && code == juce::KeyPress::pageDownKey)
            fraction = -kCoarseStep;
        else
            return std::nullopt;

        // Steps are fractions of the travel, so a skewed control (cutoff, envelope times) moves as
        // evenly as it does under the mouse. A stepped parameter moves at least one notch.
        const double current = slider.getValue();
        const double proportion = juce::jlimit(0.0, 1.0, slider.valueToProportionOfLength(current) + fraction);
        double next = slider.proportionOfLengthToValue(proportion);
        if (const double interval = slider.getInterval(); interval > 0.0) {
            next = slider.getMinimum() + interval * std::round((next - slider.getMinimum()) / interval);
            if (next == current)
                next = current + (fraction > 0.0 ? interval : -interval);
        }
        return juce::jlimit(slider.getMinimum(), slider.getMaximum(), next);
    }

    /** Applies a value key as one change gesture (one undo step, one automation touch); false when
     *  `key` is not a value key, so Tab, Escape and Return stay with the card. */
    static bool applyValueKey(juce::Slider& slider, const juce::KeyPress& key) {
        const auto target = valueForKey(slider, key);
        if (!target.has_value())
            return false;
        if (*target != slider.getValue()) {
            juce::Slider::ScopedDragNotification gesture(slider);
            slider.setValue(*target, juce::sendNotificationSync);
        }
        return true;
    }

    static constexpr double kStep = 0.01;      // of the travel
    static constexpr double kFineStep = 0.001; // Shift
    static constexpr double kCoarseStep = 0.1; // Page Up / Page Down

protected:
    /** True when a card gesture took this press; the control skips its own handling then. */
    bool claimMouseDown(const juce::MouseEvent& e) {
        modAmountActive_ = wantsModAmountGesture && wantsModAmountGesture(e);
        if (modAmountActive_) {
            if (onModAmountGesture)
                onModAmountGesture(e, 0);
            return true;
        }
        pickupActive_ = wantsCablePickupGesture && wantsCablePickupGesture(e);
        if (pickupActive_) {
            if (onCablePickupGesture)
                onCablePickupGesture(e, 0);
            return true;
        }
        return false;
    }

    /** Forwards a drag (phase 1) or release (phase 2) to the claimed gesture; false when none is active. */
    bool forwardClaimed(const juce::MouseEvent& e, int phase) {
        if (modAmountActive_) {
            if (phase == 2)
                modAmountActive_ = false;
            if (onModAmountGesture)
                onModAmountGesture(e, phase);
            return true;
        }
        if (pickupActive_) {
            if (phase == 2)
                pickupActive_ = false;
            if (onCablePickupGesture)
                onCablePickupGesture(e, phase);
            return true;
        }
        return false;
    }

    void notifyHover(bool entered) {
        if (onHoverChanged)
            onHoverChanged(entered);
    }

private:
    bool modAmountActive_ = false;
    bool pickupActive_ = false;
};

} // namespace synth::ui
