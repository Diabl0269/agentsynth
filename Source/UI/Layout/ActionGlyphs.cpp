// Concern: the shared painters for actions drawn on several surfaces (see ActionGlyphs.h).
#include "UI/Layout/ActionGlyphs.h"

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
    const auto stem = [&](float x, float top, float bottom) {
        g.fillRect(juce::Rectangle<float>(glyph.getX() + x * unit, glyph.getY() + top * unit, 1.2f * unit,
                                          (bottom - top) * unit));
    };
    g.setColour(ink);
    if (voice == VoiceGlyph::mono) { // one filled head, its stem rising from the right edge
        g.fillPath(head(5.0f, 9.0f, 5.6f, 4.0f));
        stem(6.6f, 1.0f, 9.0f);
        return;
    }
    const juce::PathStrokeType outline(1.0f * unit * strokeScale);
    for (const float cy : {2.8f, 6.0f, 9.2f}) { // a chord: three stacked heads sharing one stem
        const auto path = head(4.6f, cy, 4.6f, 2.8f);
        if (voice == VoiceGlyph::poly)
            g.fillPath(path);
        else
            g.strokePath(path, outline);
    }
    stem(6.2f, 0.8f, 9.2f);
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

// A circle with a mark inside it (info, error) or a triangle with an exclamation mark (warning). The mark is a stroke
// or a dot, so it stays legible down to a 12 px square.
void paintSeverityGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink, SeverityGlyph severity,
                        float strokeScale) {
    const auto glyph = squareIn(area);
    const float unit = glyph.getWidth() / kGridSize;
    const float thickness = kRingThickness * unit * strokeScale;
    const auto at = [&](float x, float y) {
        return juce::Point<float>(glyph.getX() + x * unit, glyph.getY() + y * unit);
    };
    const auto dotAt = [&](float x, float y) {
        const float d = 1.8f * unit;
        g.fillEllipse(at(x, y).x - d * 0.5f, at(x, y).y - d * 0.5f, d, d);
    };
    g.setColour(ink);
    const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    if (severity == SeverityGlyph::warning) {
        juce::Path triangle;
        triangle.addTriangle(at(6.0f, 0.9f), at(11.3f, 10.8f), at(0.7f, 10.8f));
        g.strokePath(triangle, stroke);
        g.drawLine({at(6.0f, 4.1f), at(6.0f, 7.0f)}, thickness);
        dotAt(6.0f, 9.0f);
        return;
    }
    g.drawEllipse(glyph.reduced(thickness * 0.5f), thickness);
    if (severity == SeverityGlyph::info) {
        dotAt(6.0f, 3.5f);
        g.drawLine({at(6.0f, 5.3f), at(6.0f, 9.0f)}, thickness);
        return;
    }
    juce::Path cross;
    cross.startNewSubPath(at(4.0f, 4.0f));
    cross.lineTo(at(8.0f, 8.0f));
    cross.startNewSubPath(at(8.0f, 4.0f));
    cross.lineTo(at(4.0f, 8.0f));
    g.strokePath(cross, stroke);
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
