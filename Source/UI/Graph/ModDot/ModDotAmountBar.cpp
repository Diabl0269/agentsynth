#include "ModDotAmountBar.h"

#include "UI/Layout/FocusRing.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kTrackHeight = 4.0f;
} // namespace

// The numeric value the accessibility API sees is the percent, so a screen reader says "42", not "0.42".
class ModDotAmountBar::Value final : public juce::AccessibilityValueInterface {
public:
    explicit Value(ModDotAmountBar& bar)
        : bar_(bar) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return std::round(bar_.getValue() * 100.0f); }
    juce::String getCurrentValueAsString() const override { return bar_.getValueText(); }
    void setValue(double percent) override { bar_.change(juce::jlimit(-1.0f, 1.0f, (float)percent / 100.0f)); }
    void setValueAsString(const juce::String& text) override { setValue(text.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return {{-100.0, 100.0}, 1.0}; }

private:
    ModDotAmountBar& bar_;
};

ModDotAmountBar::ModDotAmountBar() {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

juce::String ModDotAmountBar::percentText(float value) {
    const int pct = juce::roundToInt(value * 100.0f);
    return (pct > 0 ? "+" : "") + juce::String(pct) + "%";
}

juce::String ModDotAmountBar::getValueText() const { return percentText(value_); }

void ModDotAmountBar::setValue(float value) {
    value = juce::jlimit(-1.0f, 1.0f, value);
    if (value == value_)
        return;
    value_ = value;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

juce::Range<float> ModDotAmountBar::trackRange() const {
    const float half = (float)kHandle * 0.5f;
    return {half, (float)getWidth() - half};
}

float ModDotAmountBar::xForValue(float value) const {
    const auto r = trackRange();
    return r.getStart() + (value + 1.0f) * 0.5f * r.getLength();
}

float ModDotAmountBar::valueForX(float x) const {
    const auto r = trackRange();
    const float v = r.getLength() > 0.0f ? (x - r.getStart()) / r.getLength() * 2.0f - 1.0f : 0.0f;
    return std::round(juce::jlimit(-1.0f, 1.0f, v) * 100.0f) / 100.0f;
}

void ModDotAmountBar::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    const auto bounds = getLocalBounds().toFloat();
    const float cy = bounds.getCentreY();
    const auto track = juce::Rectangle<float>(0.0f, cy - kTrackHeight * 0.5f, bounds.getWidth(), kTrackHeight);
    g.setColour(p.field);
    g.fillRoundedRectangle(track, kTrackHeight * 0.5f);

    const float zeroX = xForValue(0.0f);
    const float handleX = xForValue(value_);
    const auto colour = p.swatchFor(value_);
    g.setColour(colour);
    g.fillRect(juce::Rectangle<float>(std::min(zeroX, handleX), track.getY(), std::abs(handleX - zeroX), kTrackHeight));
    g.setColour(p.muted.withAlpha(0.6f)); // the zero tick
    g.fillRect(juce::Rectangle<float>(zeroX - 0.5f, cy - 5.0f, 1.0f, 10.0f));
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>((float)kHandle, (float)kHandle).withCentre({handleX, cy}));
    g.setColour(p.panel);
    g.drawEllipse(juce::Rectangle<float>((float)kHandle, (float)kHandle).withCentre({handleX, cy}), 1.0f);
    paintFocusRing(g, bounds.reduced(0.0f, 2.0f), *this, 6.0f);
}

void ModDotAmountBar::change(float value) {
    value = juce::jlimit(-1.0f, 1.0f, value);
    if (value == value_)
        return;
    if (!gestureOpen_ && onGestureBegin) {
        gestureOpen_ = true;
        onGestureBegin(); // captured lazily: a click that changes nothing leaves no undo step
    }
    setValue(value);
    if (onValueChanged)
        onValueChanged(value);
}

void ModDotAmountBar::mouseDown(const juce::MouseEvent& e) {
    if (onFocused)
        onFocused();
    grabKeyboardFocus();
    const float x = e.position.x;
    // Pressing the handle itself keeps it under the pointer; pressing the track jumps it there.
    grabOffset_ = std::abs(x - xForValue(value_)) <= (float)kHandle ? xForValue(value_) - x : 0.0f;
    dragging_ = true;
    change(valueForX(x + grabOffset_));
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
}

void ModDotAmountBar::applyKeyStep(float delta) {
    const float target = juce::jlimit(-1.0f, 1.0f, std::round((value_ + delta) * 100.0f) / 100.0f);
    if (target == value_)
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
