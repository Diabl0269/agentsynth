#pragma once

// CanvasEdgeScroll.h
//
// How fast the patch canvas pans while a card is dragged near the edge of its view. Pure functions only (no
// component, no timer), so the speed curve is unit-tested on its own; CanvasEdgeScroller (CanvasEdgeScroller.h)
// applies it, one eased step a display frame. The curve itself is the timeline's (UI/Timeline/EdgeAutoScroll.h): linear
// in how far into the edge zone the pointer is, so the canvas crawls where the zone starts and runs at full speed at
// the edge.

#include "UI/Timeline/EdgeAutoScroll.h"

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** Width (px) of the strip along each side of the canvas view that starts a scroll. */
constexpr int kCanvasEdgeZonePx = 48;
/** Pan speed (screen px per second) with the pointer on the edge or past it; EdgeScrollPump eases toward it and steps
 *  it once per display frame. */
constexpr float kCanvasEdgeMaxPxPerSec = 660.0f;

/** The zone actually used for a view `extent` px wide: never more than a third of it, so a small view keeps a
 *  dead middle band where nothing scrolls. */
inline int canvasEdgeZoneFor(int extent) noexcept { return std::min(kCanvasEdgeZonePx, extent / 3); }

/** The pan to aim for, as a fraction (-1..1 per axis) of kCanvasEdgeMaxPxPerSec, for a pointer at `pointer`
 *  (view-local) over a view of `view`, in the direction it adds to the canvas pan offset: the pointer near the right
 *  edge gives a negative x (the content moves left, so the view moves right). Zero in the middle of the view; the
 *  magnitude grows linearly to 1 at the edge. */
inline juce::Point<float> canvasEdgePanTarget(juce::Point<int> pointer, juce::Rectangle<int> view) noexcept {
    const auto axis = [](int pos, int lo, int hi) {
        return (float)-edgeScrollVelocity(pos, lo, hi, canvasEdgeZoneFor(hi - lo), 1.0);
    };
    return {axis(pointer.x, view.getX(), view.getRight()), axis(pointer.y, view.getY(), view.getBottom())};
}

/** canvasEdgePanTarget, held at zero on each axis until `intent` says the pointer moved toward that edge. */
inline juce::Point<float> canvasEdgePanTarget(juce::Point<int> pointer, juce::Rectangle<int> view,
                                              EdgeIntent& intent) noexcept {
    const auto axis = [](int pos, int lo, int hi, EdgeIntentAxis& gate) {
        return (float)-edgeScrollVelocity(pos, lo, hi, canvasEdgeZoneFor(hi - lo), 1.0, gate);
    };
    return {axis(pointer.x, view.getX(), view.getRight(), intent.x),
            axis(pointer.y, view.getY(), view.getBottom(), intent.y)};
}

} // namespace synth::ui
