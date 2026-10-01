#include "AppLookAndFeelFader.h"
#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: the fader (linear slider) look -- slot + fill, cap, state overlays.

namespace fader {

namespace {

constexpr float kSlotOverhang = 2.0f; // the slot runs this far past each end of the travel
constexpr float kFocusRingGap = 3.0f;
constexpr float kFocusRingWidth = 2.0f;

// Fits a nominal length inside `available` (never below 4 px so the cap stays visible).
float fit(float nominal, int available) { return juce::jmax(4.0f, juce::jmin(nominal, (float)available)); }

void drawSlotAndFill(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> travel, float sliderPos,
                     bool vertical, const Metrics& m, bool enabled) {
    const auto& c = theme.colors;
    const auto centre = travel.getCentre();
    juce::Rectangle<float> slot =
        vertical
            ? juce::Rectangle<float>(m.slot, travel.getHeight() + 2 * kSlotOverhang).withCentre({centre.x, centre.y})
            : juce::Rectangle<float>(travel.getWidth() + 2 * kSlotOverhang, m.slot).withCentre({centre.x, centre.y});
    g.setColour(c.bg0);
    g.fillRoundedRectangle(slot, m.slot * 0.5f);
    g.setColour(c.border);
    g.drawRoundedRectangle(slot.reduced(0.5f), m.slot * 0.5f - 0.5f, 1.0f);

    auto fill = slot.reduced(1.0f);
    if (vertical)
        fill.setTop(juce::jlimit(fill.getY(), fill.getBottom(), sliderPos));
    else
        fill.setRight(juce::jlimit(fill.getX(), fill.getRight(), sliderPos));
    g.setColour(enabled ? c.accent : c.textDisabled);
    g.fillRoundedRectangle(fill, (m.slot - 2.0f) * 0.5f);
}

juce::Rectangle<float> capBounds(juce::Rectangle<float> travel, float sliderPos, bool vertical, const Metrics& m) {
    const auto centre = travel.getCentre();
    const juce::Point<float> at =
        vertical ? juce::Point<float>(centre.x, sliderPos) : juce::Point<float>(sliderPos, centre.y);
    return juce::Rectangle<float>(m.capW, m.capH).withCentre(at);
}

void drawCap(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> cap, bool vertical, const Metrics& m,
             const State& state) {
    const auto& c = theme.colors;

    // Cheap drop shadow: an offset translucent copy of the cap, no blur.
    g.setColour(juce::Colours::black.withAlpha(0.35f * juce::jlimit(0.0f, 1.0f, theme.treatment.shadow)));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), m.radius);

    const auto from = cap.getTopLeft();
    const auto to = vertical ? cap.getBottomLeft() : cap.getTopRight();
    g.setGradientFill(juce::ColourGradient(c.surfaceHi, from, c.knobBody, to, false));
    g.fillRoundedRectangle(cap, m.radius);

    // Rest outline in textDisabled: border is too close to the cap fill on dark themes.
    const juce::Colour outline = state.dragging ? c.accent : state.hover ? c.textMuted : c.textDisabled;
    g.setColour(outline);
    g.drawRoundedRectangle(cap.reduced(0.5f), juce::jmax(0.0f, m.radius - 0.5f), 1.0f);

    // Centre line: the exact value position, like the rotary knob's pointer.
    const auto centre = cap.getCentre();
    const float half = vertical ? m.groove * 0.5f : m.groove * 0.5f - 2.0f;
    juce::Path line;
    if (vertical)
        line.addLineSegment({centre.x - half, centre.y, centre.x + half, centre.y}, 0.0f);
    else
        line.addLineSegment({centre.x, centre.y - half, centre.x, centre.y + half}, 0.0f);
    g.setColour(c.knobPointer);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawFocusRing(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> cap, const Metrics& m) {
    g.setColour(theme.colors.accent);
    g.drawRoundedRectangle(cap.expanded(kFocusRingGap), m.radius + kFocusRingGap, kFocusRingWidth);
}

} // namespace

Metrics metricsFor(bool vertical, int width, int height) {
    if (!vertical)
        return {4.0f, 10.0f, fit(18.0f, height - 2), 3.0f, 10.0f};
    if (width >= kLargeMinWidth && height >= kLargeMinHeight)
        return {6.0f, fit(30.0f, width), 14.0f, 4.0f, 18.0f};
    return {4.0f, fit(18.0f, width), 7.0f, 2.0f, 10.0f};
}

int thumbRadiusFor(bool vertical, int width, int height) {
    const auto m = metricsFor(vertical, width, height);
    const float along = vertical ? m.capH : m.capW;
    return (int)std::ceil(along * 0.5f) + 2;
}

void paint(juce::Graphics& g, const Theme& theme, juce::Rectangle<int> travelBounds, juce::Point<int> sliderSize,
           float sliderPos, bool vertical, const State& state) {
    const auto travel = travelBounds.toFloat();
    const auto m = metricsFor(vertical, sliderSize.x, sliderSize.y);
    const auto cap = capBounds(travel, sliderPos, vertical, m);

    const bool greyed = !state.enabled || state.dimmed;
    if (greyed)
        g.beginTransparencyLayer(AppLookAndFeel::kDisabledControlAlpha);
    drawSlotAndFill(g, theme, travel, sliderPos, vertical, m, state.enabled);
    drawCap(g, theme, cap, vertical, m, state);
    if (greyed)
        g.endTransparencyLayer();

    if (state.focused && state.enabled)
        drawFocusRing(g, theme, cap, m);
}

} // namespace fader

int AppLookAndFeel::getSliderThumbRadius(juce::Slider& slider) {
    if (!slider.isBar() && (slider.isHorizontal() || slider.isVertical()))
        return fader::thumbRadiusFor(slider.isVertical(), slider.getWidth(), slider.getHeight());
    return LookAndFeel_V4::getSliderThumbRadius(slider);
}

void AppLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float /*minSliderPos*/, float /*maxSliderPos*/, juce::Slider::SliderStyle style,
                                      juce::Slider& slider) {
    const bool vertical = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);
    fader::State state;
    state.enabled = slider.isEnabled();
    state.dimmed = paintsDimmed(slider);
    state.dragging = slider.isMouseButtonDown();
    state.hover = slider.isMouseOverOrDragging() && !state.dragging;
    state.focused = slider.hasKeyboardFocus(true);
    fader::paint(g, theme, {x, y, width, height}, {slider.getWidth(), slider.getHeight()}, sliderPos, vertical, state);
}

} // namespace synth::theme
