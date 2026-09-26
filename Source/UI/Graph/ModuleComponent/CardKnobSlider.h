#pragma once

// FRO287: a module card's rotary knob, subclassed only so a click/drag that lands on its
// modulation-ring annulus (or is Alt-modified) can be redirected to a "adjust this routing's
// attenuverter amount" gesture instead of moving the knob itself. Every other click behaves as a
// plain juce::Slider -- this class owns no drag state beyond "is the gesture currently active".
//
// FRO312: a second, independent gesture pair (wantsCablePickupGesture/onCablePickupGesture) claims
// a click landing on the knob's cable-landing DOT instead -- the jack that gesture used to start
// from is hidden now that a knob-bound CV jack draws no gutter dot of its own. Checked AFTER the
// ring-amount gesture (the dot sits just outside the ring, never inside its annulus, so the two
// hit zones cannot overlap -- see ModuleComponent::getModTargetKnobAnchor's push-out math).
//
// docs/layout/module-card.md and docs/modules/modulation.md#drag-to-knob-modulation describe the
// gesture from the user's side.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardKnobSlider : public juce::Slider {
public:
    using juce::Slider::Slider;

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

    /** FRO288: fired on mouseEnter(true)/mouseExit(false), for a knob with a live routing, so
     *  ModuleComponent can tell GraphEditor::setHoveredModTarget -- the knob-hover-highlights-cable
     *  direction of docs/modules/modulation.md#modulation-rings-on-knobs. Unset for a knob with no
     *  routing (ModuleComponent only wires it up when one exists). */
    std::function<void(bool entered)> onHoverChanged;

    /** FRO312: asked on mouseDown right after wantsModAmountGesture declines -- true claims the
     *  whole gesture for onCablePickupGesture instead (pick up / redrag / disconnect the cable
     *  landed on this knob). */
    std::function<bool(const juce::MouseEvent&)> wantsCablePickupGesture;

    /** FRO312: down (0) / drag (1) / up (2) once wantsCablePickupGesture has claimed a gesture --
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
