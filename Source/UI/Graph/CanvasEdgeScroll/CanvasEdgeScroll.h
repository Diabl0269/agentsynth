#pragma once

// CanvasEdgeScroll.h
//
// How fast the patch canvas pans while a card is dragged near the edge of its view. Pure functions only (no
// component, no timer), so the speed curve is unit-tested on its own; CanvasEdgeScroller (CanvasEdgeScroller.h) is
// the timer that applies it. The curve itself is the timeline's (UI/Timeline/EdgeAutoScroll.h): linear in how far
// into the edge zone the pointer is, so the canvas crawls where the zone starts and runs at full speed at the edge.

#include "UI/Timeline/EdgeAutoScroll.h"

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** Width (px) of the strip along each side of the canvas view that starts a scroll. */
constexpr int kCanvasEdgeZonePx = 48;
/** Pan speed (screen px per tick) with the pointer on the edge or past it. */
constexpr float kCanvasEdgeMaxPxPerTick = 22.0f;

/** The zone actually used for a view `extent` px wide: never more than a third of it, so a small view keeps a
 *  dead middle band where nothing scrolls. */
inline int canvasEdgeZoneFor(int extent) noexcept { return std::min(kCanvasEdgeZonePx, extent / 3); }

/** The screen-pixel change to add to the canvas pan offset this tick for a pointer at `pointer` (view-local) over a
 *  view of `view`. The pointer near the right edge gives a negative x (the content moves left, so the view moves
 *  right). Zero in the middle of the view; magnitude grows linearly to kCanvasEdgeMaxPxPerTick at the edge. */
inline juce::Point<float> canvasEdgePanDelta(juce::Point<int> pointer, juce::Rectangle<int> view) noexcept {
    const auto axis = [](int pos, int lo, int hi) {
        return (float)-edgeScrollVelocity(pos, lo, hi, canvasEdgeZoneFor(hi - lo), (double)kCanvasEdgeMaxPxPerTick);
    };
    return {axis(pointer.x, view.getX(), view.getRight()), axis(pointer.y, view.getY(), view.getBottom())};
}

} // namespace synth::ui
