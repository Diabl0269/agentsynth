// MorphMapPaint.cpp -- how the morph map looks: the plot, one coloured dot per shape, the dark cursor, the
// optional names and the lit edge a cable is about to land on.
#include "MorphMap.h"
#include "MorphMapPalette.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Layout/ControlMotion.h"
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
        const float scale = getDotScale(i);
        const float alpha = getDotAlpha(i);
        const float r = kDotRadius * scale;
        if (i == selected_) {
            g.setColour(colors.textPrimary.withAlpha(0.9f * alpha));
            g.drawEllipse(c.x - (kDotRadius + 3.0f) * scale, c.y - (kDotRadius + 3.0f) * scale,
                          (kDotRadius + 3.0f) * 2.0f * scale, (kDotRadius + 3.0f) * 2.0f * scale, 1.5f);
        }
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        if (showLabels_ && dots_[(size_t)i].name.isNotEmpty()) {
            g.setColour(colors.textMuted.withMultipliedAlpha(alpha * juce::jlimit(0.0f, 1.0f, scale)));
            const bool leftSide = c.x > getWidth() * 0.65f;
            const auto textArea = juce::Rectangle<float>(72.0f, 14.0f).withCentre({c.x, c.y - 14.0f});
            g.drawText(dots_[(size_t)i].name, textArea.withX(leftSide ? c.x - 76.0f : c.x + 4.0f).toNearestInt(),
                       leftSide ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
        }
    }

    for (const auto& m : motions_) {
        if (!m.leaving)
            continue;
        const auto c = toPixels(m.dot.x, m.dot.y);
        const float scale = motionReduced_ ? 1.0f : control_motion::shrinkScale(m.progress);
        const float alpha = motionReduced_ ? 1.0f - easeInCubic(m.progress) : 1.0f;
        const float r = kDotRadius * scale;
        g.setColour(morphShapeColour(colors, m.colourIndex).withMultipliedAlpha(alpha));
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    if (cursorVisible_) {
        const auto c = getCursorCentre();
        g.setColour(colors.bg0);
        g.fillEllipse(c.x - kCursorRadius, c.y - kCursorRadius, kCursorRadius * 2.0f, kCursorRadius * 2.0f);
        g.setColour(colors.textPrimary);
        g.drawEllipse(c.x - kCursorRadius, c.y - kCursorRadius, kCursorRadius * 2.0f, kCursorRadius * 2.0f, 2.0f);
    }

    if (cursorVisible_)
        paintStrips(g);
}

/** Each routing's modulation beside its slider, painted by the helper a card fader's bar uses: the reachable range
    faint underneath (it answers how far the slider could move, so it shows at rest too) and the live bar from the
    slider's value to value plus CV on top. */
void MorphMap::paintStripModulation(juce::Graphics& g, Axis axis) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    for (const auto& m : stripModulation_[axis == Axis::X ? 0 : 1]) {
        const auto bandColour = m.bandNegative ? colors.modRingNegative : colors.modRingPositive;
        paintCardFaderModBar(g, getModBarBetween(axis, m.bandStart, m.bandEnd), bandColour.withAlpha(0.35f), false);
        paintCardFaderModBar(g, getModBarBetween(axis, m.baseNorm, m.modNorm),
                             m.positive ? colors.modRingPositive : colors.modRingNegative, m.hovered);
    }
}

/** Thin sliders: a track, the accent fill from the start to the value, a thumb at the cursor, and an outline in the
    wire colour while a cable is about to land on it. */
void MorphMap::paintStrips(juce::Graphics& g) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    for (const Axis axis : {Axis::X, Axis::Y}) {
        const auto strip = getStripBounds(axis);
        const float value = axis == Axis::X ? cursor_.x : cursor_.y;
        g.setColour(colors.border.withAlpha(0.8f));
        g.fillRoundedRectangle(strip, 3.0f);
        g.setColour(colors.accent);
        if (axis == Axis::X)
            g.fillRoundedRectangle(strip.withWidth(juce::jmax(6.0f, strip.getWidth() * value)), 3.0f);
        else
            g.fillRoundedRectangle(strip.withHeight(juce::jmax(6.0f, strip.getHeight() * value)), 3.0f);
        const auto thumb = axis == Axis::X
                               ? juce::Point<float>(strip.getX() + strip.getWidth() * value, strip.getCentreY())
                               : juce::Point<float>(strip.getCentreX(), strip.getY() + strip.getHeight() * value);
        g.setColour(colors.textPrimary);
        g.fillEllipse(thumb.x - 5.0f, thumb.y - 5.0f, 10.0f, 10.0f);
        if (dropAxis_ == axis) {
            g.setColour(colors.modWire);
            g.drawRoundedRectangle(strip.expanded(2.0f), 4.0f, 2.0f);
        }
        paintStripModulation(g, axis);
    }
}

void MorphMap::paintOverChildren(juce::Graphics& g) {
    if (hasKeyboardFocus(false))
        paintFocusRegionOutline(*this, g);
}

} // namespace synth::ui
