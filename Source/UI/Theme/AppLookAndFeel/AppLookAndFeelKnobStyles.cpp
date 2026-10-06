#include "AppLookAndFeel.h"
#include "KnobPainter.h"

namespace synth::theme {

// Concern: the six knob looks (Settings > Appearance > Controls). Every style shares one geometry --
// the arc radius, body radius and 270 degree sweep -- so the modulation ring and the mod-ring
// anchors painted elsewhere never move when the style changes. AppLookAndFeelSliders.cpp keeps the
// dim layer and the focus ring around this.

namespace {

constexpr float kReferenceSize = 44.0f; // the designer's mockups are 44 px knobs; sizes scale from it

struct Geo {
    juce::Point<float> centre;
    float size = 0.0f;
    float scale = 1.0f; // size / kReferenceSize
    float arcRadius = 0.0f;
    float bodyRadius = 0.0f;
    float trackWidth = 0.0f;
    float startAngle = 0.0f;
    float endAngle = 0.0f;
    float valueAngle = 0.0f;
    float pos = 0.0f;

    juce::Point<float> pointAt(float radius, float angle) const noexcept {
        return {centre.x + std::sin(angle) * radius, centre.y - std::cos(angle) * radius};
    }
    juce::Rectangle<float> discBounds(float radius) const noexcept {
        return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
    }
};

Geo makeGeo(const Theme& theme, juce::Rectangle<float> bounds, float pos01) {
    Geo geo;
    geo.pos = juce::jlimit(0.0f, 1.0f, pos01);
    geo.size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    geo.scale = geo.size / kReferenceSize;
    geo.centre = bounds.getCentre();
    geo.trackWidth = theme.metrics.knobTrackWidth;
    geo.arcRadius = geo.size * 0.5f - geo.trackWidth;
    geo.bodyRadius = geo.size * 0.26f;
    geo.startAngle = AppLookAndFeel::kRotaryStart;
    geo.endAngle = AppLookAndFeel::kRotaryEnd;
    geo.valueAngle = geo.startAngle + geo.pos * (geo.endAngle - geo.startAngle);
    return geo;
}

juce::PathStrokeType roundedStroke(float width) {
    return juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}

juce::Path arcPath(const Geo& geo, float fromAngle, float toAngle) {
    juce::Path path;
    path.addCentredArc(geo.centre.x, geo.centre.y, geo.arcRadius, geo.arcRadius, 0.0f, fromAngle, toAngle, true);
    return path;
}

void drawTrack(juce::Graphics& g, const Geo& geo, const Theme& theme, float width) {
    g.setColour(theme.colors.border);
    g.strokePath(arcPath(geo, geo.startAngle, geo.endAngle), roundedStroke(width));
}

// The theme's glow halo under a value arc (nothing when the theme has no glow).
void drawArcGlow(juce::Graphics& g, const Theme& theme, const juce::Path& value, juce::Colour colour, float width) {
    if (theme.treatment.glow <= 0.0f)
        return;
    g.setColour(colour.withAlpha(theme.treatment.glow * 0.5f));
    g.strokePath(value, roundedStroke(width * 2.5f));
}

void drawSolidValueArc(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour colour, float width) {
    const auto value = arcPath(geo, geo.startAngle, geo.valueAngle);
    drawArcGlow(g, theme, value, colour, width);
    g.setColour(colour);
    g.strokePath(value, roundedStroke(width));
}

void drawPointer(juce::Graphics& g, const Geo& geo, juce::Colour colour, float fromRadius, float toRadius,
                 float thickness) {
    g.setColour(colour);
    g.drawLine(juce::Line<float>(geo.pointAt(fromRadius, geo.valueAngle), geo.pointAt(toRadius, geo.valueAngle)),
               thickness);
}

void drawClassicBody(juce::Graphics& g, const Geo& geo, const Theme& theme) {
    const auto& c = theme.colors;
    const auto bodyBounds = geo.discBounds(geo.bodyRadius);
    juce::Point<float> focal(bodyBounds.getX() + bodyBounds.getWidth() * 0.38f,
                             bodyBounds.getY() + bodyBounds.getHeight() * 0.32f);
    juce::ColourGradient grad(c.surfaceHi, focal, c.knobBody, bodyBounds.getBottomRight(), true);
    g.setGradientFill(grad);
    g.fillEllipse(bodyBounds);
    g.setColour(c.border);
    g.drawEllipse(bodyBounds, theme.metrics.borderWidth);
}

void paintClassic(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawTrack(g, geo, theme, geo.trackWidth);
    drawSolidValueArc(g, geo, theme, valueColour, geo.trackWidth);
    drawClassicBody(g, geo, theme);
    drawPointer(g, geo, theme.colors.knobPointer, 0.0f, geo.bodyRadius * 0.92f, 2.0f);

    const auto& tr = theme.treatment;
    if (tr.style == ThemeStyle::Textured && tr.texture > 0.0f) {
        g.setColour(theme.colors.border.withAlpha(tr.texture * 0.25f));
        for (int i = 1; i <= 3; ++i)
            g.drawEllipse(geo.discBounds(geo.bodyRadius * (0.4f + 0.18f * (float)i)), 0.6f);
    }
}

void drawPolishedTicks(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    constexpr int kTickCount = 11;
    const float tickRadius = geo.bodyRadius + (geo.arcRadius - geo.bodyRadius) * 0.43f;
    const float dotRadius = 0.75f * geo.scale;
    for (int i = 0; i < kTickCount; ++i) {
        const float fraction = (float)i / (float)(kTickCount - 1);
        const bool lit = fraction <= geo.pos + 1.0e-4f;
        g.setColour(lit ? valueColour.withAlpha(0.75f) : theme.colors.border);
        const auto p = geo.pointAt(tickRadius, geo.startAngle + fraction * (geo.endAngle - geo.startAngle));
        g.fillEllipse(juce::Rectangle<float>(dotRadius * 2.0f, dotRadius * 2.0f).withCentre(p));
    }
}

void drawPolishedValueArc(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto value = arcPath(geo, geo.startAngle, geo.valueAngle);
    drawArcGlow(g, theme, value, valueColour, geo.trackWidth);
    const auto startPoint = geo.pointAt(geo.arcRadius, geo.startAngle);
    const auto endPoint = geo.pointAt(geo.arcRadius, geo.valueAngle);
    if (startPoint.getDistanceFrom(endPoint) < 1.0f) {
        g.setColour(valueColour);
    } else {
        g.setGradientFill(juce::ColourGradient(valueColour.withAlpha(0.6f), startPoint, valueColour, endPoint, false));
    }
    g.strokePath(value, roundedStroke(geo.trackWidth));
}

void paintPolished(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawPolishedTicks(g, geo, theme, valueColour);
    drawTrack(g, geo, theme, geo.trackWidth);
    drawPolishedValueArc(g, geo, theme, valueColour);

    // Classic body plus an inner shadow that is clear until 72% of the radius, then black to the edge.
    const auto& c = theme.colors;
    const auto bodyBounds = geo.discBounds(geo.bodyRadius);
    juce::Point<float> focal(bodyBounds.getX() + bodyBounds.getWidth() * 0.38f,
                             bodyBounds.getY() + bodyBounds.getHeight() * 0.32f);
    g.setGradientFill(juce::ColourGradient(c.surfaceHi, focal, c.knobBody, bodyBounds.getBottomRight(), true));
    g.fillEllipse(bodyBounds);
    juce::ColourGradient shadow(juce::Colours::transparentBlack, geo.centre, juce::Colours::black.withAlpha(0.5f),
                                geo.pointAt(geo.bodyRadius, 0.0f), true);
    shadow.addColour(0.72, juce::Colours::transparentBlack);
    g.setGradientFill(shadow);
    g.fillEllipse(bodyBounds);
    g.setColour(c.border);
    g.drawEllipse(bodyBounds, theme.metrics.borderWidth);

    drawPointer(g, geo, c.knobPointer, 0.0f, geo.bodyRadius * 0.92f, 2.0f);
}

void paintHardware(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto& c = theme.colors;
    drawTrack(g, geo, theme, geo.trackWidth);
    drawSolidValueArc(g, geo, theme, valueColour, geo.trackWidth);

    // Skirt with 24 knurl ticks.
    const auto skirt = geo.discBounds(15.5f * geo.scale);
    g.setColour(c.knobSkirt);
    g.fillEllipse(skirt);
    g.setColour(c.border);
    g.drawEllipse(skirt, 1.0f);
    g.setColour(c.surface);
    constexpr int kKnurlCount = 24;
    for (int i = 0; i < kKnurlCount; ++i) {
        const float angle = juce::MathConstants<float>::twoPi * (float)i / (float)kKnurlCount;
        g.drawLine(juce::Line<float>(geo.pointAt(12.6f * geo.scale, angle), geo.pointAt(15.0f * geo.scale, angle)),
                   1.0f);
    }

    // Cap: vertical gradient, light on the top half, with a soft highlight.
    const auto cap = geo.discBounds(geo.bodyRadius);
    juce::ColourGradient capGradient(c.surfaceHi, geo.centre.x, cap.getY(), c.knobBody, geo.centre.x, cap.getBottom(),
                                     false);
    capGradient.addColour(0.55, c.surfaceHi);
    g.setGradientFill(capGradient);
    g.fillEllipse(cap);
    g.setColour(c.border);
    g.drawEllipse(cap, 1.0f);
    const float rx = 6.5f * geo.scale;
    const float ry = 2.2f * geo.scale;
    g.setColour(c.knobCapHighlight);
    g.fillEllipse(
        juce::Rectangle<float>(rx * 2.0f, ry * 2.0f).withCentre({geo.centre.x, geo.centre.y - 6.0f * geo.scale}));

    // Notch.
    juce::Path notch;
    notch.startNewSubPath(geo.pointAt(4.5f * geo.scale, geo.valueAngle));
    notch.lineTo(geo.pointAt(10.5f * geo.scale, geo.valueAngle));
    g.setColour(c.knobPointer);
    g.strokePath(notch, roundedStroke(2.5f));
}

void drawBloomDot(juce::Graphics& g, juce::Point<float> p, float radius, juce::Colour colour) {
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(p));
}

void paintNeon(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto& c = theme.colors;
    const float glow = neonGlowStrength(theme, geo.pos);

    const auto body = geo.discBounds(geo.bodyRadius);
    g.setColour(c.knobBody);
    g.fillEllipse(body);
    g.setColour(c.border);
    g.drawEllipse(body, 1.0f);

    // A soft bloom under the arc: a few wider strokes at falling alpha stand in for a blur.
    const auto value = arcPath(geo, geo.startAngle, geo.valueAngle);
    g.setColour(valueColour.withAlpha(0.55f * glow * 0.18f));
    g.strokePath(value, roundedStroke(5.0f * 2.6f));
    g.setColour(valueColour.withAlpha(0.55f * glow * 0.32f));
    g.strokePath(value, roundedStroke(5.0f * 1.9f));
    g.setColour(valueColour.withAlpha(0.55f * glow * 0.55f));
    g.strokePath(value, roundedStroke(5.0f * 1.3f));
    drawTrack(g, geo, theme, geo.trackWidth);
    g.setColour(valueColour);
    g.strokePath(value, roundedStroke(geo.trackWidth));

    drawPointer(g, geo, valueColour, 0.0f, geo.bodyRadius * 0.92f, 2.0f);
    const auto tip = geo.pointAt(geo.bodyRadius * 0.92f, geo.valueAngle);
    drawBloomDot(g, tip, (2.4f + 1.2f * geo.pos) * geo.scale, valueColour.withAlpha(juce::jmin(1.0f, 0.6f * glow)));
    drawBloomDot(g, tip, 1.8f * geo.scale, valueColour);
}

void paintRing(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto& c = theme.colors;
    drawBloomDot(g, geo.centre, 1.5f * geo.scale, c.textMuted);
    const auto rider = geo.pointAt(geo.arcRadius, geo.valueAngle);
    g.setColour(c.border);
    g.drawLine(juce::Line<float>(geo.centre, rider), 1.0f);
    drawTrack(g, geo, theme, geo.trackWidth);
    drawSolidValueArc(g, geo, theme, valueColour, geo.trackWidth);
    drawBloomDot(g, rider, 3.0f * geo.scale, c.knobPointer);
}

void paintSoft(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto& c = theme.colors;
    const float width = geo.trackWidth * 1.25f;
    drawTrack(g, geo, theme, width);
    drawSolidValueArc(g, geo, theme, valueColour, width);

    const auto body = geo.discBounds(geo.bodyRadius);
    g.setColour(c.surfaceHi);
    g.fillEllipse(body);
    g.setColour(valueColour.withAlpha(0.22f));
    g.fillEllipse(body);
    g.setColour(valueColour.withAlpha(0.6f));
    g.drawEllipse(body, 1.0f);
    drawBloomDot(g, geo.pointAt(geo.bodyRadius * 0.62f, geo.valueAngle), 2.2f * geo.scale, c.knobPointer);
}

} // namespace

void paintKnob(juce::Graphics& g, const Theme& theme, KnobStyle style, juce::Rectangle<float> knobBounds, float pos01,
               juce::Colour valueColour) {
    const auto geo = makeGeo(theme, knobBounds, pos01);
    switch (style) {
    case KnobStyle::Classic:
        paintClassic(g, geo, theme, valueColour);
        break;
    case KnobStyle::Polished:
        paintPolished(g, geo, theme, valueColour);
        break;
    case KnobStyle::Hardware:
        paintHardware(g, geo, theme, valueColour);
        break;
    case KnobStyle::Neon:
        paintNeon(g, geo, theme, valueColour);
        break;
    case KnobStyle::Ring:
        paintRing(g, geo, theme, valueColour);
        break;
    case KnobStyle::Soft:
        paintSoft(g, geo, theme, valueColour);
        break;
    }
}

} // namespace synth::theme
