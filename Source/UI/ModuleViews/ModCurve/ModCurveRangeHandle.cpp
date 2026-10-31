#include "ModCurveRangeHandle.h"

#include "UI/Graph/ModDot/ModDotPalette.h"
#include "UI/Layout/FocusRing.h"

namespace synth::ui {

namespace {
constexpr float kTrackInset = 7.0f;
constexpr float kMarkerHeight = 8.0f;
} // namespace

ModCurveRangeHandle::ModCurveRangeHandle(const juce::String& name)
    : name_(name) {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setTooltip(name + ": drag, or press Up and Down");
    setValue(0.0f);
}

void ModCurveRangeHandle::setValue(float value) {
    value_ = juce::jlimit(0.0f, 1.0f, value);
    setTitle(name_ + ", " + juce::String(juce::roundToInt(value_ * 100.0f)) + " percent");
    repaint();
}

float ModCurveRangeHandle::markerY() const {
    const float top = kTrackInset, bottom = (float)getHeight() - kTrackInset;
    return bottom - value_ * (bottom - top);
}

float ModCurveRangeHandle::valueForY(float y) const {
    const float top = kTrackInset, bottom = (float)getHeight() - kTrackInset;
    return juce::jlimit(0.0f, 1.0f, (bottom - y) / juce::jmax(1.0f, bottom - top));
}

void ModCurveRangeHandle::changeTo(float value) {
    setValue(value);
    if (onChange)
        onChange(value_);
}

void ModCurveRangeHandle::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    const auto w = (float)getWidth();
    g.setColour(p.border);
    g.fillRoundedRectangle(w * 0.5f - 1.0f, kTrackInset, 2.0f, (float)getHeight() - 2.0f * kTrackInset, 1.0f);
    const auto marker = juce::Rectangle<float>(1.0f, markerY() - kMarkerHeight * 0.5f, w - 2.0f, kMarkerHeight);
    g.setColour(p.accent);
    g.fillRoundedRectangle(marker, 3.0f);
    paintFocusRing(g, marker.expanded(1.5f), *this, 4.0f);
}

void ModCurveRangeHandle::mouseDown(const juce::MouseEvent& e) {
    dragging_ = true;
    if (onBegin)
        onBegin();
    changeTo(valueForY(e.position.y));
}

void ModCurveRangeHandle::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_)
        changeTo(valueForY(e.position.y));
}

void ModCurveRangeHandle::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (onEnd)
        onEnd();
}

bool ModCurveRangeHandle::keyPressed(const juce::KeyPress& key) {
    const float step = key.getModifiers().isShiftDown() ? 0.1f : 0.01f;
    float target = value_;
    if (key.isKeyCode(juce::KeyPress::upKey))
        target += step;
    else if (key.isKeyCode(juce::KeyPress::downKey))
        target -= step;
    else if (key.isKeyCode(juce::KeyPress::homeKey))
        target = 0.0f;
    else if (key.isKeyCode(juce::KeyPress::endKey))
        target = 1.0f;
    else
        return false;
    if (onBegin)
        onBegin();
    changeTo(target);
    if (onEnd)
        onEnd();
    return true;
}

} // namespace synth::ui
