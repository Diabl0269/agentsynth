#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// TooltipAnchor.h (docs/layout/animation.md#tooltips): a component that is a thin row of named neighbours (the Sections
// strip) sets the property below to true on itself so the shared AppTooltipWindow keeps its tip off the whole row
// instead of covering the next block's name. A property rather than a base class so the row's header stays free of
// this include. Every component without it keeps juce's placement.
namespace synth::ui {

namespace tooltip_anchor {

/** Component property (bool): "keep my whole bounds clear of my tip". */
inline const juce::Identifier kKeepClearProperty{"tooltipKeepClear"};

constexpr int kGap = 4; // between the avoid rect and the tip

/** Where a tip of `size` goes for an avoid rect: just below it with its left edge at `pointerX` (held inside `area`),
 *  or just above it when it would leave the bottom of `area`; when neither side has room it takes the side with more.
 */
inline juce::Rectangle<int> placeAvoiding(juce::Point<int> size, juce::Rectangle<int> avoid, int pointerX,
                                          juce::Rectangle<int> area) {
    const int x = juce::jlimit(area.getX(), std::max(area.getX(), area.getRight() - size.x), pointerX);
    const int below = avoid.getBottom() + kGap;
    const int above = avoid.getY() - kGap - size.y;
    const bool fitsBelow = below + size.y <= area.getBottom();
    const bool fitsAbove = above >= area.getY();
    int y = below;
    if (!fitsBelow && (fitsAbove || avoid.getY() - area.getY() > area.getBottom() - avoid.getBottom()))
        y = above;
    return {x, y, size.x, size.y};
}

} // namespace tooltip_anchor
} // namespace synth::ui
