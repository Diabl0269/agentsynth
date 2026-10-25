// DrumMachineComponent: the marks on a cell that say what its hit does beyond its loudness (chance, repeat, flam,
// condition, and where a swung or nudged hit really plays). docs/timeline/drum-machine.md#grid-marks.

#include "DrumMachineComponent.h"

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
} // namespace

DrumCellMarks DrumMachineComponent::getCellMarks(int row, int step) const {
    const int pitch = getLanePitch(row);
    const int source = ghostSourceStep(row, step);
    const auto it = pitch < 0 ? cells_.end() : cells_.find(drumgrid::cellKey(pitch, source >= 0 ? source : step));
    return it == cells_.end() ? DrumCellMarks{} : describeCellMarks(it->second, getRowSwing(pitch), step);
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
