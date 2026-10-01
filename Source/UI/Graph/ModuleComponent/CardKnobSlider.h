#pragma once

// A module card's rotary knob. It takes keyboard focus and turns from the keys (keyPressed below),
// and a click/drag that lands on its modulation-ring annulus (or is Alt-modified) can be redirected
// to a "adjust this routing's attenuverter amount" gesture instead of moving the knob itself. Every other click behaves
// as a plain juce::Slider -- this class owns no drag state beyond "is the gesture currently active".
//
// A second, independent gesture pair (wantsCablePickupGesture/onCablePickupGesture) claims
// a click landing on the knob's cable-landing DOT instead -- the jack that gesture used to start
// from is hidden now that a knob-bound CV jack draws no gutter dot of its own. Checked AFTER the
// ring-amount gesture (the dot sits just outside the ring, never inside its annulus, so the two
// hit zones cannot overlap -- see ModuleComponent::getModTargetKnobAnchor's push-out math).
//
// docs/layout/module-card.md and docs/modules/modulation.md#drag-to-knob-modulation describe the
// gesture from the user's side.

#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

class CardKnobSlider : public juce::Slider {
public:
    CardKnobSlider() { setWantsKeyboardFocus(true); }

    /** Keyboard steps for a focused knob: Up/Right and Down/Left one step (Shift a fine step), Page
     *  Up/Down a coarse step, Home/End the minimum/maximum. Each press is one change gesture, the
     *  same begin/set/end a mouse drag makes, so it is one undo step and one automation touch.
     *  Every other key (Tab, Escape, Return) is left for the card. */
    bool keyPressed(const juce::KeyPress& key) override {
        const auto target = valueForKey(key);
        if (!target.has_value())
            return false;
        if (*target != getValue()) {
            ScopedDragNotification gesture(*this);
            setValue(*target, juce::sendNotificationSync);
        }
        return true;
    }

    /** The value `key` would set, or nullopt when it is not a knob key. */
    std::optional<double> valueForKey(const juce::KeyPress& key) {
        const auto mods = key.getModifiers();
        if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
            return std::nullopt;
        const bool shift = mods.isShiftDown();
        const int code = key.getKeyCode();
        if (!shift && code == juce::KeyPress::homeKey)
            return getMinimum();
        if (!shift && code == juce::KeyPress::endKey)
            return getMaximum();

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

        // Steps are fractions of the knob's travel, so a skewed knob (cutoff, envelope times) moves
        // as evenly as it turns under the mouse. A stepped parameter moves at least one notch.
        const double current = getValue();
        const double proportion = juce::jlimit(0.0, 1.0, valueToProportionOfLength(current) + fraction);
        double next = proportionOfLengthToValue(proportion);
        if (const double interval = getInterval(); interval > 0.0) {
            next = getMinimum() + interval * std::round((next - getMinimum()) / interval);
            if (next == current)
                next = current + (fraction > 0.0 ? interval : -interval);
        }
        return juce::jlimit(getMinimum(), getMaximum(), next);
    }

    static constexpr double kStep = 0.01;      // of the knob's travel
    static constexpr double kFineStep = 0.001; // Shift
    static constexpr double kCoarseStep = 0.1; // Page Up / Page Down

    /** Asked on every mouseDown before JUCE's own handling runs. Returning true claims the whole
     *  gesture (down through up); ModuleComponent decides based on whether this knob has a live
     *  AttenuverterChain routing and whether the click is Alt-modified or within the ring's
     *  annulus. Must be set before this component receives a mouseDown, or a click behaves as a
     *  plain Slider. */
    std::function<bool(const juce::MouseEvent&)> wantsModAmountGesture;

    /** Fired for each of down (phase 0), drag (phase 1) and up (phase 2) once
     *  wantsModAmountGesture has claimed a gesture. The knob's own value is never touched by this
     *  class while a gesture is active -- ModuleComponent's handler adjusts the attenuverter's
     *  "amount" param instead (GraphEditor::adjustModAmount). */
    std::function<void(const juce::MouseEvent&, int phase)> onModAmountGesture;

    /** Fired on mouseEnter(true)/mouseExit(false), for a knob with a live routing, so
     *  ModuleComponent can tell GraphEditor::setHoveredModTarget -- the knob-hover-highlights-cable
     *  direction of docs/modules/modulation.md#modulation-rings-on-knobs. Unset for a knob with no
     *  routing (ModuleComponent only wires it up when one exists). */
    std::function<void(bool entered)> onHoverChanged;

    /** Asked on mouseDown right after wantsModAmountGesture declines -- true claims the
     *  whole gesture for onCablePickupGesture instead (pick up / redrag / disconnect the cable
     *  landed on this knob). */
    std::function<bool(const juce::MouseEvent&)> wantsCablePickupGesture;

    /** Down (0) / drag (1) / up (2) once wantsCablePickupGesture has claimed a gesture --
     *  forwards straight into GraphEditor::beginConnectionDrag/dragConnection/endConnectionDrag as
     *  an INPUT drag, exactly what a click on the (now hidden) gutter jack used to start. */
    std::function<void(const juce::MouseEvent&, int phase)> onCablePickupGesture;

    void mouseDown(const juce::MouseEvent& e) override {
        gestureActive_ = wantsModAmountGesture && wantsModAmountGesture(e);
        if (gestureActive_) {
            if (onModAmountGesture)
                onModAmountGesture(e, 0);
            return;
        }
        pickupActive_ = wantsCablePickupGesture && wantsCablePickupGesture(e);
        if (pickupActive_) {
            if (onCablePickupGesture)
                onCablePickupGesture(e, 0);
            return;
        }
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (gestureActive_) {
            if (onModAmountGesture)
                onModAmountGesture(e, 1);
            return;
        }
        if (pickupActive_) {
            if (onCablePickupGesture)
                onCablePickupGesture(e, 1);
            return;
        }
        juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (gestureActive_) {
            gestureActive_ = false;
            if (onModAmountGesture)
                onModAmountGesture(e, 2);
            return;
        }
        if (pickupActive_) {
            pickupActive_ = false;
            if (onCablePickupGesture)
                onCablePickupGesture(e, 2);
            return;
        }
        juce::Slider::mouseUp(e);
    }

    void mouseEnter(const juce::MouseEvent& e) override {
        if (onHoverChanged)
            onHoverChanged(true);
        juce::Slider::mouseEnter(e);
    }

    void mouseExit(const juce::MouseEvent& e) override {
        if (onHoverChanged)
            onHoverChanged(false);
        juce::Slider::mouseExit(e);
    }

private:
    bool gestureActive_ = false;
    bool pickupActive_ = false;
};

} // namespace synth::ui
