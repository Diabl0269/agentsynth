// LaneGlyphs.cpp
//
// Concern: the drawn glyphs for the timeline toolbar's automation controls and the button that
// paints them (docs/timeline/edit-tools.md#the-curve-selector).

#include "LaneGlyphs.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

// Maps a unit-square point (0..1, y down) into `area`.
juce::Point<float> at(juce::Rectangle<float> area, float u, float v) {
    return {area.getX() + u * area.getWidth(), area.getY() + v * area.getHeight()};
}

// Each waveform glyph is ONE period and a half so the shape reads as periodic at 14 px, and every
// polyline starts at the left edge and ends at the right, so the set looks like one family.
juce::Path polyline(juce::Rectangle<float> area, std::initializer_list<std::pair<float, float>> points) {
    juce::Path path;
    bool first = true;
    for (const auto& [u, v] : points) {
        const auto p = at(area, u, v);
        if (first)
            path.startNewSubPath(p);
        else
            path.lineTo(p);
        first = false;
    }
    return path;
}

juce::Path sineGlyph(juce::Rectangle<float> area) {
    juce::Path path;
    constexpr int kSteps = 24;
    for (int i = 0; i <= kSteps; ++i) {
        const float u = (float)i / (float)kSteps;
        const float v = 0.5f - 0.4f * std::sin(u * juce::MathConstants<float>::twoPi * 1.5f);
        const auto p = at(area, u, v);
        if (i == 0)
            path.startNewSubPath(p);
        else
            path.lineTo(p);
    }
    return path;
}

juce::Path freehandGlyph(juce::Rectangle<float> area) {
    juce::Path path;
    path.startNewSubPath(at(area, 0.0f, 0.7f));
    path.cubicTo(at(area, 0.2f, 0.1f), at(area, 0.35f, 0.95f), at(area, 0.55f, 0.45f));
    path.cubicTo(at(area, 0.7f, 0.1f), at(area, 0.85f, 0.6f), at(area, 1.0f, 0.25f));
    return path;
}

} // namespace

juce::Path laneCurveGlyph(LaneCurve curve, juce::Rectangle<float> area) {
    switch (curve) {
    case LaneCurve::Freehand:
        return freehandGlyph(area);
    case LaneCurve::Line:
        return polyline(area, {{0.0f, 0.85f}, {1.0f, 0.15f}});
    case LaneCurve::Sine:
        return sineGlyph(area);
    case LaneCurve::Triangle:
        return polyline(area, {{0.0f, 0.85f}, {0.33f, 0.15f}, {0.67f, 0.85f}, {1.0f, 0.15f}});
    case LaneCurve::Square:
        return polyline(area, {{0.0f, 0.85f},
                               {0.0f, 0.15f},
                               {0.33f, 0.15f},
                               {0.33f, 0.85f},
                               {0.67f, 0.85f},
                               {0.67f, 0.15f},
                               {1.0f, 0.15f}});
    case LaneCurve::SawUp:
        return polyline(area, {{0.0f, 0.85f}, {0.5f, 0.15f}, {0.5f, 0.85f}, {1.0f, 0.15f}});
    case LaneCurve::SawDown:
        return polyline(area, {{0.0f, 0.15f}, {0.5f, 0.85f}, {0.5f, 0.15f}, {1.0f, 0.85f}});
    case LaneCurve::Random:
        return polyline(area, {{0.0f, 0.6f},
                               {0.2f, 0.6f},
                               {0.2f, 0.2f},
                               {0.4f, 0.2f},
                               {0.4f, 0.8f},
                               {0.6f, 0.8f},
                               {0.6f, 0.4f},
                               {0.8f, 0.4f},
                               {0.8f, 0.15f},
                               {1.0f, 0.15f}});
    }
    return freehandGlyph(area);
}

juce::Path globalAutomationGlyph(juce::Rectangle<float> area) {
    // A dock bar along the bottom with a lane curve above it: "the bottom automation strip".
    auto path = polyline(area, {{0.0f, 0.55f}, {0.3f, 0.2f}, {0.6f, 0.5f}, {1.0f, 0.15f}});
    path.addRectangle(juce::Rectangle<float>::leftTopRightBottom(area.getX(), area.getY() + area.getHeight() * 0.78f,
                                                                 area.getRight(), area.getBottom()));
    return path;
}

juce::Path followsClipsGlyph(juce::Rectangle<float> area) {
    // A clip block on top, the curve it carries underneath, and a small arrow: they move together.
    juce::Path path;
    path.addRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(area.getX(), area.getY(),
                                                                        area.getX() + area.getWidth() * 0.62f,
                                                                        area.getY() + area.getHeight() * 0.38f),
                             1.5f);
    path.addPath(polyline(area, {{0.0f, 0.95f}, {0.25f, 0.6f}, {0.45f, 0.85f}, {0.62f, 0.62f}}));
    path.addPath(polyline(area, {{0.72f, 0.5f}, {1.0f, 0.5f}}));
    path.addPath(polyline(area, {{0.88f, 0.36f}, {1.0f, 0.5f}, {0.88f, 0.64f}}));
    return path;
}

//==============================================================================
LaneGlyphButton::LaneGlyphButton(const juce::String& name, GlyphFn glyph)
    : juce::Button(name)
    , glyph_(std::move(glyph)) {}

void LaneGlyphButton::setGlyph(GlyphFn glyph) {
    glyph_ = std::move(glyph);
    repaint();
}

void LaneGlyphButton::setBadgeText(const juce::String& text) {
    if (text == badge_)
        return;
    badge_ = text;
    repaint();
}

// Same visual grammar as the edit-tool strip's DrawableButtons: a toolActive fill when on, a faint
// hover fill otherwise, and the glyph in textPrimary (textMuted while disabled). Stroked, never
// filled, so every glyph keeps one line weight whatever its shape.
void LaneGlyphButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    juce::Colour active = juce::Colours::cyan, text = juce::Colours::white, muted = juce::Colours::grey,
                 surface = juce::Colours::darkgrey, onText = juce::Colours::black;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        active = c.toolActive;
        text = c.textPrimary;
        muted = c.textMuted;
        surface = c.surfaceHi;
        onText = c.bg0;
    }

    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const bool on = getToggleState();
    if (on)
        g.setColour(active);
    else
        g.setColour(surface.withAlpha(highlighted || down ? 0.9f : 0.0f));
    g.fillRoundedRectangle(bounds, 3.0f);

    const auto ink = !isEnabled() ? muted : (on ? onText : text);
    auto content = bounds.reduced(4.0f, 5.0f);
    if (badge_.isNotEmpty()) {
        const auto badgeArea = content.removeFromRight(juce::jmin(content.getWidth() * 0.5f, 14.0f));
        g.setColour(ink);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(badge_, badgeArea, juce::Justification::centred, false);
        content.removeFromRight(1.0f);
    }
    if (glyph_) {
        const float side = juce::jmin(content.getWidth(), content.getHeight() * 1.4f);
        g.setColour(ink);
        g.strokePath(glyph_(content.withSizeKeepingCentre(side, content.getHeight())),
                     juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

} // namespace synth::ui
