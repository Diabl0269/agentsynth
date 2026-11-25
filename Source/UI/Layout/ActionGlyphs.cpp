// Concern: the shared painters for actions drawn on several surfaces (see ActionGlyphs.h).
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Theme/IconTileDepth.h"

#include <algorithm>

namespace synth::ui {

namespace {
// The drawings below are laid out on a 12 px square; every length scales with the square actually used.
constexpr float kGridSize = 12.0f;
constexpr float kRingThickness = 1.4f;

juce::Rectangle<float> squareIn(juce::Rectangle<float> area) {
    const float side = std::min(area.getWidth(), area.getHeight());
    return juce::Rectangle<float>(side, side).withCentre(area.getCentre());
}
} // namespace

// A target: a ring with four ticks reaching out of it.
void paintShowOnCanvasGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float thickness = kRingThickness * unit * strokeScale;
    const auto centre = glyph.getCentre();
    const float radius = kGridSize * 0.28f * unit;
    const float tick = kGridSize * 0.5f * unit;
    g.setColour(ink);
    g.drawEllipse(glyph.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f), thickness);
    for (const auto d :
         {juce::Point<float>(1, 0), juce::Point<float>(-1, 0), juce::Point<float>(0, 1), juce::Point<float>(0, -1)})
        g.drawLine({centre + d * (radius + unit), centre + d * tick}, thickness);
}

// Note heads, a little tilted like engraved ones. Mono is one large head; poly is three smaller ones stacked like a
// chord; mixed is the same stack as outlines, so "some of each" reads as "not filled in".
void paintVoiceGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, VoiceGlyph voice,
                     float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const auto head = [&](float cx, float cy, float w, float h) {
        juce::Path path;
        path.addEllipse(-w * 0.5f * unit, -h * 0.5f * unit, w * unit, h * unit);
        path.applyTransform(
            juce::AffineTransform::rotation(-0.35f).translated(glyph.getX() + cx * unit, glyph.getY() + cy * unit));
        return path;
    };
    const auto stem = [&](float x, float top, float bottom, float width = 1.2f) {
        g.fillRect(juce::Rectangle<float>(glyph.getX() + x * unit, glyph.getY() + top * unit, width * unit,
                                          (bottom - top) * unit));
    };
    g.setColour(ink);
    if (voice == VoiceGlyph::mono) { // one filled head, its stem rising from the right edge
        g.fillPath(head(5.0f, 9.0f, 5.6f, 4.0f));
        stem(6.6f, 1.0f, 9.0f);
        return;
    }
    // A chord: three small heads stacked low and tight on one stem that rises well clear of them, so the picture reads
    // as notes (a tall line over a cluster), not as three dots. The stem tops out at the grid's top edge.
    const juce::PathStrokeType outline(0.8f * unit * strokeScale);
    for (const float cy : {5.2f, 7.6f, 10.0f}) {
        const auto path = head(4.4f, cy, 4.2f, 2.0f);
        if (voice == VoiceGlyph::poly)
            g.fillPath(path);
        else
            g.strokePath(path, outline);
    }
    stem(5.7f, 0.0f, 10.0f, 1.3f);
}

// A headband arc over two filled ear cups, which sit on its ends.
void paintHeadphonesGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float thickness = kRingThickness * unit * strokeScale;
    const auto at = [&](float x, float y) {
        return juce::Point<float>(glyph.getX() + x * unit, glyph.getY() + y * unit);
    };
    g.setColour(ink);
    juce::Path band;
    const auto centre = at(6.0f, 6.4f);
    const float radius = 4.3f * unit;
    band.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi,
                       juce::MathConstants<float>::halfPi, true);
    g.strokePath(band, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (const float x : {1.0f, 8.4f})
        g.fillRoundedRectangle({at(x, 6.2f), at(x + 2.6f, 10.8f)}, 1.1f * unit);
}

// A low-pass response: level, a small bump at the corner, then the roll-off, over a faint baseline.
void paintResponseGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float thickness = kRingThickness * unit * strokeScale;
    const auto at = [&](float x, float y) {
        return juce::Point<float>(glyph.getX() + x * unit, glyph.getY() + y * unit);
    };
    g.setColour(ink.withMultipliedAlpha(0.45f));
    g.drawLine({at(0.5f, 10.6f), at(11.5f, 10.6f)}, thickness * 0.7f);
    juce::Path curve;
    curve.startNewSubPath(at(0.8f, 4.6f));
    curve.lineTo(at(5.2f, 4.6f));
    curve.cubicTo(at(7.0f, 4.6f), at(7.4f, 2.4f), at(8.4f, 4.8f));
    curve.cubicTo(at(9.2f, 6.6f), at(9.8f, 9.0f), at(11.2f, 10.2f));
    g.setColour(ink);
    g.strokePath(curve, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Four bars of different heights standing on a baseline.
void paintSpectrumGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float width = 1.9f * unit * std::max(0.8f, strokeScale);
    const float bottom = glyph.getY() + 11.0f * unit;
    g.setColour(ink);
    int index = 0;
    for (const float height : {4.0f, 8.0f, 5.5f, 9.5f}) {
        const float x = glyph.getX() + (1.0f + (float)index * 2.9f) * unit;
        g.fillRoundedRectangle(x, bottom - height * unit, width, height * unit, width * 0.4f);
        ++index;
    }
}

// A window: a rounded frame with a title bar line.
void paintWindowGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float thickness = kRingThickness * unit * strokeScale;
    const auto frame =
        juce::Rectangle<float>(glyph.getX() + 1.0f * unit, glyph.getY() + 1.6f * unit, 10.0f * unit, 8.8f * unit);
    g.setColour(ink);
    g.drawRoundedRectangle(frame, 1.2f * unit, thickness);
    g.drawLine({frame.getX(), frame.getY() + 2.6f * unit, frame.getRight(), frame.getY() + 2.6f * unit}, thickness);
}

// The lit disc: a radial light from the top left (38% to 10% of `ink`), a 1 px rim at
// 45%, and the glyph at 52% of the disc with a small soft shadow. At Flat it is a plain disc (one fill, no rim, no
// shadow); at Rich the shared tile painter adds its drop shadow, ring and top highlight (IconTileDepth.h). The glyphs
// are drawn on a 24-unit grid with rounded joins; the warning triangle is nudged up 1.3 units so it sits optically
// centred in the circle.
void paintSeverityDisc(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph severity,
                       juce::Colour cut, const synth::theme::Theme* theme) {
    const auto disc = squareIn(area);
    const float side = disc.getWidth();
    const bool large = side > 30.0f;
    const bool flat = theme != nullptr && theme->treatment.depth == synth::theme::DepthLevel::Flat;
    if (theme != nullptr)
        synth::theme::paintIconTileUnder(g, disc.reduced(0.5f), side * 0.5f, ink, 0.0f, *theme);
    const auto lit = juce::ColourGradient(ink.withMultipliedAlpha(0.38f), disc.getX() + side * 0.35f,
                                          disc.getY() + side * 0.28f, ink.withMultipliedAlpha(0.10f),
                                          disc.getX() + side * 0.35f + side * 0.8f, disc.getY() + side * 0.28f, true);
    if (flat) {
        g.setColour(ink.withMultipliedAlpha(0.24f));
    } else {
        g.setGradientFill(lit);
    }
    g.fillEllipse(disc.reduced(0.5f));
    if (!flat) {
        g.setColour(ink.withMultipliedAlpha(0.45f));
        g.drawEllipse(disc.reduced(0.5f), 1.0f);
    }
    if (theme != nullptr)
        synth::theme::paintIconTileOver(g, disc.reduced(0.5f), side * 0.5f, ink, 0.0f, 0.0f, *theme);

    paintSeverityShape(g, disc.getCentre(), side * 0.52f, ink, severity, cut, !flat, large);
}

juce::Path severityBodyPath(SeverityGlyph severity, juce::Point<float> centre, float glyphSide) {
    const auto glyph = juce::Rectangle<float>(glyphSide, glyphSide).withCentre(centre);
    const float unit = glyphSide / 24.0f;
    const auto place = juce::AffineTransform::scale(unit).translated(glyph.getX(), glyph.getY());
    juce::Path body;
    if (severity == SeverityGlyph::warning)
        body = juce::Drawable::parseSVGPath(
            "M10.27 3.6a2 2 0 0 1 3.46 0l8.1 14.4A2 2 0 0 1 20.1 21H3.9a2 2 0 0 1-1.73-3Z");
    else
        body.addEllipse(1.8f, 1.8f, 20.4f, 20.4f);
    const auto nudge =
        severity == SeverityGlyph::warning ? juce::AffineTransform::translation(0.0f, -1.3f) : juce::AffineTransform();
    body.applyTransform(nudge.followedBy(place));
    return body;
}

void paintSeverityBody(juce::Graphics& g, juce::Point<float> centre, float glyphSide, juce::Colour ink,
                       SeverityGlyph severity, const juce::DropShadow* shadow) {
    const auto body = severityBodyPath(severity, centre, glyphSide);
    if (shadow != nullptr)
        shadow->drawForPath(g, body);
    g.setColour(ink);
    g.fillPath(body);
    // The warning's corners are rounded by a stroke of its own colour round the path.
    if (severity == SeverityGlyph::warning)
        g.strokePath(body, juce::PathStrokeType(1.6f * glyphSide / 24.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
}

void paintSeverityShape(juce::Graphics& g, juce::Point<float> centre, float glyphSide, juce::Colour ink,
                        SeverityGlyph severity, juce::Colour cut, bool shadow, bool large) {
    const juce::DropShadow soft(juce::Colours::black.withAlpha(0.28f * ink.getFloatAlpha()), large ? 3 : 1,
                                {0, large ? 2 : 1});
    paintSeverityBody(g, centre, glyphSide, ink, severity, shadow ? &soft : nullptr);
    paintSeverityMark(g, centre, glyphSide, severity, cut);
}

void paintSeverityMark(juce::Graphics& g, juce::Point<float> centre, float glyphSide, SeverityGlyph severity,
                       juce::Colour cut) {
    const auto glyph = juce::Rectangle<float>(glyphSide, glyphSide).withCentre(centre);
    const float unit = glyphSide / 24.0f;
    const auto place = juce::AffineTransform::scale(unit).translated(glyph.getX(), glyph.getY());
    const auto at = [&](float x, float y) { return juce::Point<float>(x, y).transformedBy(place); };
    const auto dot = [&](float cx, float cy, float r) {
        g.fillEllipse(at(cx, cy).x - r * unit, at(cx, cy).y - r * unit, 2.0f * r * unit, 2.0f * r * unit);
    };
    const auto bar = [&](float x, float y, float w, float h) {
        g.fillRoundedRectangle({at(x, y), at(x + w, y + h)}, w * 0.5f * unit);
    };

    g.setColour(cut);
    const float up = severity == SeverityGlyph::warning ? -1.3f : 0.0f;
    const juce::PathStrokeType mark(2.2f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (severity) {
    case SeverityGlyph::warning:
        bar(10.9f, 8.6f + up, 2.2f, 6.6f);
        dot(12.0f, 17.7f + up, 1.25f);
        break;
    case SeverityGlyph::info:
        bar(10.9f, 10.6f, 2.2f, 6.8f);
        dot(12.0f, 7.6f, 1.3f);
        break;
    case SeverityGlyph::question: {
        auto hook = juce::Drawable::parseSVGPath("M9.2 9.3a2.8 2.8 0 0 1 5.5.8c0 1.9-2.7 2.3-2.7 4.3");
        hook.applyTransform(place);
        g.strokePath(hook, mark);
        dot(12.0f, 17.6f, 1.25f);
        break;
    }
    case SeverityGlyph::error: {
        juce::Path cross;
        cross.startNewSubPath(at(8.2f, 8.2f));
        cross.lineTo(at(15.8f, 15.8f));
        cross.startNewSubPath(at(15.8f, 8.2f));
        cross.lineTo(at(8.2f, 15.8f));
        g.strokePath(cross, mark);
        break;
    }
    }
}

// The curve of a band type on a 14 x 10 grid (y grows downwards): a flat level, a hump, a step, a dip.
void paintEqBandTypeGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, synth::eq::BandType type,
                          float strokeScale) {
    using synth::eq::BandType;
    constexpr float kW = 14.0f;
    constexpr float kH = 10.0f;
    const float unit = std::min(area.getWidth() / kW, area.getHeight() / kH);
    const auto origin = area.getCentre() - juce::Point<float>(kW, kH) * (0.5f * unit);
    const auto at = [&](float x, float y) { return origin + juce::Point<float>(x, y) * unit; };
    juce::Path path;
    const auto move = [&](float x, float y) { path.startNewSubPath(at(x, y)); };
    const auto line = [&](float x, float y) { path.lineTo(at(x, y)); };
    const auto curve = [&](float x1, float y1, float x2, float y2, float x, float y) {
        path.cubicTo(at(x1, y1), at(x2, y2), at(x, y));
    };
    switch (type) {
    case BandType::Peak:
        move(0.5f, 7.0f);
        curve(4.5f, 7.0f, 4.5f, 1.5f, 7.0f, 1.5f);
        curve(9.5f, 1.5f, 9.5f, 7.0f, 13.5f, 7.0f);
        break;
    case BandType::LowShelf:
        move(0.5f, 2.5f);
        line(3.0f, 2.5f);
        curve(6.0f, 2.5f, 8.0f, 7.5f, 11.0f, 7.5f);
        line(13.5f, 7.5f);
        break;
    case BandType::HighShelf:
        move(0.5f, 7.5f);
        line(3.0f, 7.5f);
        curve(6.0f, 7.5f, 8.0f, 2.5f, 11.0f, 2.5f);
        line(13.5f, 2.5f);
        break;
    case BandType::LowCut:
        move(2.5f, 9.5f);
        curve(3.5f, 5.0f, 4.5f, 3.0f, 7.0f, 3.0f);
        line(13.5f, 3.0f);
        break;
    case BandType::HighCut:
        move(0.5f, 3.0f);
        line(7.0f, 3.0f);
        curve(9.5f, 3.0f, 10.5f, 5.0f, 11.5f, 9.5f);
        break;
    case BandType::Notch:
        move(0.5f, 3.0f);
        line(4.5f, 3.0f);
        curve(6.0f, 3.0f, 6.0f, 9.0f, 7.0f, 9.0f);
        curve(8.0f, 9.0f, 8.0f, 3.0f, 9.5f, 3.0f);
        line(13.5f, 3.0f);
        break;
    case BandType::BandPass:
        move(0.5f, 9.0f);
        curve(4.0f, 9.0f, 5.0f, 1.5f, 7.0f, 1.5f);
        curve(9.0f, 1.5f, 10.0f, 9.0f, 13.5f, 9.0f);
        break;
    case BandType::Tilt:
        move(0.5f, 7.5f);
        line(3.0f, 7.5f);
        line(11.0f, 2.5f);
        line(13.5f, 2.5f);
        break;
    }
    g.setColour(ink);
    g.strokePath(path, juce::PathStrokeType(1.3f * unit * strokeScale, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

} // namespace synth::ui
