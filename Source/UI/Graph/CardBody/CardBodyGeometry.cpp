// CardBodyGeometry.cpp -- the run layouts every card body is built from. The numbers are the generic
// card's, unchanged: the hosted-plugin card lays its widgets out with the same runs.
#include "CardBodyGeometry.h"
#include "UI/Layout/LayoutUtil.h"
#include <algorithm>

namespace synth::cardbody {

namespace {

// A null entry is a cell kept empty (a swap group with no member shown): it takes its space, unpainted.
void place(juce::Component* component, juce::Rectangle<int> bounds) {
    if (component != nullptr)
        component->setBounds(bounds);
}

} // namespace

// Body content sits below every jack, so it gets nearly the full card width. Single-column widgets
// (combos, the load row) look stretched at full width, so they stay centred in a narrower band.
BodyGeometry BodyGeometry::forCardWidth(int width, int rightGutter) {
    BodyGeometry g;
    g.width = width;
    g.rightGutter = rightGutter;
    g.contentX = kContentMargin;
    g.contentW = std::max(60, width - kContentMargin * 2 - rightGutter);
    g.narrowW = std::min(g.contentW, kNarrowContentWidth);
    g.narrowX = g.contentX + (g.contentW - g.narrowW) / 2;
    return g;
}

bool BodyGeometry::isDoubleWidth() const { return width - rightGutter >= synth::LayoutUtil::kDoubleWidth; }

// A double-width card pairs combos up; otherwise a high parameter count alone would add ~180px of
// dead single-column height.
int layoutChoiceRun(const std::vector<CaptionedWidget>& combos, int y, const BodyGeometry& g, bool apply) {
    constexpr int kStep = kLabelHeight + kRowHeight + 6;
    const int columns = g.isDoubleWidth() ? 2 : 1;
    const int count = (int)combos.size();
    if (columns == 1) {
        for (const auto& [combo, label] : combos) {
            if (apply) {
                place(label, {g.narrowX, y, g.narrowW, kCaptionBoxHeight});
                place(combo, {g.narrowX, y + kLabelHeight, g.narrowW, kRowHeight});
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
            place(combos[(size_t)i].second, {cellX, rowY, cellW - 8, kCaptionBoxHeight});
            place(combos[(size_t)i].first, {cellX, rowY + kLabelHeight, cellW - 8, kRowHeight});
        }
    }
    return y + ((count + columns - 1) / columns) * kStep;
}

// Toggles lay out flush-left at full content width: a toggle's label reads better there than
// centred in a narrow column.
int layoutToggleRun(const std::vector<juce::Component*>& toggles, int y, const BodyGeometry& g, bool apply) {
    for (auto* toggle : toggles) {
        if (apply)
            place(toggle, {g.contentX, y, g.contentW, kRowHeight});
        y += kRowHeight + 2;
    }
    return y;
}

int gridCellWidth(int columns, const BodyGeometry& g) {
    return g.contentW / (g.isDoubleWidth() ? columns * 2 : columns);
}

int layoutKnobRun(const std::vector<CaptionedWidget>& knobs, int columns, int y, const BodyGeometry& g, bool apply) {
    return layoutGridRun(knobs, columns, kKnobHeight, 0, y, g, apply);
}

int layoutGridRun(const std::vector<CaptionedWidget>& cells, int columns, int cellHeight, int widgetWidth, int y,
                  const BodyGeometry& g, bool apply) {
    const int gridColumns = g.isDoubleWidth() ? columns * 2 : columns;
    const int cellWidth = gridCellWidth(columns, g);
    const int width = widgetWidth > 0 ? std::min(widgetWidth, cellWidth) : cellWidth;
    const int count = (int)cells.size();
    for (int i = 0; i < count; ++i) {
        const int x = g.contentX + (i % gridColumns) * cellWidth;
        const int rowY = y + (i / gridColumns) * (kLabelHeight + cellHeight);
        if (apply) {
            place(cells[(size_t)i].second, {x, rowY, cellWidth, kCaptionBoxHeight});
            place(cells[(size_t)i].first, {x + (cellWidth - width) / 2, rowY + kLabelHeight, width, cellHeight});
        }
    }
    return y + ((count + gridColumns - 1) / gridColumns) * (kLabelHeight + cellHeight);
}

// Each cell goes through layoutGridRun as a run of one in a geometry that is its own column (one column wide, never
// double width), so a cell's caption and widget are placed by that one function whatever its neighbours are.
int layoutMixedGridRun(const std::vector<GridCell>& cells, int columns, int y, const BodyGeometry& g, bool apply) {
    const int gridColumns = g.isDoubleWidth() ? columns * 2 : columns;
    const int cellWidth = gridCellWidth(columns, g);
    auto column = g;
    column.width = 0;
    column.rightGutter = 0;
    column.contentW = column.narrowW = cellWidth;
    const int count = (int)cells.size();
    for (int row = 0; row * gridColumns < count; ++row) {
        const int first = row * gridColumns;
        const int last = std::min(count, first + gridColumns);
        int rowHeight = 0;
        for (int i = first; i < last; ++i) {
            rowHeight = std::max(rowHeight, kLabelHeight + cells[(size_t)i].cellHeight);
            if (!apply)
                continue;
            column.contentX = column.narrowX = g.contentX + (i - first) * cellWidth;
            layoutGridRun({cells[(size_t)i].widgets}, 1, cells[(size_t)i].cellHeight, cells[(size_t)i].widgetWidth, y,
                          column, true);
        }
        y += rowHeight;
    }
    return y;
}

int layoutCaptionedRows(const std::vector<CaptionedWidget>& rows, int rowHeight, bool wide, int y,
                        const BodyGeometry& g, bool apply) {
    const int x = wide ? g.contentX : g.narrowX;
    const int w = wide ? g.contentW : g.narrowW;
    for (const auto& [widget, label] : rows) {
        if (apply) {
            place(label, {x, y, w, kCaptionBoxHeight});
            place(widget, {x, y + kLabelHeight, w, rowHeight});
        }
        y += kLabelHeight + rowHeight + 6;
    }
    return y;
}

int layoutViewRow(juce::Component* view, int height, int y, const BodyGeometry& g, bool apply) {
    if (apply)
        place(view, {g.contentX, y, g.contentW, height});
    return y + height + 6;
}

} // namespace synth::cardbody
