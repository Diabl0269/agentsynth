// MorphMapPaint.cpp -- how the morph map looks: the plot, one coloured dot per shape, the dark cursor, the
// optional names and the lit edge a cable is about to land on.
#include "MorphMap.h"
#include "MorphMapPalette.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kDotRadius = 6.0f;
constexpr float kCursorRadius = 6.5f;
constexpr float kCornerRadius = 6.0f;
} // namespace

void MorphMap::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colors.bg1);
    g.fillRoundedRectangle(bounds, kCornerRadius);
    g.setColour(colors.border);
    g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);

    const auto plot = getPlotArea();
    g.setColour(colors.border.withAlpha(0.45f));
    for (int i = 1; i < 4; ++i) {
        const float t = (float)i / 4.0f;
        g.drawVerticalLine((int)(plot.getX() + t * plot.getWidth()), plot.getY(), plot.getBottom());
        g.drawHorizontalLine((int)(plot.getY() + t * plot.getHeight()), plot.getX(), plot.getRight());
    }

    g.setFont(juce::Font(10.0f));
    for (int i = 0; i < (int)dots_.size(); ++i) {
        const auto c = getDotCentre(i);
        const auto colour = morphShapeColour(colors, i);
        if (i == selected_) {
            g.setColour(colors.textPrimary.withAlpha(0.9f));
            g.drawEllipse(c.x - kDotRadius - 3.0f, c.y - kDotRadius - 3.0f, (kDotRadius + 3.0f) * 2.0f,
                          (kDotRadius + 3.0f) * 2.0f, 1.5f);
        }
        g.setColour(colour);
        g.fillEllipse(c.x - kDotRadius, c.y - kDotRadius, kDotRadius * 2.0f, kDotRadius * 2.0f);
        if (showLabels_ && dots_[(size_t)i].name.isNotEmpty()) {
            g.setColour(colors.textMuted);
            const bool leftSide = c.x > getWidth() * 0.65f;
            const auto textArea = juce::Rectangle<float>(72.0f, 14.0f).withCentre({c.x, c.y - 14.0f});
            g.drawText(dots_[(size_t)i].name, textArea.withX(leftSide ? c.x - 76.0f : c.x + 4.0f).toNearestInt(),
                       leftSide ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
        }
    }

    if (cursorVisible_) {
        const auto c = getCursorCentre();
        g.setColour(colors.bg0);
        g.fillEllipse(c.x - kCursorRadius, c.y - kCursorRadius, kCursorRadius * 2.0f, kCursorRadius * 2.0f);
        g.setColour(colors.textPrimary);
        g.drawEllipse(c.x - kCursorRadius, c.y - kCursorRadius, kCursorRadius * 2.0f, kCursorRadius * 2.0f, 2.0f);
    }

    if (dropEdge_ != Edge::None) {
        g.setColour(colors.modWire);
        const auto b = getLocalBounds().toFloat();
        constexpr float kLit = 3.0f;
        switch (dropEdge_) {
        case Edge::Bottom:
            g.fillRoundedRectangle(b.getX() + kPad, b.getBottom() - kLit - 1.0f, b.getWidth() - 2.0f * kPad, kLit,
                                   1.5f);
            break;
        case Edge::Left:
            g.fillRoundedRectangle(b.getX() + 1.0f, b.getY() + kPad, kLit, b.getHeight() - 2.0f * kPad, 1.5f);
            break;
        case Edge::Right:
            g.fillRoundedRectangle(b.getRight() - kLit - 1.0f, b.getY() + kPad, kLit, b.getHeight() - 2.0f * kPad,
                                   1.5f);
            break;
        case Edge::None:
            break;
        }
    }
}

void MorphMap::paintOverChildren(juce::Graphics& g) {
    if (hasKeyboardFocus(false))
        paintFocusRegionOutline(*this, g);
}

} // namespace synth::ui
