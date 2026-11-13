// MorphPlot.cpp -- the plot, grid, ring cursor and pointer mapping the morph map and the XY pad share.
#include "MorphPlot.h"

namespace synth::ui::morphplot {

void paintPlot(juce::Graphics& g, juce::Rectangle<float> plate, juce::Rectangle<float> plot,
               const synth::theme::Colors& colors) {
    g.setColour(colors.bg1);
    g.fillRoundedRectangle(plate, kCornerRadius);
    g.setColour(colors.border);
    g.drawRoundedRectangle(plate, kCornerRadius, 1.0f);

    g.setColour(colors.border.withAlpha(0.45f));
    for (int i = 1; i < 4; ++i) {
        const float t = (float)i / 4.0f;
        g.drawVerticalLine((int)(plot.getX() + t * plot.getWidth()), plot.getY(), plot.getBottom());
        g.drawHorizontalLine((int)(plot.getY() + t * plot.getHeight()), plot.getX(), plot.getRight());
    }
}

void paintCursor(juce::Graphics& g, juce::Point<float> centre, float alpha, const synth::theme::Colors& colors) {
    g.setColour(colors.bg0.withMultipliedAlpha(alpha));
    g.fillEllipse(centre.x - kCursorRadius, centre.y - kCursorRadius, kCursorRadius * 2.0f, kCursorRadius * 2.0f);
    g.setColour(colors.textPrimary.withMultipliedAlpha(alpha));
    g.drawEllipse(centre.x - kCursorRadius, centre.y - kCursorRadius, kCursorRadius * 2.0f, kCursorRadius * 2.0f, 2.0f);
}

juce::Point<float> toUnit(juce::Rectangle<float> plot, juce::Point<float> pixels) {
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return {0.5f, 0.5f};
    return {juce::jlimit(0.0f, 1.0f, (pixels.x - plot.getX()) / plot.getWidth()),
            juce::jlimit(0.0f, 1.0f, (pixels.y - plot.getY()) / plot.getHeight())};
}

juce::Point<float> toPixels(juce::Rectangle<float> plot, juce::Point<float> unit) {
    return {plot.getX() + unit.x * plot.getWidth(), plot.getY() + unit.y * plot.getHeight()};
}

} // namespace synth::ui::morphplot
