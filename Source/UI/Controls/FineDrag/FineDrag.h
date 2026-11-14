#pragma once

// The one place that decides how much slower a drag runs while Shift is held: every slider, knob, fader, number field
// and amount bar scales its pointer travel through this file, so the rate is the same everywhere and one number
// (kRate) changes it. docs/layout/fine-drag.md has the rule from the user's side.
//
// Two ways to use it:
//  - a control that works out its own value from a pointer position or a per-move delta reads rateFor(mods) and
//    multiplies the travel by it (the delta-based ones: the stepper, the mod amount gestures), or keeps its own anchor
//    and re-anchors when the rate changes (CardFader, MixerFaderSlider);
//  - a control that hands the mouse to juce::Slider (the knobs and plain sliders) derives from FineDragSlider
//    (FineDragSlider.h), which feeds juce::Slider a pointer that has travelled only `rate` of the real distance.
//
// Pressing or releasing Shift in the middle of a drag never makes the value jump: only the travel after the change
// counts at the new rate.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class FineDrag {
public:
    /** Shift scales a drag's travel to this fraction (about a tenth). */
    static constexpr double kRate = 0.1;

    /** The fraction of the pointer's travel that counts: kRate with Shift held, else 1. */
    static double rateFor(const juce::ModifierKeys& mods) noexcept { return mods.isShiftDown() ? kRate : 1.0; }

    /** Starts a drag at `pointer` (the mouse-down position). */
    void begin(juce::Point<float> pointer) noexcept { start_ = virtual_ = lastReal_ = pointer; }

    /** The pointer position a drag has earned: the start plus every move since, each scaled by its own Shift state. */
    juce::Point<float> advance(const juce::MouseEvent& e) noexcept {
        virtual_ += (e.position - lastReal_) * (float)rateFor(e.mods);
        lastReal_ = e.position;
        return virtual_;
    }

    /** How far the drag has got from where it began, at the scaled rate (advance() minus the start). */
    juce::Point<float> travelled(const juce::MouseEvent& e) noexcept { return advance(e) - start_; }

    /** `e` moved to where advance() says the drag has got to; hand this to code that reads the pointer position. */
    juce::MouseEvent scaled(const juce::MouseEvent& e) noexcept { return e.withNewPosition(advance(e)); }

private:
    juce::Point<float> start_, virtual_, lastReal_;
};

} // namespace synth::ui
