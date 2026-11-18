#pragma once

// ControlDepthReference.h -- the knob and fader painters as they were before the Depth setting reached controls, kept
// verbatim as the golden for Soft (ControlDepthTests.cpp: Soft must match them pixel for pixel in every theme).
// Do not edit and do not call from the app; only the namespace and function names differ from the originals.

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include "UI/Theme/AppLookAndFeel/KnobPainter.h"
#include "UI/Theme/AppLookAndFeel/KnobPainterInternal.h"

namespace synth::theme::refknobs {
using namespace synth::theme::knobs;

namespace {

// Classic's glowing tip at the value end of the arc (brighter the further the value is from its origin)
// and the small tick marking 12 o'clock.
void drawClassicTipAndTick(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto tip = geo.pointAt(geo.arcRadius, geo.valueAngle);
    fillGlow(g, tip, geo.size * 0.095f, valueColour, 0.15f + 0.7f * geo.light);
    fillDisc(g, tip, juce::jmax(1.0f, geo.size * 0.018f), juce::Colours::white.withAlpha(0.8f * geo.light));

    g.setColour(theme.colors.textMuted.withAlpha(0.6f));
    g.drawLine(juce::Line<float>(geo.pointAt(geo.half - 6.0f, 0.0f), geo.pointAt(geo.half - 9.0f, 0.0f)), 1.2f);
}

// Classic's body: a deep radial gradient lit from the top left, a soft drop shadow and a bevel rim.
void drawClassicBody(juce::Graphics& g, const Geo& geo, const Theme& theme) {
    const auto& c = theme.colors;
    const float r = geo.bodyRadius;
    fillDiscShadow(g, geo.centre.translated(0.0f, geo.size * 0.012f), r, geo.size * 0.02f, 0.45f);

    const auto body = geo.discBounds(r);
    const juce::Point<float> focal(body.getX() + body.getWidth() * 0.36f, body.getY() + body.getHeight() * 0.3f);
    juce::ColourGradient fill(c.surfaceHi.interpolatedWith(juce::Colours::white, 0.1f), focal, c.knobBody.darker(0.4f),
                              focal.translated(body.getWidth() * 0.95f, 0.0f), true);
    fill.addColour(0.5, c.surfaceHi);
    g.setGradientFill(fill);
    g.fillEllipse(body);
    g.setColour(c.border);
    g.drawEllipse(body, theme.metrics.borderWidth);

    const auto bevel = geo.discBounds(r - 1.2f);
    juce::ColourGradient rim(juce::Colours::white.withAlpha(0.45f), bevel.getTopLeft(),
                             juce::Colours::black.withAlpha(0.35f), bevel.getBottomRight(), false);
    rim.addColour(0.5, juce::Colours::transparentWhite);
    g.setGradientFill(rim);
    g.drawEllipse(bevel, 1.3f);
}

} // namespace

void paintClassicRef(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawTrackAndValueArc(g, geo, theme, valueColour);
    drawClassicTipAndTick(g, geo, theme, valueColour);
    drawClassicBody(g, geo, theme);
    g.setColour(theme.colors.knobPointer);
    juce::Path pointer;
    pointer.startNewSubPath(geo.centre);
    pointer.lineTo(geo.pointAt(geo.bodyRadius * 0.92f, geo.valueAngle));
    g.strokePath(pointer, roundedStroke(2.0f));

    const auto& tr = theme.treatment;
    if (tr.style == ThemeStyle::Textured && tr.texture > 0.0f) {
        g.setColour(theme.colors.border.withAlpha(tr.texture * 0.25f));
        for (int i = 1; i <= 3; ++i)
            g.drawEllipse(geo.discBounds(geo.bodyRadius * (0.4f + 0.18f * (float)i)), 0.6f);
    }
}

namespace paintChunky_ref {
namespace {

constexpr int kLobes = 10;

// The flower outline at rest (a lobe at 12 o'clock), rotated to the value. Lobes are rounded and the
// valleys between them pinched, from a sharpened |cos| profile.
juce::Path flowerPath(const Geo& geo) {
    const float outer = geo.size * 0.37f;
    const float valley = geo.size * 0.28f;
    constexpr int kPoints = 160;
    juce::Path flower;
    for (int i = 0; i < kPoints; ++i) {
        const float angle = juce::MathConstants<float>::twoPi * (float)i / (float)kPoints;
        const float lobe = std::pow(std::abs(std::cos(angle * (float)kLobes * 0.5f)), 0.6f);
        const auto p = geo.pointAt(valley + (outer - valley) * lobe, angle + geo.valueAngle);
        if (i == 0)
            flower.startNewSubPath(p);
        else
            flower.lineTo(p);
    }
    flower.closeSubPath();
    return flower;
}

void drawFlower(juce::Graphics& g, const Geo& geo, juce::Colour valueColour) {
    const auto flower = flowerPath(geo);
    g.setColour(valueColour);
    g.fillPath(flower);

    // Shading lit from the top left: a highlight fading out by the middle, a darker far rim.
    const auto box = geo.discBounds(geo.size * 0.37f);
    const juce::Point<float> focal(box.getX() + box.getWidth() * 0.35f, box.getY() + box.getHeight() * 0.3f);
    juce::ColourGradient shade(juce::Colours::white.withAlpha(0.35f), focal, juce::Colours::black.withAlpha(0.3f),
                               focal.translated(box.getWidth() * 0.85f, 0.0f), true);
    shade.addColour(0.55, juce::Colours::transparentWhite);
    g.setGradientFill(shade);
    g.fillPath(flower);

    g.setColour(valueColour.interpolatedWith(juce::Colours::black, 0.4f));
    g.strokePath(flower, juce::PathStrokeType(juce::jmax(1.0f, geo.size * 0.012f), juce::PathStrokeType::curved));
}

void drawCap(juce::Graphics& g, const Geo& geo, juce::Colour valueColour) {
    const auto cap = geo.discBounds(geo.size * 0.22f);
    const juce::Point<float> focal(cap.getX() + cap.getWidth() * 0.38f, cap.getY() + cap.getHeight() * 0.32f);
    juce::ColourGradient cream(juce::Colour(0xfffffdf7), focal, juce::Colour(0xffcdbfa4),
                               focal.translated(cap.getWidth() * 0.85f, 0.0f), true);
    cream.addColour(0.7, juce::Colour(0xffefe6d4));
    g.setGradientFill(cream);
    g.fillEllipse(cap);
    g.setColour(valueColour.interpolatedWith(juce::Colour(0xff3a2a1a), 0.5f));
    g.drawEllipse(cap, juce::jmax(1.0f, geo.size * 0.015f));

    // A glossy highlight, tilted, on the cap's upper left.
    juce::Path gloss;
    const float rx = geo.size * 0.09f;
    const float ry = geo.size * 0.045f;
    gloss.addEllipse(-rx, -ry, rx * 2.0f, ry * 2.0f);
    gloss.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::pi / 6.0f)
                             .translated(geo.centre.x - geo.size * 0.05f, geo.centre.y - geo.size * 0.08f));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.fillPath(gloss);
}

} // namespace

void paint(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawTrackAndValueArc(g, geo, theme, valueColour);
    fillDiscShadow(g, geo.centre.translated(0.0f, geo.size * 0.04f), geo.size * 0.33f, geo.size * 0.03f, 0.55f);
    drawFlower(g, geo, valueColour);
    drawCap(g, geo, valueColour);

    juce::Path pointer;
    pointer.startNewSubPath(geo.pointAt(geo.size * 0.03f, geo.valueAngle));
    pointer.lineTo(geo.pointAt(geo.size * 0.18f, geo.valueAngle));
    g.setColour(juce::Colour(0xff2a2320));
    g.strokePath(pointer, roundedStroke(geo.size * 0.055f));
}

} // namespace paintChunky_ref

namespace paintAnalog_ref {
namespace {

constexpr int kTickCount = 11;

// The printed ink: cream on a dark theme, the text colour on a light one where cream would vanish.
juce::Colour scaleInk(const Theme& theme) {
    return isLightTheme(theme) ? theme.colors.textPrimary.withAlpha(0.8f) : juce::Colour(0xffe9e3d2);
}

// The knob's radius once the printed numbers (if any) have their margin inside the bounds.
float dialRadius(const Geo& geo, bool numbers) { return numbers ? geo.half - geo.size * 0.08f * 1.5f : geo.half; }

void drawTicks(juce::Graphics& g, const Geo& geo, const Theme& theme, float outer) {
    const float k = juce::jlimit(0.8f, 1.0f, geo.size / 54.0f);
    g.setColour(scaleInk(theme));
    for (int i = 0; i < kTickCount; ++i) {
        const bool major = i == 0 || i == kTickCount / 2 || i == kTickCount - 1;
        const float angle = geo.angleAt((float)i / (float)(kTickCount - 1));
        const float length = (major ? 5.0f : 3.0f) * k;
        g.drawLine(juce::Line<float>(geo.pointAt(outer - 0.5f, angle), geo.pointAt(outer - 0.5f - length, angle)),
                   major ? 1.4f : 0.9f);
    }
}

void drawNumbers(juce::Graphics& g, const Geo& geo, const Theme& theme, float dial) {
    const float textHeight = geo.size * 0.08f;
    const float radius = dial + textHeight * 0.85f;
    const char* labels[3] = {"0", "5", "10"};
    const char* bipolarLabels[3] = {"-5", "0", "+5"};
    const float at[3] = {0.0f, 0.5f, 1.0f};
    g.setColour(scaleInk(theme));
    g.setFont(AppLookAndFeel::uiSemiBoldFont(textHeight));
    for (int i = 0; i < 3; ++i) {
        const auto centre = geo.pointAt(radius, geo.angleAt(at[i]));
        const auto box = juce::Rectangle<float>(textHeight * 2.4f, textHeight * 1.3f).withCentre(centre);
        g.drawText(geo.bipolar() ? bipolarLabels[i] : labels[i], box, juce::Justification::centred, false);
    }
}

// The skirt: a black disc with sixty lighter ridges, darkened towards its rim, a top-lit edge.
void drawSkirt(juce::Graphics& g, const Geo& geo, float radius) {
    fillDiscShadow(g, geo.centre.translated(0.0f, geo.size * 0.027f), radius, geo.size * 0.05f, 0.75f);
    const auto skirt = geo.discBounds(radius);
    g.setColour(juce::Colour(0xff17181a));
    g.fillEllipse(skirt);

    constexpr int kRidges = 60;
    const float ridge = juce::MathConstants<float>::twoPi / (float)kRidges;
    juce::Path ridges;
    for (int i = 0; i < kRidges; ++i) {
        const float from = (float)i * ridge + ridge * 0.5f;
        ridges.addPieSegment(skirt, from, from + ridge * 0.25f, 0.0f);
    }
    g.setColour(juce::Colour(0xff34363a));
    g.fillPath(ridges);

    juce::ColourGradient rim(juce::Colours::transparentBlack, geo.centre, juce::Colours::black.withAlpha(0.35f),
                             geo.centre.translated(radius, 0.0f), true);
    rim.addColour(0.58, juce::Colours::transparentBlack);
    g.setGradientFill(rim);
    g.fillEllipse(skirt);

    juce::ColourGradient edge(juce::Colours::white.withAlpha(0.18f), skirt.getCentreX(), skirt.getY(),
                              juce::Colours::black.withAlpha(0.5f), skirt.getCentreX(), skirt.getBottom(), false);
    g.setGradientFill(edge);
    g.drawEllipse(skirt.reduced(0.5f), 1.0f);
}

// The cap: brushed metal (a fixed conic sweep of greys, so the light does not turn with the knob) with
// a soft top-left highlight and a dark hairline edge.
void drawCap(juce::Graphics& g, const Geo& geo, float radius) {
    static const juce::uint32 kBrushed[] = {0xffc9ccd0, 0xff8a8f96, 0xffe6e8ea, 0xff7b8088, 0xffc9ccd0,
                                            0xff949aa1, 0xffeceef0, 0xff80858c, 0xffc9ccd0};
    constexpr int kStops = (int)(sizeof(kBrushed) / sizeof(kBrushed[0]));
    constexpr int kSegments = 64;
    const auto cap = geo.discBounds(radius);
    const float start = juce::degreesToRadians(20.0f);
    const float step = juce::MathConstants<float>::twoPi / (float)kSegments;
    for (int i = 0; i < kSegments; ++i) {
        const float t = ((float)i + 0.5f) / (float)kSegments * (float)(kStops - 1);
        const int a = juce::jmin(kStops - 2, (int)t);
        const auto colour = juce::Colour(kBrushed[a]).interpolatedWith(juce::Colour(kBrushed[a + 1]), t - (float)a);
        juce::Path wedge;
        wedge.addPieSegment(cap, start + (float)i * step, start + (float)(i + 1) * step + 0.01f, 0.0f);
        g.setColour(colour);
        g.fillPath(wedge);
    }

    const juce::Point<float> focal(cap.getX() + cap.getWidth() * 0.34f, cap.getY() + cap.getHeight() * 0.28f);
    juce::ColourGradient shine(juce::Colours::white.withAlpha(0.55f), focal, juce::Colours::transparentWhite,
                               focal.translated(cap.getWidth() * 0.55f, 0.0f), true);
    g.setGradientFill(shine);
    g.fillEllipse(cap);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(cap, 1.0f);
}

void drawLine(juce::Graphics& g, const Geo& geo, float skirtRadius) {
    juce::Path line;
    line.startNewSubPath(geo.pointAt(skirtRadius * 0.17f, geo.valueAngle));
    line.lineTo(geo.pointAt(skirtRadius * 0.95f, geo.valueAngle));
    const float width = juce::jmax(1.6f, geo.size * 0.026f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.strokePath(line, roundedStroke(width + 1.0f));
    g.setColour(juce::Colour(0xfff7f5ee));
    g.strokePath(line, roundedStroke(width));
}

} // namespace

void paint(juce::Graphics& g, const Geo& geo, const Theme& theme) {
    const bool numbers = geo.size >= kAnalogNumbersMinSize;
    const float dial = dialRadius(geo, numbers);
    const float skirt = dial - 7.0f * juce::jlimit(0.8f, 1.0f, geo.size / 54.0f);
    drawTicks(g, geo, theme, dial);
    if (numbers)
        drawNumbers(g, geo, theme, dial);
    drawSkirt(g, geo, skirt);
    drawCap(g, geo, skirt * 0.62f);
    drawLine(g, geo, skirt);
}

} // namespace paintAnalog_ref

} // namespace synth::theme::refknobs

namespace synth::theme::reffader {
using fader::kLargeMinHeight;
using fader::kLargeMinWidth;
using fader::Metrics;
using fader::State;

namespace {

// The thickest ring any built-in theme paints (a light theme's, 2.5 x a 1 px border): the room kept for it.
constexpr float kWidestRingThickness = 2.5f;
constexpr float kSlotOverhang = 2.0f; // the slot runs this far past each end of the travel

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

// Analog: 11 dots beside the slot, lit in the fader's colour up to the value.
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

void drawAnalogSlot(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> travel, float sliderPos,
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
    case KnobStyle::Analog:
        drawAnalogSlot(g, theme, travel, sliderPos, vertical, m, colour);
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
    case KnobStyle::Classic:
        break;
    }
    const auto slot = slotRect(travel, vertical, m.slot);
    drawSlot(g, c.bg0, c.border, slot);
    drawSolidFill(g, colour, slot, sliderPos, vertical);
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

    if (state.style == KnobStyle::Hardware) {
        g.setColour(c.knobCapHighlight);
        const auto inner = cap.reduced(1.0f);
        g.fillRoundedRectangle(vertical ? inner.withHeight(cap.getHeight() * 0.45f)
                                        : inner.withWidth(cap.getWidth() * 0.45f),
                               juce::jmax(0.0f, m.radius - 1.0f));
    }

    // Rest outline in textDisabled: border is too close to the cap fill on dark themes.
    g.setColour(capOutline(theme, state, c.textDisabled));
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
    const float reach = synth::ui::kFocusRingGap + synth::ui::focusRingThickness(theme);
    synth::ui::paintFocusRingForTheme(g, theme, cap.expanded(reach), m.radius + reach);
}

} // namespace

juce::Rectangle<float> capBounds(juce::Rectangle<float> travel, float sliderPos, bool vertical, const Metrics& m) {
    const auto centre = travel.getCentre();
    const juce::Point<float> at =
        vertical ? juce::Point<float>(centre.x, sliderPos) : juce::Point<float>(sliderPos, centre.y);
    return juce::Rectangle<float>(m.capW, m.capH).withCentre(at);
}

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
    // A horizontal fader sits beside its value box: its cap and focus ring (the shared gap plus its stroke beyond the
    // cap) end a clear gap before the box, so the ring never touches the text at the maximum.
    const float margin = vertical ? 2.0f : synth::ui::kFocusRingGap + kWidestRingThickness + 2.0f;
    return (int)std::ceil(along * 0.5f + margin);
}

void paint(juce::Graphics& g, const Theme& theme, juce::Rectangle<int> travelBounds, juce::Point<int> sliderSize,
           float sliderPos, bool vertical, const State& state) {
    const auto travel = travelBounds.toFloat();
    const auto m = metricsFor(vertical, sliderSize.x, sliderSize.y);
    const auto cap = reffader::capBounds(travel, sliderPos, vertical, m);

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

} // namespace synth::theme::reffader
