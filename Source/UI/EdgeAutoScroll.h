#pragma once

#include <algorithm>
#include <cmath>

// EdgeAutoScroll — a small pure helper shared by every timeline surface that scrolls its own view
// while the pointer is dragging something near an edge (a clip drag in TimelineClipLaneArea today;
// the piano roll gets its own wiring in a later wave — see that class's header comment).
//
// Deliberately just a function plus two constants: no state, no timer, no component. Owning it
// here (rather than duplicating the maths per editor) is what keeps "how fast does an edge-drag
// scroll" one decision instead of two that can silently drift apart.
namespace synth::ui {

// The width (px) of the edge zone, on EITHER side of the component, that arms auto-scroll — and the
// rate (Hz) the owning component's gated juce::Timer ticks at while a drag sits inside it. Both are
// deliberately shared constants rather than per-editor tuning: the piano roll's future wiring is
// expected to feel identical to the clip lanes', not merely similar.
constexpr int kEdgeZonePx = 24;
constexpr int kEdgeScrollHz = 30;

// The signed scroll speed (in the SAME units as `maxPerTick` — the caller decides px or beats per
// tick) a pointer at `pos` should drive, given the component's visible span [lo, hi) and an edge
// zone `zonePx` wide at each end.
//
//   - 0 whenever `pos` is in the "dead" middle band [lo + zonePx, hi - zonePx] — no auto-scroll
//     while the pointer isn't near an edge.
//   - Negative (scroll toward lo/backward) when `pos` is within `zonePx` of `lo`, or further past
//     it entirely; positive (scroll toward hi/forward) the mirror on the `hi` side.
//   - Magnitude scales LINEARLY with penetration depth into the zone: just inside the zone is a
//     crawl, right at the edge is `maxPerTick`, and anything beyond the edge (the pointer dragged
//     off the component entirely, which JUCE still reports via mouseDrag) is clamped to
//     `maxPerTick` rather than growing without bound — a runaway scroll speed from a pointer that
//     wandered far off-screen would fly the view past where the user meant to land.
//
// `zonePx <= 0` or `hi <= lo` returns 0 (nothing to scale against). NaN/inf inputs are not
// guarded — callers pass component-local pixel coordinates and clamped ints, which are never
// either.
inline double edgeScrollVelocity(int pos, int lo, int hi, int zonePx, double maxPerTick) noexcept {
    if (zonePx <= 0 || hi <= lo)
        return 0.0;

    const int nearLoEdge = lo + zonePx;
    if (pos < nearLoEdge) {
        const double penetration = (double)(nearLoEdge - pos) / (double)zonePx; // 0 at nearLoEdge, 1 at lo
        return -maxPerTick * std::clamp(penetration, 0.0, 1.0);
    }

    const int nearHiEdge = hi - zonePx;
    if (pos > nearHiEdge) {
        const double penetration = (double)(pos - nearHiEdge) / (double)zonePx; // 0 at nearHiEdge, 1 at hi
        return maxPerTick * std::clamp(penetration, 0.0, 1.0);
    }

    return 0.0;
}

} // namespace synth::ui
