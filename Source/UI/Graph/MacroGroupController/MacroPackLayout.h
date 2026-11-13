#pragma once

// MacroPackLayout.h
//
// Where the folded macro cards go when "Fold and Pack Macros" tidies them (docs/macros/menu-and-membership.md). Pure:
// rectangles in, top-lefts out, no components. Rows of up to four cards, 24 px apart, in reading order of where the
// cards stood (top to bottom, then left to right), starting at the top-left corner of the first of them.

#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <numeric>
#include <vector>

namespace macro_pack {

inline constexpr int kColumns = 4;
inline constexpr int kGap = 24;

/** The new top-left of each rect in `rects`, in the same order. The anchor is the top-left of the rect that comes first
 *  in reading order; a row is as tall as its tallest card. */
inline std::vector<juce::Point<int>> packedPositions(const std::vector<juce::Rectangle<int>>& rects) {
    std::vector<size_t> order(rects.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&rects](size_t a, size_t b) {
        const auto pa = rects[a].getPosition(), pb = rects[b].getPosition();
        return pa.y != pb.y ? pa.y < pb.y : pa.x < pb.x;
    });

    std::vector<juce::Point<int>> out(rects.size());
    if (order.empty())
        return out;
    const auto anchor = rects[order.front()].getPosition();
    int x = anchor.x, y = anchor.y, rowHeight = 0, column = 0;
    for (const auto index : order) {
        out[index] = {x, y};
        x += rects[index].getWidth() + kGap;
        rowHeight = std::max(rowHeight, rects[index].getHeight());
        if (++column == kColumns) {
            column = 0;
            x = anchor.x;
            y += rowHeight + kGap;
            rowHeight = 0;
        }
    }
    return out;
}

} // namespace macro_pack
