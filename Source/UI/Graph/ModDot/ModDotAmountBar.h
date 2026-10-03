#pragma once

// The amount bar of one row of the mod dot's panel: a track whose centre is zero, a fill from the centre to a
// round handle, dragged or keyed to change how strongly a source moves the knob (-100..+100 percent). A slider
// for assistive technology: it speaks "<Source> amount" and the value in percent. Edits are reported as a
// gesture (begin, change..., end) so the owner makes ONE undo step of a drag; each Left/Right key press is a
// gesture of its own (1 percent, Shift 10).

#include "ModDotPalette.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ModDotAmountBar final
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr float kStep = 0.01f;
    static constexpr float kShiftStep = 0.10f;
    static constexpr int kHandle = 10;

    ModDotAmountBar();

    /** The amount, -1..1. Never fires the callbacks. */
    void setValue(float value);
    float getValue() const noexcept { return value_; }
    /** The value as spoken and shown: "+42%", "-18%", "0%". */
    juce::String getValueText() const;
    static juce::String percentText(float value);

    std::function<void()> onGestureBegin;
    std::function<void(float value)> onValueChanged; // between begin and end
    std::function<void()> onGestureEnd;
    std::function<void()> onFocused; // pressed or focused: the owner's row becomes the selected one

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** The track's x range (the handle's centre travels over it), for tests. */
    juce::Range<float> trackRange() const;
    float xForValue(float value) const;
    float valueForX(float x) const;

private:
    class Value;
    void change(float value);
    void applyKeyStep(float delta);

    float value_ = 0.0f;
    bool gestureOpen_ = false;
    float grabOffset_ = 0.0f;
    bool dragging_ = false;
};

} // namespace synth::ui
