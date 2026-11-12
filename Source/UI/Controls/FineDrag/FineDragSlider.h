#pragma once

// A juce::Slider whose drag slows to FineDrag::kRate while Shift is held (rotary angle, rotary and linear drags,
// absolute and relative modes alike). It feeds the base class a pointer that has travelled only the scaled distance,
// so juce::Slider's own maths, gesture notifications and snapping are untouched and the value never jumps when Shift
// is pressed or released mid-drag. Every slider in the app derives from this (or from a class that does); the guard
// FineDragEveryControl in Tests/UI/Controls/FineDragTests.cpp fails on a juce::Slider that does not.

#include "UI/Controls/FineDrag/FineDrag.h"
#include <memory>

namespace synth::ui {

class FineDragSlider : public juce::Slider {
public:
    FineDragSlider() = default;
    FineDragSlider(SliderStyle style, TextEntryBoxPosition textBox)
        : juce::Slider(style, textBox) {}

    void mouseDown(const juce::MouseEvent& e) override {
        fine_.begin(e.position);
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override { juce::Slider::mouseDrag(fine_.scaled(e)); }

private:
    FineDrag fine_;
};

// A new FineDragSlider as a plain juce::Slider, for a widely included header that must not include this file (the
// header-reach guard): declare this there and call it. Defined in FineDragSlider.cpp.
std::unique_ptr<juce::Slider> makeFineDragSlider();

} // namespace synth::ui
