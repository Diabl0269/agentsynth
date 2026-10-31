#include "ModCurveChip.h"

#include <cmath>

#include "UI/Graph/ModDot/ModDotPalette.h"
#include "UI/Layout/FocusRing.h"

namespace synth::ui {

void ModCurvePresetButton::lookAndFeelChanged() {
    juce::TextButton::lookAndFeelChanged();
    const auto fill = findColour(juce::TextButton::buttonOnColourId);
    const auto contrast = [fill](juce::Colour text) {
        const auto luminance = [](juce::Colour c) {
            const auto channel = [](float v) {
                return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * channel(c.getFloatRed()) + 0.7152f * channel(c.getFloatGreen()) +
                   0.0722f * channel(c.getFloatBlue());
        };
        const auto a = luminance(fill), b = luminance(text);
        return (juce::jmax(a, b) + 0.05f) / (juce::jmin(a, b) + 0.05f);
    };
    setColour(juce::TextButton::textColourOnId, contrast(juce::Colours::white) >= contrast(juce::Colours::black)
                                                    ? juce::Colours::white
                                                    : juce::Colours::black);
}

void ModCurvePresetButton::parentHierarchyChanged() {
    juce::TextButton::parentHierarchyChanged();
    lookAndFeelChanged();
}

void paintModCurveGlyph(juce::Graphics& g, juce::Rectangle<float> area, const synth::ModCurve& curve,
                        juce::Colour colour, float thickness) {
    synth::ModCurve::Table table;
    curve.renderTable(table);
    constexpr int kSteps = 16;
    juce::Path path;
    for (int i = 0; i <= kSteps; ++i) {
        const float t = (float)i / (float)kSteps;
        const float v =
            juce::jlimit(0.0f, 1.0f, table[(size_t)juce::roundToInt(t * (float)synth::ModCurve::kTableSize)]);
        const juce::Point<float> p(area.getX() + t * area.getWidth(), area.getBottom() - v * area.getHeight());
        if (i == 0)
            path.startNewSubPath(p);
        else
            path.lineTo(p);
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

ModCurveChip::ModCurveChip()
    : juce::Button("Curve") {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle("Curve");
    setTooltip("Edit this modulation's response curve");
}

void ModCurveChip::setSourceName(const juce::String& sourceName) {
    setTitle(sourceName.isNotEmpty() ? sourceName + " curve" : juce::String("Curve"));
    setTooltip(sourceName.isNotEmpty() ? "Edit the response curve of " + sourceName
                                       : juce::String("Edit this modulation's response curve"));
}

void ModCurveChip::setTarget(std::shared_ptr<ModCurveEditTarget> target, const juce::String& sourceName) {
    target_ = std::move(target);
    setSourceName(sourceName);
    refresh();
}

void ModCurveChip::refresh() {
    const auto curve = target_ != nullptr ? target_->read() : std::nullopt;
    const auto next = curve.value_or(synth::ModCurve::linear());
    if (next == shown_)
        return;
    shown_ = next;
    bent_ = !next.isLinear();
    repaint();
}

bool ModCurveChip::keyPressed(const juce::KeyPress& key) {
    if (isEnabled() && (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)) {
        triggerClick();
        return true;
    }
    return false;
}

void ModCurveChip::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto p = modDotPaletteFor(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(down || over ? p.hover : p.field);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(bent_ ? p.accent : p.border);
    g.drawRoundedRectangle(bounds, 4.0f, bent_ ? 1.5f : 1.0f);
    paintModCurveGlyph(g, bounds.reduced(5.0f, 4.0f), shown_, bent_ ? p.accent : p.muted);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, 4.0f);
}

} // namespace synth::ui
