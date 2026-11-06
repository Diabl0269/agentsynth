#pragma once

// ShapeDrawStroke.h -- what the pen drew on a shape curve and what that makes of the curve. No adapter and no UI, so
// the curve editor's draw gesture can use it. ONE function, drawOnCurve, computes the result; the drag's preview and
// the release's commit both call it (CurveDrawGesture), so what the drag shows is what the drop writes.
#include "Modules/Lfo/LfoCustomWave.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

/** What the pen drew: the shape, the stretch of the curve it covers and the numbers each shape needs. x is the
 *  curve's own axis (0..1), levels are curve y (0..1). */
struct ShapeDrawStroke {
    DrawShape shape = DrawShape::Sine;
    double x0 = 0.0;               ///< Line and periodic shapes: the stretch's start, x0 <= x1.
    double x1 = 1.0;               ///< ... and end.
    float y0 = 0.5f;               ///< Line: the level at x0.
    float y1 = 0.5f;               ///< Line: the level at x1.
    float lo = 0.5f;               ///< Periodic: the low points' level.
    float hi = 0.9f;               ///< Periodic: the high points' level.
    double cycleWidth = 1.0 / 3.0; ///< Periodic: the width of one cycle.
    /** Free: every mouse sample (x, y). The stretch is the samples' own extent. */
    std::vector<LfoCustomWave::Point> freehand;
};

struct ShapeDrawResult {
    LfoCustomWave curve;  ///< The curve after the stroke (the input curve when nothing changes).
    bool changed = false; ///< False when the stroke draws nothing or was refused.
    juce::String refusal; ///< Why it was refused ("Too many points..."), else empty.
    double cycles = 0.0;  ///< Periodic: how many cycles it drew (fewer than asked when they did not fit).
};

/** Replaces only the points inside the stroke's stretch with the stroke's points (the curve outside it is kept as
 *  it was). Periodic shapes shrink to the most whole cycles that fit under the curve's point cap; one that cannot fit
 *  at all is refused with a sentence. The result is sanitised. */
ShapeDrawResult drawOnCurve(const LfoCustomWave& current, const ShapeDrawStroke& stroke);

} // namespace synth::ui
