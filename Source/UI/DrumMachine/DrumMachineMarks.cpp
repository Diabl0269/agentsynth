// DrumMachineComponent: the marks on a cell that say what its hit does beyond its loudness (chance, repeat, flam,
// condition, and where a swung or nudged hit really plays). docs/timeline/drum-machine.md#grid-marks.

#include "DrumMachineComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

namespace {
constexpr float kMarkAlpha = 0.8f;
constexpr float kTickBelowAccent = 4.0f;
constexpr float kRepeatTickHeight = 3.0f;
constexpr float kFlamTickHeight = 6.0f;
constexpr float kPlayTickInset = 3.0f;
constexpr float kMarkFontHeight = 8.0f;
constexpr float kDotSize = 3.0f;
constexpr float kConditionTextWidth = 26.0f;
constexpr float kStackedMinWidth = 12.0f;
constexpr float kStackedMinFont = 7.0f;
constexpr float kStackedMaxFont = 12.0f;
} // namespace

int DrumMachineComponent::getStackedHitCount(int row, int step) const {
    const int pitch = getLanePitch(row);
    const auto it = pitch < 0 ? cells_.end() : cells_.find(drumgrid::cellKey(pitch, step));
    return it != cells_.end() && it->second.ids.size() > 1 ? static_cast<int>(it->second.ids.size()) : 0;
}

DrumCellMarks DrumMachineComponent::getCellMarks(int row, int step) const {
    const int pitch = getLanePitch(row);
    const int source = ghostSourceStep(row, step);
    const auto it = pitch < 0 ? cells_.end() : cells_.find(drumgrid::cellKey(pitch, source >= 0 ? source : step));
    return it == cells_.end() ? DrumCellMarks{} : describeCellMarks(it->second, getRowSwing(pitch), step);
}

// The count of notes a cell holds when it holds more than one: a small chip in the top right corner (the loudness fill
// grows from the bottom, so the chip never covers it) in the theme's own surface and text colours, scaled with the
// cell and left out when the cell is too small to read it.
void DrumMachineComponent::paintStackedCount(juce::Graphics& g, juce::Rectangle<float> area, int count, float alpha) {
    if (count < 2 || area.getWidth() < kStackedMinWidth)
        return;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const float fontHeight = juce::jlimit(kStackedMinFont, kStackedMaxFont, area.getHeight() * 0.4f);
    const auto text = juce::String(count);
    const float width = fontHeight * (0.7f * static_cast<float>(text.length()) + 0.5f);
    const auto chip =
        juce::Rectangle<float>(area.getRight() - width - 1.5f, area.getY() + 1.5f, width, fontHeight + 2.0f);
    g.setColour(colors.surface.withAlpha(0.9f * alpha));
    g.fillRoundedRectangle(chip, 2.0f);
    g.setColour(colors.textPrimary.withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), fontHeight, juce::Font::bold));
    g.drawText(text, chip, juce::Justification::centred, false);
}

// Draws `marks` inside `area` (the cell, already in the hit's scaled space). Nothing on a dense grid: the cells are too
// small to hold them.
void DrumMachineComponent::paintCellMarks(juce::Graphics& g, juce::Rectangle<float> area, const DrumCellMarks& marks,
                                          float alpha, bool dense) {
    if (dense || !marks.any())
        return;
    const auto ink = juce::Colours::white.withMultipliedAlpha(kMarkAlpha * alpha);
    g.setColour(ink);
    for (int j = 0; j < marks.repeatTicks; ++j)
        g.fillRect(juce::Rectangle<float>(area.getX() + 1.0f +
                                              (area.getWidth() - 3.0f) * static_cast<float>(j) /
                                                  static_cast<float>(marks.repeatTicks),
                                          area.getY() + kTickBelowAccent, 1.5f, kRepeatTickHeight));
    if (marks.flam)
        g.fillRect(juce::Rectangle<float>(area.getX() + 1.5f, area.getCentreY() - kFlamTickHeight * 0.5f, 2.0f,
                                          kFlamTickHeight));
    if (marks.playsOffStart) {
        const float x = juce::jlimit(area.getX(), area.getRight() - 2.0f,
                                     area.getX() + static_cast<float>(marks.playOffset) * area.getWidth() - 1.0f);
        g.setColour(juce::Colours::white.withMultipliedAlpha(0.45f * alpha));
        g.fillRect(juce::Rectangle<float>(x, area.getY() + kPlayTickInset, 2.0f,
                                          std::max(1.0f, area.getHeight() - 2.0f * kPlayTickInset)));
    }
    paintStackedCount(g, area, marks.stacked, alpha);
    g.setColour(ink);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), kMarkFontHeight, juce::Font::plain));
    const auto corner = area.reduced(2.0f, 1.0f).removeFromBottom(kMarkFontHeight + 1.0f);
    if (marks.dimmed)
        g.drawText("%", corner, juce::Justification::bottomLeft, false);
    if (marks.condition.isEmpty())
        return;
    if (area.getWidth() >= kConditionTextWidth)
        g.drawText(marks.condition, corner, juce::Justification::bottomRight, false);
    else
        g.fillEllipse(area.getRight() - kDotSize - 2.0f, area.getBottom() - kDotSize - 2.0f, kDotSize, kDotSize);
}

} // namespace synth::ui
