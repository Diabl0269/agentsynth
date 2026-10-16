#pragma once

// ShapeDrawing.h -- the automation lane's draw shapes applied to a shape curve. The periodic shapes come from the
// lane's own generator (LaneShapeGenerator), so a sine here and a sine drawn on a lane are the same points.
#include "ShapeListAdapter.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"

namespace synth::ui {

/** The curve `shape` makes of `current`. Free changes nothing; Line joins the first and last levels with a
 *  straight line; Sine, Triangle, Saw and Square repeat `range.cycles` times between `range.lo` and `range.hi`
 *  across the whole curve (a hold becomes a vertical step). The result is sanitised. */
LfoCustomWave curveForDrawShape(DrawShape shape, const LfoCustomWave& current, const ShapeDrawRange& range);

} // namespace synth::ui
