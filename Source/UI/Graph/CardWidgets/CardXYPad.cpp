// CardXYPad.cpp -- the XY pad's look (the morph map's plot and cursor), its pointer and key handling, and its names.
#include "CardXYPad.h"
#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/MorphMap/MorphPlot.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr float kPlotInset = 8.0f; // keeps the ring at 0 or 1 inside the plate
}

CardXYPad::CardXYPad() {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setSize(kSize, kHeight);
    refreshTitle();
}

void CardXYPad::setAxes(const Axis& x, const Axis& y) {
    if (x == xAxis_ && y == yAxis_)
        return;
    xAxis_ = x;
    yAxis_ = y;
    refreshTitle();
    repaint();
}

void CardXYPad::setValues(float x, float y) {
    x = juce::jlimit(0.0f, 1.0f, x);
    y = juce::jlimit(0.0f, 1.0f, y);
    if (x == x_ && y == y_)
        return;
    x_ = x;
    y_ = y;
    refreshTitle();
    repaint();
}

juce::String CardXYPad::valueText(float position, const Axis& axis) const {
    return juce::String(axis.bipolar ? position * 2.0f - 1.0f : position, 2);
}

juce::String CardXYPad::describe() const {
    return xAxis_.name + " " + valueText(x_, xAxis_) + ", " + yAxis_.name + " " + valueText(y_, yAxis_);
}

void CardXYPad::refreshTitle() {
    setTitle("XY pad " + describe());
    setDescription("Drag, or use the arrow keys: left and right move " + xAxis_.name + ", up and down move " +
                   yAxis_.name + ". Shift moves in fine steps.");
}

juce::String CardXYPad::getTooltip() {
    return "XY pad: " + describe() + ". Drag, or use the arrow keys (Shift for fine steps)";
}

// The plot is the square the pad's width makes, inset so the ring at an edge is not clipped; the readout sits under it.
juce::Rectangle<float> CardXYPad::plateArea() const {
    const float side = (float)std::min(getWidth(), getHeight() - kReadoutHeight);
    return juce::Rectangle<float>(((float)getWidth() - side) * 0.5f, 0.0f, side, side);
}

juce::Rectangle<float> CardXYPad::plotArea() const { return plateArea().reduced(kPlotInset); }

// Y runs upwards: a full Y is the top of the plot.
juce::Point<float> CardXYPad::pixelsFor(float x, float y) const {
    return morphplot::toPixels(plotArea(), {x, 1.0f - y});
}

void CardXYPad::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto plate = plateArea();
    morphplot::paintPlot(g, plate.reduced(0.5f), plotArea(), colors);
    morphplot::paintCursor(g, pixelsFor(x_, y_), 1.0f, colors);
    g.setColour(colors.textMuted);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(describe(), 0, (int)plate.getBottom(), getWidth(), kReadoutHeight, juce::Justification::centred, true);
}

void CardXYPad::paintOverChildren(juce::Graphics& g) {
    paintFocusRing(g, plateArea(), *this, morphplot::kCornerRadius);
}

void CardXYPad::moveTo(float x, float y) {
    x = juce::jlimit(0.0f, 1.0f, x);
    y = juce::jlimit(0.0f, 1.0f, y);
    if (x == x_ && y == y_)
        return;
    setValues(x, y);
    if (onMoved)
        onMoved(x_, y_);
}

void CardXYPad::moveToPixels(juce::Point<float> pixels) {
    const auto unit = morphplot::toUnit(plotArea(), pixels);
    moveTo(unit.x, 1.0f - unit.y);
}

void CardXYPad::mouseDown(const juce::MouseEvent& e) {
    dragging_ = false;
    if (e.mods.isPopupMenu() || (refusesEdit && refusesEdit()))
        return;
    dragging_ = true;
    if (onGestureStart)
        onGestureStart();
    moveToPixels(e.position);
}

void CardXYPad::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_)
        moveToPixels(e.position);
}

void CardXYPad::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

bool CardXYPad::nudge(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const float step = mods.isShiftDown() ? kFineStep : kStep;
    float dx = 0.0f;
    float dy = 0.0f;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey)
        dx = -step;
    else if (code == juce::KeyPress::rightKey)
        dx = step;
    else if (code == juce::KeyPress::upKey)
        dy = step;
    else if (code == juce::KeyPress::downKey)
        dy = -step;
    else
        return false;
    if (refusesEdit && refusesEdit())
        return true;
    if (onGestureStart)
        onGestureStart();
    moveTo(x_ + dx, y_ + dy);
    if (onGestureEnd)
        onGestureEnd();
    return true;
}

bool CardXYPad::keyPressed(const juce::KeyPress& key) { return nudge(key); }

} // namespace synth::ui
