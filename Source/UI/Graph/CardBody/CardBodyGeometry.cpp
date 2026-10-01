// CardBodyGeometry.cpp -- the run layouts every card body is built from. The numbers are the generic
// card's, unchanged: the hosted-plugin card lays its widgets out with the same runs.
#include "CardBodyGeometry.h"
#include "UI/Layout/LayoutUtil.h"
#include <algorithm>

namespace synth::cardbody {

// Body content sits below every jack, so it gets nearly the full card width. Single-column widgets
// (combos, the load row) look stretched at full width, so they stay centred in a narrower band.
BodyGeometry BodyGeometry::forCardWidth(int width) {
    BodyGeometry g;
    g.width = width;
    g.contentX = kContentMargin;
    g.contentW = std::max(60, width - kContentMargin * 2);
    g.narrowW = std::min(g.contentW, kNarrowContentWidth);
    g.narrowX = g.contentX + (g.contentW - g.narrowW) / 2;
    return g;
}

bool BodyGeometry::isDoubleWidth() const { return width >= synth::LayoutUtil::kDoubleWidth; }

// A double-width card pairs combos up; otherwise a high parameter count alone would add ~180px of
// dead single-column height.
int layoutChoiceRun(const std::vector<CaptionedWidget>& combos, int y, const BodyGeometry& g, bool apply) {
    constexpr int kStep = kLabelHeight + kRowHeight + 6;
    const int columns = g.isDoubleWidth() ? 2 : 1;
    const int count = (int)combos.size();
    if (columns == 1) {
        for (const auto& [combo, label] : combos) {
            if (apply) {
                label->setBounds(g.narrowX, y, g.narrowW, kLabelHeight);
                combo->setBounds(g.narrowX, y + kLabelHeight, g.narrowW, kRowHeight);
            }
            y += kStep;
        }
        return y;
    }
    const int cellW = g.contentW / columns;
    for (int i = 0; i < count; ++i) {
        const int cellX = g.contentX + (i % columns) * cellW;
        const int rowY = y + (i / columns) * kStep;
        if (apply) {
            combos[(size_t)i].second->setBounds(cellX, rowY, cellW - 8, kLabelHeight);
            combos[(size_t)i].first->setBounds(cellX, rowY + kLabelHeight, cellW - 8, kRowHeight);
        }
    }
    return y + ((count + columns - 1) / columns) * kStep;
}

// Toggles lay out flush-left at full content width: a toggle's label reads better there than
// centred in a narrow column.
int layoutToggleRun(const std::vector<juce::Component*>& toggles, int y, const BodyGeometry& g, bool apply) {
    for (auto* toggle : toggles) {
        if (apply)
            toggle->setBounds(g.contentX, y, g.contentW, kRowHeight);
        y += kRowHeight + 2;
    }
    return y;
}

int layoutKnobRun(const std::vector<CaptionedWidget>& knobs, int columns, int y, const BodyGeometry& g, bool apply) {
    const int knobColumns = g.isDoubleWidth() ? columns * 2 : columns;
    const int knobWidth = g.contentW / knobColumns;
    const int count = (int)knobs.size();
    for (int i = 0; i < count; ++i) {
        const int x = g.contentX + (i % knobColumns) * knobWidth;
        const int rowY = y + (i / knobColumns) * (kLabelHeight + kKnobHeight);
        if (apply) {
            knobs[(size_t)i].second->setBounds(x, rowY, knobWidth, kLabelHeight);
            knobs[(size_t)i].first->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
        }
    }
    return y + ((count + knobColumns - 1) / knobColumns) * (kLabelHeight + kKnobHeight);
}

int layoutViewRow(juce::Component* view, int height, int y, const BodyGeometry& g, bool apply) {
    if (apply)
        view->setBounds(g.contentX, y, g.contentW, height);
    return y + height + 6;
}

} // namespace synth::cardbody
