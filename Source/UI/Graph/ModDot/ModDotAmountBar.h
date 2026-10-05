#pragma once

// The amount fader of one row of the mod dot's panel: the Design System fader (the horizontal juce::Slider painted by
// AppLookAndFeel, the one a module card shows) from -100 to +100 percent, dragged or keyed to change how strongly a
// source moves the knob. A slider for assistive technology: it speaks "<Source> amount" and the value in percent.
// Edits are reported as a gesture (begin, change..., end) so the owner makes ONE undo step of a drag; each Left/Right
// key press is a gesture of its own (1 percent, Shift 10). The drag maths is this class's own (no juce::Slider mouse
// handling), like CardFader, so a headless test can drive it with plain events.

#include "ModDotPalette.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ModDotAmountBar final : public juce::Slider {
public:
    static constexpr float kStep = 0.01f;
    static constexpr float kShiftStep = 0.10f;
    /** A press this close (px) to the cap grabs it where it is instead of jumping it to the pointer. */
    static constexpr int kGrab = 10;

    ModDotAmountBar();

    /** The amount, -1..1. Never fires the callbacks. */
    void setAmount(float value);
    float amount() const noexcept { return (float)getValue(); }
    /** The value as spoken and shown: "+42%", "-18%", "0%". */
    juce::String getValueText() const;
    static juce::String percentText(float value);

    std::function<void()> onGestureBegin;
    std::function<void(float value)> onValueChanged; // between begin and end
    std::function<void()> onGestureEnd;
    std::function<void()> onFocused; // pressed or focused: the owner's row becomes the selected one

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        juce::Component::mouseWheelMove(e, wheel); // the page scrolls; a stray wheel never edits an amount
    }
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** The cap centre's x for `value` (-1..1) and the value an x maps to (whole percent), in the bar's own space. */
    float xForValue(float value) const;
    float valueForX(float x) const;

private:
    class Value;
    void change(float value);
    void applyKeyStep(float delta);

    bool gestureOpen_ = false;
    float grabOffset_ = 0.0f;
    bool dragging_ = false;
};

} // namespace synth::ui
