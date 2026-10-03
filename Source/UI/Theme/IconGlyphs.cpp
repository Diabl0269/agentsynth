#include "IconGlyphs.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace synth::theme {

namespace {
// The drawable square of the transport glyphs is the shorter side of the area, inset by this
// fraction on every edge: a proportional inset on a SQUARE, never a fraction of the width applied to
// both axes (that flattened every glyph on a short bar).
constexpr float kGlyphInsetRatio = 0.24f;

juce::Rectangle<float> centredSquare(juce::Rectangle<float> area) {
    const float side = std::min(area.getWidth(), area.getHeight());
    return juce::Rectangle<float>(side, side).withCentre(area.getCentre());
}

juce::Rectangle<float> insetSquare(juce::Rectangle<float> area) {
    const auto square = centredSquare(area);
    return square.reduced(square.getWidth() * kGlyphInsetRatio);
}

void paintPlay(juce::Graphics& g, juce::Rectangle<float> area) {
    // Optical centring: a triangle's visual mass sits left of its bounding box, so it is nudged right
    // and kept narrower than it is tall.
    const auto glyphArea = insetSquare(area);
    const auto tri = glyphArea.withTrimmedLeft(glyphArea.getWidth() * 0.12f);
    juce::Path triangle;
    triangle.addTriangle(tri.getX(), tri.getY(), tri.getX(), tri.getBottom(), tri.getRight(), tri.getCentreY());
    g.fillPath(triangle);
}

void paintStop(juce::Graphics& g, juce::Rectangle<float> area) {
    const auto glyphArea = insetSquare(area);
    g.fillRoundedRectangle(glyphArea.reduced(glyphArea.getWidth() * 0.06f), 1.5f);
}

void paintLoop(juce::Graphics& g, juce::Rectangle<float> area) {
    const auto glyphArea = insetSquare(area);
    const float radius = glyphArea.getWidth() * 0.5f;
    const auto centre = glyphArea.getCentre();
    constexpr float kGapStartRadians = juce::MathConstants<float>::pi * 0.15f;
    constexpr float kGapEndRadians = juce::MathConstants<float>::pi * 1.85f;

    juce::Path loopPath;
    loopPath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, kGapStartRadians, kGapEndRadians, true);
    const float strokeWidth = juce::jmax(1.4f, radius * 0.3f);
    g.strokePath(loopPath,
                 juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // A small arrowhead at the arc's start so the bracket reads as a loop, not a plain "C". Sized off
    // the radius so it scales with the button instead of overwhelming a small one.
    const auto tip = centre.translated(radius * std::sin(kGapStartRadians), -radius * std::cos(kGapStartRadians));
    const float arrow = juce::jmax(2.0f, radius * 0.5f);
    juce::Path arrowHead;
    arrowHead.addTriangle(tip.x - arrow, tip.y - arrow * 0.65f, tip.x + arrow, tip.y, tip.x - arrow * 0.35f,
                          tip.y + arrow * 1.15f);
    g.fillPath(arrowHead);
}

void paintReturnToStart(juce::Graphics& g, juce::Rectangle<float> area) {
    // "Skip to start": a bar on the left edge with a left-pointing triangle against it.
    const auto glyphArea = insetSquare(area);
    const float barWidth = juce::jmax(1.5f, glyphArea.getWidth() * 0.16f);
    g.fillRect(glyphArea.getX(), glyphArea.getY(), barWidth, glyphArea.getHeight());
    const auto tri = glyphArea.withTrimmedLeft(barWidth + glyphArea.getWidth() * 0.08f);
    juce::Path triangle;
    triangle.addTriangle(tri.getRight(), tri.getY(), tri.getRight(), tri.getBottom(), tri.getX(), tri.getCentreY());
    g.fillPath(triangle);
}

void paintMetronome(juce::Graphics& g, juce::Rectangle<float> area) {
    // A plain "quarter note" glyph (notehead + stem): asset-free and distinct at a glance from the
    // record circle, proportioned as a group inside the square so it reads as a note rather than a
    // blob hugging one corner.
    const auto glyphArea = insetSquare(area);
    const float headWidth = glyphArea.getWidth() * 0.58f;
    const float headHeight = headWidth * 0.72f;
    const juce::Rectangle<float> head(glyphArea.getX(), glyphArea.getBottom() - headHeight, headWidth, headHeight);
    g.fillEllipse(head);
    const float stemWidth = juce::jmax(1.0f, headWidth * 0.18f);
    g.fillRect(head.getRight() - stemWidth, glyphArea.getY(), stemWidth, glyphArea.getHeight() - headHeight * 0.5f);
}

void paintCross(juce::Graphics& g, juce::Rectangle<float> inner) {
    g.drawLine(inner.getX(), inner.getY(), inner.getRight(), inner.getBottom(), 1.6f);
    g.drawLine(inner.getX(), inner.getBottom(), inner.getRight(), inner.getY(), 1.6f);
}

void paintPin(juce::Graphics& g, juce::Rectangle<float> area, bool pinned) {
    const auto square = centredSquare(area);
    const float headR = square.getWidth() * 0.22f;
    const auto centre = square.getCentre();
    juce::Path p;
    p.addEllipse(centre.x - headR, square.getY() + headR * 0.4f, headR * 2.0f, headR * 2.0f);
    p.addTriangle(centre.x - headR * 0.7f, square.getY() + headR * 1.8f, centre.x + headR * 0.7f,
                  square.getY() + headR * 1.8f, centre.x, square.getBottom());
    if (pinned) {
        p.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.25f, centre.x, centre.y));
        g.fillPath(p);
    } else {
        g.strokePath(p, juce::PathStrokeType(1.4f));
    }
}

// Three dots drawn rather than a text ellipsis, so the glyph never depends on font coverage.
void paintMenuDots(juce::Graphics& g, juce::Rectangle<float> area) {
    constexpr float dot = 2.5f;
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre(area.getCentre().translated((float)i * 4.5f, 0.0f)));
}

// One period of a waveform, stroked across the glyph's wide box (twice as wide as it is tall, centred in `area`). The
// shapes are polylines so a headless build draws them like the app.
juce::Rectangle<float> waveBox(juce::Rectangle<float> area) {
    const float height = std::min(area.getHeight() * 0.7f, area.getWidth() * 0.4f);
    return juce::Rectangle<float>(std::min(area.getWidth() - 2.0f, height * 2.0f), height).withCentre(area.getCentre());
}

void strokePoints(juce::Graphics& g, const std::vector<juce::Point<float>>& points, juce::Rectangle<float> box) {
    juce::Path path;
    for (size_t i = 0; i < points.size(); ++i) {
        const float x = box.getX() + points[i].x * box.getWidth();
        const float y = box.getBottom() - points[i].y * box.getHeight();
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.strokePath(path, juce::PathStrokeType(1.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

void paintShape(juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area) {
    const auto box = waveBox(area);
    switch (glyph) {
    case Glyph::ShapeSine: {
        std::vector<juce::Point<float>> points;
        for (int i = 0; i <= 24; ++i) {
            const float t = (float)i / 24.0f;
            points.push_back({t, 0.5f + 0.5f * std::sin(t * juce::MathConstants<float>::twoPi)});
        }
        strokePoints(g, points, box);
        break;
    }
    case Glyph::ShapeTriangle:
        strokePoints(g, {{0.0f, 0.5f}, {0.25f, 1.0f}, {0.75f, 0.0f}, {1.0f, 0.5f}}, box);
        break;
    case Glyph::ShapeSaw:
        strokePoints(g, {{0.0f, 0.0f}, {0.5f, 1.0f}, {0.5f, 0.0f}, {1.0f, 1.0f}}, box);
        break;
    case Glyph::ShapeSquare:
        strokePoints(g, {{0.0f, 0.0f}, {0.0f, 1.0f}, {0.5f, 1.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}, box);
        break;
    case Glyph::ShapeRandom:
        strokePoints(g,
                     {{0.0f, 0.55f},
                      {0.2f, 0.55f},
                      {0.2f, 1.0f},
                      {0.4f, 1.0f},
                      {0.4f, 0.1f},
                      {0.6f, 0.1f},
                      {0.6f, 0.75f},
                      {0.8f, 0.75f},
                      {0.8f, 0.3f},
                      {1.0f, 0.3f}},
                     box);
        break;
    case Glyph::ShapeCustom: {
        const std::vector<juce::Point<float>> points{{0.0f, 0.3f}, {0.3f, 0.9f}, {0.6f, 0.2f}, {1.0f, 0.7f}};
        strokePoints(g, points, box);
        for (const auto& p : points)
            g.fillEllipse(
                juce::Rectangle<float>(3.0f, 3.0f)
                    .withCentre({box.getX() + p.x * box.getWidth(), box.getBottom() - p.y * box.getHeight()}));
        break;
    }
    default:
        break;
    }
}

void paintEye(juce::Graphics& g, juce::Rectangle<float> bounds, bool hidden) {
    const auto area = juce::Rectangle<float>(14.0f, 9.0f).withCentre(bounds.getCentre());
    juce::Path lid;
    lid.startNewSubPath(area.getX(), area.getCentreY());
    lid.quadraticTo(area.getCentreX(), area.getY() - 4.0f, area.getRight(), area.getCentreY());
    lid.quadraticTo(area.getCentreX(), area.getBottom() + 4.0f, area.getX(), area.getCentreY());
    g.strokePath(lid, juce::PathStrokeType(1.2f));
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(area.getCentre()));
    if (hidden)
        g.drawLine(area.getX() + 1.0f, area.getBottom() + 1.5f, area.getRight() - 1.0f, area.getY() - 1.5f, 1.4f);
}

// A window whose left strip is filled while the pane is open and only divided off while it is closed.
void paintSidePane(juce::Graphics& g, juce::Rectangle<float> area, bool open) {
    const auto glyph = juce::Rectangle<float>(14.0f, 11.0f).withCentre(area.getCentre());
    g.drawRoundedRectangle(glyph, 1.5f, 1.2f);
    const auto strip = glyph.withWidth(5.0f);
    if (open)
        g.fillRect(strip.reduced(0.6f));
    else
        g.fillRect(strip.getRight() - 0.6f, glyph.getY() + 1.0f, 1.2f, glyph.getHeight() - 2.0f);
}
} // namespace

void paintGlyph(juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area, juce::Colour colour) {
    g.setColour(colour);
    switch (glyph) {
    case Glyph::Play:
        paintPlay(g, area);
        break;
    case Glyph::Stop:
        paintStop(g, area);
        break;
    case Glyph::RecordIdle:
        g.drawEllipse(insetSquare(area).reduced(0.75f), 1.5f);
        break;
    case Glyph::RecordOn:
        g.fillEllipse(insetSquare(area));
        break;
    case Glyph::Loop:
        paintLoop(g, area);
        break;
    case Glyph::ReturnToStart:
        paintReturnToStart(g, area);
        break;
    case Glyph::Metronome:
        paintMetronome(g, area);
        break;
    case Glyph::Close:
        paintCross(g, centredSquare(area).reduced(centredSquare(area).getWidth() * 0.22f));
        break;
    case Glyph::Delete:
        paintCross(g, centredSquare(area).reduced(centredSquare(area).getWidth() * 0.3f));
        break;
    case Glyph::Pin:
        paintPin(g, area, false);
        break;
    case Glyph::PinOn:
        paintPin(g, area, true);
        break;
    case Glyph::MenuDots:
        paintMenuDots(g, area);
        break;
    case Glyph::EyeOpen:
        paintEye(g, area, false);
        break;
    case Glyph::EyeHidden:
        paintEye(g, area, true);
        break;
    case Glyph::SidePane:
        paintSidePane(g, area, false);
        break;
    case Glyph::SidePaneOpen:
        paintSidePane(g, area, true);
        break;
    case Glyph::ShapeSine:
    case Glyph::ShapeTriangle:
    case Glyph::ShapeSaw:
    case Glyph::ShapeSquare:
    case Glyph::ShapeRandom:
    case Glyph::ShapeCustom:
        paintShape(g, glyph, area);
        break;
    }
}

} // namespace synth::theme
