#include "DrawShapeIcons.h"

#include <cmath>

namespace synth::ui {

// Concern: the Draw shapes' stroke-path icons and the one routine that paints them.

namespace {

constexpr float kBox = 24.0f;
constexpr float kStroke = 1.8f;

void polyline(juce::Path& p, std::initializer_list<juce::Point<float>> pts) {
    bool first = true;
    for (auto pt : pts) {
        if (first)
            p.startNewSubPath(pt);
        else
            p.lineTo(pt);
        first = false;
    }
}

} // namespace

juce::Path drawShapeIconPath(DrawShape shape) {
    juce::Path p;
    switch (shape) {
    case DrawShape::Free:
        // A pen (body, then the tip) drawing a wobbly line under it.
        polyline(p, {{15.0f, 3.0f}, {21.0f, 9.0f}, {12.0f, 18.0f}, {6.0f, 19.0f}, {7.0f, 13.0f}});
        p.closeSubPath();
        p.startNewSubPath(3.0f, 21.0f);
        p.cubicTo(5.0f, 18.0f, 7.0f, 23.0f, 9.0f, 20.0f);
        p.cubicTo(11.0f, 17.0f, 12.0f, 21.0f, 13.0f, 20.0f);
        break;
    case DrawShape::Line:
        polyline(p, {{4.0f, 19.0f}, {20.0f, 5.0f}});
        break;
    case DrawShape::Sine:
        for (int i = 0; i <= 36; ++i) {
            const float x = 3.0f + 18.0f * (float)i / 36.0f;
            const float y = 12.0f - 6.0f * std::sin(6.2831853f * (float)i / 36.0f);
            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
        break;
    case DrawShape::Triangle:
        polyline(p, {{3.0f, 17.0f}, {7.5f, 7.0f}, {12.0f, 17.0f}, {16.5f, 7.0f}, {21.0f, 17.0f}});
        break;
    case DrawShape::Saw:
        polyline(p, {{3.0f, 17.0f}, {11.0f, 7.0f}, {11.0f, 17.0f}, {19.0f, 7.0f}, {19.0f, 17.0f}});
        break;
    case DrawShape::Square:
        polyline(
            p,
            {{3.0f, 17.0f}, {3.0f, 7.0f}, {9.0f, 7.0f}, {9.0f, 17.0f}, {15.0f, 17.0f}, {15.0f, 7.0f}, {21.0f, 7.0f}});
        break;
    }
    return p;
}

void paintDrawShapeIcon(juce::Graphics& g, DrawShape shape, juce::Rectangle<float> area, juce::Colour colour,
                        float alpha) {
    if (alpha <= 0.0f || area.isEmpty())
        return;
    const float scale = juce::jmin(area.getWidth(), area.getHeight()) / kBox;
    auto path = drawShapeIconPath(shape);
    path.applyTransform(juce::AffineTransform::scale(scale).translated(area.getCentreX() - kBox * scale * 0.5f,
                                                                       area.getCentreY() - kBox * scale * 0.5f));
    g.setColour(colour.withMultipliedAlpha(alpha));
    g.strokePath(path,
                 juce::PathStrokeType(kStroke * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace synth::ui
