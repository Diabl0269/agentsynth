#include "IconGlyphs.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
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

// The lane header markers are drawn on a 14 x 14 grid and scaled to the area's shorter side.
void paintLaneMarker(juce::Graphics& g, juce::Rectangle<float> area, bool mod) {
    const auto square = centredSquare(area);
    const float scale = square.getWidth() / 14.0f;
    const auto at = [&](float x, float y) {
        return juce::Point<float>(square.getX() + x * scale, square.getY() + y * scale);
    };
    const juce::PathStrokeType stroke(1.8f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path path;
    if (mod) {
        path.startNewSubPath(at(1, 7));
        path.cubicTo(at(3, 1), at(5, 1), at(7, 7));
        path.cubicTo(at(9, 13), at(11, 13), at(13, 7));
        g.strokePath(path, stroke);
        return;
    }
    path.startNewSubPath(at(1, 11));
    path.lineTo(at(5, 4));
    path.lineTo(at(9, 8));
    path.lineTo(at(13, 2));
    g.strokePath(path, stroke);
    for (const auto dot : {at(5, 4), at(9, 8)})
        g.fillEllipse(juce::Rectangle<float>(3.2f * scale, 3.2f * scale).withCentre(dot));
}

// Two opposite arrows on a 16 x 16 grid drawn at 14 px (the size of the other fixed-size glyphs), so the swap
// icon matches the decided design at any button size.
void paintSwap(juce::Graphics& g, juce::Rectangle<float> area) {
    const float side = std::min(14.0f, std::min(area.getWidth(), area.getHeight()));
    const auto square = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const float scale = side / 16.0f;
    const auto at = [&](float x, float y) {
        return juce::Point<float>(square.getX() + x * scale, square.getY() + y * scale);
    };
    juce::Path path;
    path.startNewSubPath(at(2, 6));
    path.lineTo(at(11, 6));
    path.startNewSubPath(at(8, 3));
    path.lineTo(at(11, 6));
    path.lineTo(at(8, 9));
    path.startNewSubPath(at(14, 10));
    path.lineTo(at(5, 10));
    path.startNewSubPath(at(8, 13));
    path.lineTo(at(5, 10));
    path.lineTo(at(8, 7));
    g.strokePath(path, juce::PathStrokeType(1.6f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A window with an arrow leaving its top-right corner, on the 24 x 24 grid of the app's "open in window" artwork,
// drawn at 14 px like the swap glyph.
void paintDetach(juce::Graphics& g, juce::Rectangle<float> area) {
    const float side = std::min(14.0f, std::min(area.getWidth(), area.getHeight()));
    const auto square = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const float scale = side / 24.0f;
    const auto at = [&](float x, float y) {
        return juce::Point<float>(square.getX() + x * scale, square.getY() + y * scale);
    };
    const float thickness = 2.2f * scale;
    const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.drawRoundedRectangle(juce::Rectangle<float>(at(3, 7), at(16, 21)), 1.5f * scale, thickness);
    juce::Path arrow;
    arrow.startNewSubPath(at(9, 15));
    arrow.lineTo(at(21, 3));
    arrow.startNewSubPath(at(15, 3));
    arrow.lineTo(at(21, 3));
    arrow.lineTo(at(21, 9));
    g.strokePath(arrow, stroke);
}

// Two linked pill-shaped rings in a row, turned a quarter of a right angle, on a 24 x 24 grid drawn at 14 px like the
// other fixed-size glyphs.
void paintLink(juce::Graphics& g, juce::Rectangle<float> area) {
    const float side = std::min(14.0f, std::min(area.getWidth(), area.getHeight()));
    const auto square = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const float scale = side / 24.0f;
    const auto ring = juce::Rectangle<float>(12.0f * scale, 7.0f * scale);
    juce::Path links;
    links.addRoundedRectangle(ring.withCentre({8.0f * scale, 12.0f * scale}), 3.5f * scale);
    links.addRoundedRectangle(ring.withCentre({16.0f * scale, 12.0f * scale}), 3.5f * scale);
    links.applyTransform(
        juce::AffineTransform::rotation(-juce::MathConstants<float>::pi / 4.0f, 12.0f * scale, 12.0f * scale)
            .translated(square.getX(), square.getY()));
    g.strokePath(links,
                 juce::PathStrokeType(2.2f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
// The social marks are the website's SVG drawings: a path set on a square grid (16 for the GitHub mark, 20 for the
// rest), drawn at up to 18 px like the site. `stroke` is the line width on that grid, 0 for a filled mark.
struct SocialShape {
    const char* path;
    bool filled; // fill the path instead of stroking it
};

void paintSocial(juce::Graphics& g, juce::Rectangle<float> area, float grid, float stroke,
                 std::initializer_list<SocialShape> shapes, std::initializer_list<juce::Point<float>> dots = {}) {
    const float side = std::min(18.0f, std::min(area.getWidth(), area.getHeight()));
    const auto square = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
    const float scale = side / grid;
    const auto toArea = juce::AffineTransform::scale(scale).translated(square.getX(), square.getY());
    for (const auto& shape : shapes) {
        auto path = juce::Drawable::parseSVGPath(shape.path);
        path.applyTransform(toArea);
        if (shape.filled)
            g.fillPath(path);
        else
            g.strokePath(path, juce::PathStrokeType(stroke * scale, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }
    for (const auto dot : dots)
        g.fillEllipse(juce::Rectangle<float>(1.8f * scale, 1.8f * scale)
                          .withCentre({square.getX() + dot.x * scale, square.getY() + dot.y * scale}));
}

void paintSocialGlyph(juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area) {
    switch (glyph) {
    case Glyph::SocialDiscord:
        paintSocial(
            g, area, 20.0f, 1.6f,
            {{"M3.5 4.5a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2h-6.8L5 17.5v-2A2 2 0 0 1 3.5 13.5Z", false}},
            {{7.6f, 9.0f}, {12.4f, 9.0f}});
        break;
    case Glyph::SocialYouTube:
        paintSocial(g, area, 20.0f, 1.6f,
                    {{"M4.5 4.5h11a3 3 0 0 1 3 3v5a3 3 0 0 1-3 3h-11a3 3 0 0 1-3-3v-5a3 3 0 0 1 3-3Z", false},
                     {"M8.3 7.7v5.6l5-2.8-5-2.8Z", true}});
        break;
    case Glyph::SocialGitHub:
        paintSocial(
            g, area, 16.0f, 0.0f,
            {{"M8 0c4.42 0 8 3.58 8 8a8.013 8.013 0 0 1-5.45 7.59c-.4.08-.55-.17-.55-.38 0-.27.01-1.13.01-2.2 "
              "0-.75-.25-1.23-.54-1.48 1.78-.2 3.65-.88 3.65-3.95 0-.88-.31-1.59-.82-2.15.08-.2.36-1.02-.08-2.12 0 "
              "0-.67-.22-2.2.82-.64-.18-1.32-.27-2-.27s-1.36.09-2 .27c-1.53-1.03-2.2-.82-2.2-.82-.44 1.1-.16 1.92-.08 "
              "2.12-.51.56-.82 1.28-.82 2.15 0 3.06 1.86 3.75 3.64 3.95-.23.2-.44.55-.51 "
              "1.07-.46.21-1.61.55-2.33-.66-.15-.24-.6-.83-1.23-.82-.67.01-.27.38.01.53.34.19.73.9.82 1.13.16.45.68 "
              "1.31 2.69.94 0 .67.01 1.3.01 1.49 0 .21-.15.45-.55.38A7.995 7.995 0 0 1 0 8c0-4.42 3.58-8 8-8Z",
              true}});
        break;
    case Glyph::SocialTikTok:
        paintSocial(g, area, 20.0f, 1.6f,
                    {{"M11 2.5v10.8a3.2 3.2 0 1 1-2.4-3.1", false}, {"M11 2.5c.3 2.4 2 4.1 4.4 4.3", false}});
        break;
    case Glyph::SocialInstagram:
        paintSocial(g, area, 20.0f, 1.6f,
                    {{"M7 2.5h6a4.5 4.5 0 0 1 4.5 4.5v6a4.5 4.5 0 0 1-4.5 4.5h-6a4.5 4.5 0 0 1-4.5-4.5v-6a4.5 4.5 0 0 "
                      "1 4.5-4.5Z",
                      false},
                     {"M10 6.4a3.6 3.6 0 1 1 0 7.2a3.6 3.6 0 1 1 0-7.2Z", false}},
                    {{14.6f, 5.4f}});
        break;
    default:
        break;
    }
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
    case Glyph::LaneAutomation:
        paintLaneMarker(g, area, false);
        break;
    case Glyph::LaneMod:
        paintLaneMarker(g, area, true);
        break;
    case Glyph::Swap:
        paintSwap(g, area);
        break;
    case Glyph::Detach:
        paintDetach(g, area);
        break;
    case Glyph::Link:
        paintLink(g, area);
        break;
    case Glyph::SocialDiscord:
    case Glyph::SocialYouTube:
    case Glyph::SocialGitHub:
    case Glyph::SocialTikTok:
    case Glyph::SocialInstagram:
        paintSocialGlyph(g, glyph, area);
        break;
    }
}

} // namespace synth::theme
