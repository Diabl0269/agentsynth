#pragma once

#include <algorithm>
#include <cmath>

// EdgeAutoScroll — the pure helpers shared by every surface that scrolls its own view while the pointer drags
// something near an edge (the timeline drags, the patch canvas, the controller surface, the note range picker):
// the speed curve (edgeScrollVelocity) and the intent rule (EdgeIntent: a press inside a zone scrolls only once the
// pointer moves toward that edge). No timer and no component: EdgeScrollPump is the clock. Owning it here (rather
// than duplicating the maths per editor) keeps "how fast does an edge-drag scroll" one decision instead of several
// that can silently drift apart.
namespace synth::ui {

// The width (px) of the edge zone, on EITHER side of the component, that arms auto-scroll. A shared constant rather
// than per-editor tuning, so every timeline drag feels the same at its edges.
constexpr int kEdgeZonePx = 24;

// The fastest an edge-drag scrolls a timeline view, in pixels per second (the pointer pinned to or beyond the edge):
// brisk without outrunning the eye at a typical zoom. Time-based, so it is the same at any display refresh rate;
// EdgeScrollPump (UI/Layout/EdgeScrollPump.h) turns it into a step per frame. Every timeline drag that follows the
// view reads it.
constexpr double kEdgeAutoScrollMaxPxPerSec = 540.0;

// The signed scroll speed (in the SAME units as `maxSpeed` — the caller decides: 1 for a -1..1 target an
// EdgeScrollPump scales to px per second) a pointer at `pos` should drive, given the component's visible span [lo, hi)
// and an edge zone `zonePx` wide at each end.
//
//   - 0 whenever `pos` is in the "dead" middle band [lo + zonePx, hi - zonePx] — no auto-scroll
//     while the pointer isn't near an edge.
//   - Negative (scroll toward lo/backward) when `pos` is within `zonePx` of `lo`, or further past
//     it entirely; positive (scroll toward hi/forward) the mirror on the `hi` side.
//   - Magnitude scales LINEARLY with penetration depth into the zone: just inside the zone is a
//     crawl, right at the edge is `maxSpeed`, and anything beyond the edge (the pointer dragged
//     off the component entirely, which JUCE still reports via mouseDrag) is clamped to
//     `maxSpeed` rather than growing without bound — a runaway scroll speed from a pointer that
//     wandered far off-screen would fly the view past where the user meant to land.
//
// `zonePx <= 0` or `hi <= lo` returns 0 (nothing to scale against). NaN/inf inputs are not
// guarded — callers pass component-local pixel coordinates and clamped ints, which are never
// either.
inline double edgeScrollVelocity(int pos, int lo, int hi, int zonePx, double maxSpeed) noexcept {
    if (zonePx <= 0 || hi <= lo)
        return 0.0;

    const int nearLoEdge = lo + zonePx;
    if (pos < nearLoEdge) {
        const double penetration = (double)(nearLoEdge - pos) / (double)zonePx; // 0 at nearLoEdge, 1 at lo
        return -maxSpeed * std::clamp(penetration, 0.0, 1.0);
    }

    const int nearHiEdge = hi - zonePx;
    if (pos > nearHiEdge) {
        const double penetration = (double)(pos - nearHiEdge) / (double)zonePx; // 0 at nearHiEdge, 1 at hi
        return maxSpeed * std::clamp(penetration, 0.0, 1.0);
    }

    return 0.0;
}

// Where a dragged thing is drawn along one axis of a view [lo, hi): the pointer, kept inside the span. A real pointer
// leaves the window mid-drag and JUCE keeps reporting it out there; the speed reads the real position (full speed past
// the edge) while what is dragged stays at the edge it left by, in view. `hi <= lo` returns `pos` unchanged.
inline int keepInsideSpan(int pos, int lo, int hi) noexcept { return hi <= lo ? pos : std::clamp(pos, lo, hi - 1); }

// How far (px) the pointer must travel toward an edge, from where the drag was pressed, before a press that landed
// inside that edge's zone may scroll toward it.
constexpr int kEdgeIntentPx = 4;

// One axis of a drag's edge intent: whether the pointer may scroll the view toward the edge it sits near. A press that
// lands inside an edge zone does not scroll until the pointer has moved kEdgeIntentPx toward that edge since the press,
// or has been outside that edge's zone once during the drag. Each axis and each edge counts on its own, so a press near
// the right edge still scrolls up at once when the pointer moves to the top. Never pressed (a drag that came in from
// outside the view, such as a library drop) is always armed.
class EdgeIntentAxis {
public:
    /** A new drag pressed at `pos` (the same space `armed` is asked in). */
    void press(int pos) noexcept {
        press_ = pos;
        pressed_ = true;
        leftLo_ = leftHi_ = false;
    }
    /** No press to wait on: every edge scrolls as soon as the pointer is in its zone. */
    void arm() noexcept { pressed_ = false; }

    /** Whether a pointer at `pos` over [lo, hi) with `zonePx` edge zones may scroll toward the edge it is near (true in
     *  the dead middle too, where there is nothing to scroll). Remembers the pointer leaving a zone, so call it with
     *  every pointer the drag reports (or every frame). */
    bool armed(int pos, int lo, int hi, int zonePx) noexcept {
        if (!pressed_ || zonePx <= 0 || hi <= lo)
            return true;
        const int loLine = lo + zonePx, hiLine = hi - zonePx;
        leftLo_ = leftLo_ || pos >= loLine;
        leftHi_ = leftHi_ || pos <= hiLine;
        if (pos < loLine)
            return leftLo_ || press_ >= loLine || press_ - pos >= kEdgeIntentPx;
        if (pos > hiLine)
            return leftHi_ || press_ <= hiLine || pos - press_ >= kEdgeIntentPx;
        return true;
    }

private:
    int press_ = 0;
    bool pressed_ = false;
    bool leftLo_ = false;
    bool leftHi_ = false;
};

// Both axes of a drag's edge intent (EdgeIntentAxis), for a view that scrolls both ways.
struct EdgeIntent {
    EdgeIntentAxis x, y;
    void press(int px, int py) noexcept {
        x.press(px);
        y.press(py);
    }
    void arm() noexcept {
        x.arm();
        y.arm();
    }
};

// edgeScrollVelocity, held at 0 until `intent` says the pointer has moved toward the edge it sits near.
inline double edgeScrollVelocity(int pos, int lo, int hi, int zonePx, double maxSpeed,
                                 EdgeIntentAxis& intent) noexcept {
    const bool armed = intent.armed(pos, lo, hi, zonePx); // asked every time, so a pointer leaving a zone is noted
    return armed ? edgeScrollVelocity(pos, lo, hi, zonePx, maxSpeed) : 0.0;
}

} // namespace synth::ui
