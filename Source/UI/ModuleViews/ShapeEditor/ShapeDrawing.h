#pragma once

// ShapeDrawing.h -- the automation lane's draw shapes applied to a shape curve. The periodic shapes come from the
// lane's own generator (LaneShapeGenerator), so a sine here and a sine drawn on a lane are the same points, and the
// freehand pen thins its stroke with the lane Pencil's rule (AutomationRecorder::thinPoints).
//
// ONE function, drawOnCurve, computes what a stroke makes of a curve. The drag's preview and the release's commit
// both call it (CurveDrawGesture), so what the drag shows is what the drop writes.
#include "ShapeDrawStroke.h"
#include "ShapeListAdapter.h"

namespace synth::ui {

/** The stroke a whole-curve draw makes: the full width, `range.cycles` cycles between `range.lo` and `range.hi`, and
 *  for Line the curve's own end levels. Free has no stroke (a default one that draws nothing). */
ShapeDrawStroke wholeCurveStroke(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range);

/** The same stroke over x0..x1 only (the keyboard route's selected stretch): Line joins the levels the curve has at
 *  the two ends. */
ShapeDrawStroke spanStroke(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range, double x0,
                           double x1);

/** The curve `shape` makes of `current` over the full width (Free changes nothing). */
LfoCustomWave curveForDrawShape(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range);

} // namespace synth::ui
