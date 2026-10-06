#include "AppLookAndFeelFader.h"
#include "AppLookAndFeel.h"
#include "KnobPainter.h"

namespace synth::theme {

// Concern: the fader (linear slider) look -- slot + fill, cap, state overlays; one draw path per knob style
// (a fader follows the knob style) sharing the slot / cap helpers.

namespace fader {

namespace {

constexpr float kSlotOverhang = 2.0f; // the slot runs this far past each end of the travel
constexpr float kFocusRingGap = 3.0f;
constexpr float kFocusRingWidth = 2.0f;

// Fits a nominal length inside `available` (never below 4 px so the cap stays visible).
float fit(float nominal, int available) { return juce::jmax(4.0f, juce::jmin(nominal, (float)available)); }

juce::Rectangle<float> slotRect(juce::Rectangle<float> travel, bool vertical, float thickness) {
    const auto centre = travel.getCentre();
    return vertical ? juce::Rectangle<float>(thickness, travel.getHeight() + 2 * kSlotOverhang).withCentre(centre)
                    : juce::Rectangle<float>(travel.getWidth() + 2 * kSlotOverhang, thickness).withCentre(centre);
}

// The part of `slot` (1 px inset) from the travel's start up to the cap centre.
juce::Rectangle<float> fillRect(juce::Rectangle<float> slot, float sliderPos, bool vertical) {
    auto fill = slot.reduced(1.0f);
    if (vertical)
        fill.setTop(juce::jlimit(fill.getY(), fill.getBottom(), sliderPos));
    else
        fill.setRight(juce::jlimit(fill.getX(), fill.getRight(), sliderPos));
    return fill;
}

void drawSlot(juce::Graphics& g, juce::Colour body, juce::Colour outline, juce::Rectangle<float> slot) {
    const float r = juce::jmin(slot.getWidth(), slot.getHeight()) * 0.5f;
    g.setColour(body);
    g.fillRoundedRectangle(slot, r);
    g.setColour(outline);
    g.drawRoundedRectangle(slot.reduced(0.5f), r - 0.5f, 1.0f);
}

void drawSolidFill(juce::Graphics& g, juce::Colour colour, juce::Rectangle<float> slot, float sliderPos,
                   bool vertical) {
    const auto fill = fillRect(slot, sliderPos, vertical);
    g.setColour(colour);
    g.fillRoundedRectangle(fill, (juce::jmin(slot.getWidth(), slot.getHeight()) - 2.0f) * 0.5f);
}

// Polished: 11 dots beside the slot, lit in the fader's colour up to the value.
void drawTickDots(juce::Graphics& g, const Theme& theme, juce::Colour colour, juce::Rectangle<float> travel,
                  float sliderPos, bool vertical, const Metrics& m) {
    constexpr int kDots = 11;
    const auto centre = travel.getCentre();
    const float off = m.slot * 0.5f + 3.5f;
    for (int i = 0; i < kDots; ++i) {
        const float t = (float)i / (float)(kDots - 1);
        const float along =
            vertical ? travel.getBottom() - t * travel.getHeight() : travel.getX() + t * travel.getWidth();
        const bool lit = vertical ? along >= sliderPos - 0.5f : along <= sliderPos + 0.5f;
        g.setColour(lit ? colour : theme.colors.textDisabled.withAlpha(0.6f));
        const juce::Point<float> at =
            vertical ? juce::Point<float>(centre.x + off, along) : juce::Point<float>(along, centre.y + off);
        g.fillEllipse(juce::Rectangle<float>(2.0f, 2.0f).withCentre(at));
    }
}

void drawPolishedSlot(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> travel, float sliderPos,
                      bool vertical, const Metrics& m, juce::Colour colour) {
    const auto& c = theme.colors;
    const auto slot = slotRect(travel, vertical, m.slot);
    drawSlot(g, c.bg0, c.border, slot);
    // Soft inner shadow down the slot's leading side.
    const auto inner = slot.reduced(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f * juce::jlimit(0.0f, 1.0f, theme.treatment.shadow)));
    g.fillRoundedRectangle(vertical ? inner.withWidth(1.5f) : inner.withHeight(1.5f), 0.75f);
    drawTickDots(g, theme, colour, travel, sliderPos, vertical, m);
    const auto fill = fillRect(slot, sliderPos, vertical);
    // 60% at the fill's start, full at the cap.
    const auto from = vertical ? fill.getBottomLeft() : fill.getTopLeft();
    const auto to = vertical ? fill.getTopLeft() : fill.getTopRight();
    if (from.getDistanceFrom(to) > 1.0f)
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.6f), from, colour, to, false));
    else
        g.setColour(colour);
    g.fillRoundedRectangle(fill, (m.slot - 2.0f) * 0.5f);
}

void drawNeonSlot(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> travel, float sliderPos, bool vertical,
                  const Metrics& m, juce::Colour colour) {
    const auto slot = slotRect(travel, vertical, m.slot);
    drawSlot(g, theme.colors.bg0, theme.colors.border, slot);
    const auto fill = fillRect(slot, sliderPos, vertical);
    const auto full = slot.reduced(1.0f);
    const float along = vertical ? full.getHeight() : full.getWidth();
    const float pos01 = along > 0.0f ? (vertical ? fill.getHeight() : fill.getWidth()) / along : 0.0f;
    const float glow = neonGlowStrength(theme, pos01);
    const float radius = (m.slot - 2.0f) * 0.5f;
    const float alphas[] = {0.18f, 0.32f, 0.55f};
    const float grows[] = {4.0f, 2.5f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        g.setColour(colour.withAlpha(0.55f * glow * alphas[i]));
        g.fillRoundedRectangle(fill.expanded(grows[i]), radius + grows[i]);
    }
    drawSolidFill(g, colour, slot, sliderPos, vertical);
}

void drawRingSlot(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> travel, float sliderPos, bool vertical,
                  juce::Colour colour) {
    const auto slot = slotRect(travel, vertical, 2.0f);
    g.setColour(theme.colors.border);
    g.fillRoundedRectangle(slot, 1.0f);
    auto fill = slot;
    if (vertical)
        fill.setTop(juce::jlimit(slot.getY(), slot.getBottom(), sliderPos));
    else
        fill.setRight(juce::jlimit(slot.getX(), slot.getRight(), sliderPos));
    g.setColour(colour);
    g.fillRoundedRectangle(fill, 1.0f);
}

void drawSlotAndFill(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> travel, float sliderPos,
                     bool vertical, const Metrics& m, KnobStyle style, juce::Colour colour) {
    const auto& c = theme.colors;
    switch (style) {
    case KnobStyle::Polished:
        drawPolishedSlot(g, theme, travel, sliderPos, vertical, m, colour);
        return;
    case KnobStyle::Neon:
        drawNeonSlot(g, theme, travel, sliderPos, vertical, m, colour);
        return;
    case KnobStyle::Ring:
        drawRingSlot(g, theme, travel, sliderPos, vertical, colour);
        return;
    case KnobStyle::Hardware: {
        const auto slot = slotRect(travel, vertical, m.slot);
        drawSlot(g, c.knobSkirt, c.border, slot);
        drawSolidFill(g, colour, slot, sliderPos, vertical);
        return;
    }
    case KnobStyle::Soft: {
        const auto slot = slotRect(travel, vertical, m.slot * 1.5f); // 50 percent wider
        drawSlot(g, c.bg0, c.border, slot);
        drawSolidFill(g, colour, slot, sliderPos, vertical);
        return;
    }
    case KnobStyle::Classic:
        break;
    }
    const auto slot = slotRect(travel, vertical, m.slot);
    drawSlot(g, c.bg0, c.border, slot);
    drawSolidFill(g, colour, slot, sliderPos, vertical);
}

juce::Rectangle<float> capBounds(juce::Rectangle<float> travel, float sliderPos, bool vertical, const Metrics& m) {
    const auto centre = travel.getCentre();
    const juce::Point<float> at =
        vertical ? juce::Point<float>(centre.x, sliderPos) : juce::Point<float>(sliderPos, centre.y);
    return juce::Rectangle<float>(m.capW, m.capH).withCentre(at);
}

juce::Colour capOutline(const Theme& theme, const State& state, juce::Colour restColour) {
    return state.dragging ? theme.colors.accent : state.hover ? theme.colors.textMuted : restColour;
}

// A line across the cap, centred `shift` px off its centre along the travel.
void drawCapLine(juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, float half, float shift,
                 juce::Colour colour, float width) {
    const auto centre = cap.getCentre();
    juce::Path line;
    if (vertical)
        line.addLineSegment({centre.x - half, centre.y + shift, centre.x + half, centre.y + shift}, 0.0f);
    else
        line.addLineSegment({centre.x + shift, centre.y - half, centre.x + shift, centre.y + half}, 0.0f);
    g.setColour(colour);
    g.strokePath(line, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Ring style: a round pointer dot instead of the pill (the pill rectangle stays the hit area).
void drawRingCap(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> cap, const State& state) {
    const float d = juce::jmin(cap.getWidth(), cap.getHeight());
    const auto dot = juce::Rectangle<float>(d, d).withCentre(cap.getCentre());
    g.setColour(theme.colors.knobPointer);
    g.fillEllipse(dot);
    if (state.dragging || state.hover) {
        g.setColour(capOutline(theme, state, theme.colors.textDisabled));
        g.drawEllipse(dot.reduced(0.5f), 1.0f);
    }
}

void drawCap(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> cap, bool vertical, const Metrics& m,
             const State& state, juce::Colour colour) {
    const auto& c = theme.colors;
    if (state.style == KnobStyle::Ring) {
        drawRingCap(g, theme, cap, state);
        return;
    }

    // Cheap drop shadow: an offset translucent copy of the cap, no blur.
    g.setColour(juce::Colours::black.withAlpha(0.35f * juce::jlimit(0.0f, 1.0f, theme.treatment.shadow)));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), m.radius);

    if (state.style == KnobStyle::Neon) {
        g.setColour(c.knobBody);
    } else {
        const auto from = cap.getTopLeft();
        const auto to = vertical ? cap.getBottomLeft() : cap.getTopRight();
        g.setGradientFill(juce::ColourGradient(c.surfaceHi, from, c.knobBody, to, false));
    }
    g.fillRoundedRectangle(cap, m.radius);

    if (state.style == KnobStyle::Soft) {
        g.setColour(colour.withAlpha(0.22f));
        g.fillRoundedRectangle(cap, m.radius);
    } else if (state.style == KnobStyle::Hardware) {
        g.setColour(c.knobCapHighlight);
        const auto inner = cap.reduced(1.0f);
        g.fillRoundedRectangle(vertical ? inner.withHeight(cap.getHeight() * 0.45f)
                                        : inner.withWidth(cap.getWidth() * 0.45f),
                               juce::jmax(0.0f, m.radius - 1.0f));
    }

    // Rest outline in textDisabled: border is too close to the cap fill on dark themes.
    const juce::Colour rest = state.style == KnobStyle::Soft ? colour.withAlpha(0.6f) : c.textDisabled;
    g.setColour(capOutline(theme, state, rest));
    g.drawRoundedRectangle(cap.reduced(0.5f), juce::jmax(0.0f, m.radius - 0.5f), 1.0f);

    // Centre line: the exact value position, like the rotary knob's pointer.
    const float half = vertical ? m.groove * 0.5f : m.groove * 0.5f - 2.0f;
    if (state.style == KnobStyle::Hardware) {
        drawCapLine(g, cap, vertical, half, -2.5f, c.knobPointer.withAlpha(0.8f), 1.0f);
        drawCapLine(g, cap, vertical, half, 2.5f, c.knobPointer.withAlpha(0.8f), 1.0f);
    } else {
        drawCapLine(g, cap, vertical, half, 0.0f, state.style == KnobStyle::Neon ? colour : c.knobPointer, 1.5f);
    }
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
    const juce::Colour colour = !state.enabled                      ? theme.colors.textDisabled
                                : state.valueColour.isTransparent() ? theme.colors.accent
                                                                    : state.valueColour;
    drawSlotAndFill(g, theme, travel, sliderPos, vertical, m, state.style, colour);
    drawCap(g, theme, cap, vertical, m, state, colour);
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
    state.style = knobAppearance.style;
    state.valueColour = knobValueColour(slider);
    fader::paint(g, theme, {x, y, width, height}, {slider.getWidth(), slider.getHeight()}, sliderPos, vertical, state);
}

} // namespace synth::theme
