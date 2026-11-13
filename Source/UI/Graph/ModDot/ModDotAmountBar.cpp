#include "ModDotAmountBar.h"

#include <cmath>

namespace synth::ui {

// The numeric value the accessibility API sees is the percent, so a screen reader says "42", not "0.42".
class ModDotAmountBar::Value final : public juce::AccessibilityValueInterface {
public:
    explicit Value(ModDotAmountBar& bar)
        : bar_(bar) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return std::round(bar_.amount() * 100.0f); }
    juce::String getCurrentValueAsString() const override { return bar_.getValueText(); }
    void setValue(double percent) override { bar_.change(juce::jlimit(-1.0f, 1.0f, (float)percent / 100.0f)); }
    void setValueAsString(const juce::String& text) override { setValue(text.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return {{-100.0, 100.0}, 1.0}; }

private:
    ModDotAmountBar& bar_;
};

ModDotAmountBar::ModDotAmountBar() {
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setRange(-1.0, 1.0, 0.0);
    setValue(0.0, juce::dontSendNotification);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

juce::String ModDotAmountBar::percentText(float value) {
    const int pct = juce::roundToInt(value * 100.0f);
    return (pct > 0 ? "+" : "") + juce::String(pct) + "%";
}

juce::String ModDotAmountBar::getValueText() const { return percentText(amount()); }

void ModDotAmountBar::setAmount(float value) {
    value = juce::jlimit(-1.0f, 1.0f, value);
    if (value == amount())
        return;
    setValue((double)value, juce::dontSendNotification);
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

float ModDotAmountBar::xForValue(float value) const { return (float)getPositionOfValue((double)value); }

float ModDotAmountBar::valueForX(float x) const {
    const float from = xForValue(-1.0f);
    const float to = xForValue(1.0f);
    const float v = to > from ? (x - from) / (to - from) * 2.0f - 1.0f : 0.0f;
    return std::round(juce::jlimit(-1.0f, 1.0f, v) * 100.0f) / 100.0f;
}

void ModDotAmountBar::change(float value) {
    value = juce::jlimit(-1.0f, 1.0f, value);
    if (value == amount())
        return;
    if (!gestureOpen_ && onGestureBegin) {
        gestureOpen_ = true;
        onGestureBegin(); // captured lazily: a click that changes nothing leaves no undo step
    }
    setAmount(value);
    if (onValueChanged)
        onValueChanged(value);
}

void ModDotAmountBar::mouseDown(const juce::MouseEvent& e) {
    if (onFocused)
        onFocused();
    grabKeyboardFocus();
    const float x = e.position.x;
    // Pressing the cap itself keeps it under the pointer; pressing the track jumps it there.
    grabOffset_ = std::abs(x - xForValue(amount())) <= (float)kGrab ? xForValue(amount()) - x : 0.0f;
    dragging_ = true;
    change(valueForX(x + grabOffset_));
    repaint();
}

void ModDotAmountBar::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_)
        change(valueForX(e.position.x + grabOffset_));
}

void ModDotAmountBar::mouseUp(const juce::MouseEvent&) {
    dragging_ = false;
    if (gestureOpen_) {
        gestureOpen_ = false;
        if (onGestureEnd)
            onGestureEnd();
    }
    repaint();
}

void ModDotAmountBar::applyKeyStep(float delta) {
    const float target = juce::jlimit(-1.0f, 1.0f, std::round((amount() + delta) * 100.0f) / 100.0f);
    if (target == amount())
        return;
    change(target);
    if (gestureOpen_) {
        gestureOpen_ = false;
        if (onGestureEnd)
            onGestureEnd();
    }
}

bool ModDotAmountBar::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const float step = mods.isShiftDown() ? kShiftStep : kStep;
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        applyKeyStep(-step);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::rightKey)) {
        applyKeyStep(step);
        return true;
    }
    return false;
}

void ModDotAmountBar::focusGained(FocusChangeType) {
    repaint();
    if (onFocused)
        onFocused();
}

std::unique_ptr<juce::AccessibilityHandler> ModDotAmountBar::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
}

} // namespace synth::ui
