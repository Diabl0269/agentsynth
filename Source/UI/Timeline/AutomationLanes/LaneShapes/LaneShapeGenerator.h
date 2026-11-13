#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <vector>

namespace synth::ui {

// Pure breakpoint generation for the periodic Draw shapes. No doc, no UI: the gesture and the lane-range
// stamp both call these and commit the result through TimelineDoc::editBreakpoints themselves.

/** Points per cycle of a stamped sine (Linear segments). */
inline constexpr int kSinePointsPerCycle = 16;

/**
 * The breakpoints of `shape` repeated every `cycleBeats` over [startBeat, endBeat], swinging between
 * `lo` and `hi`. Sorted by beat, beats unique, the last point exactly at `endBeat` (Linear) so the
 * segment into any point after the span keeps its meaning. Empty for a non-periodic shape, an empty
 * span or a non-positive cycle. Callers check estimateShapePointCount() first: the count is unbounded.
 */
std::vector<synth::AutomationLane::Breakpoint> generateShapePoints(DrawShape shape, double startBeat, double endBeat,
                                                                   double cycleBeats, double lo, double hi);

/** An upper bound on generateShapePoints()'s size, computed without generating; 0 when it would be empty. */
long long estimateShapePointCount(DrawShape shape, double startBeat, double endBeat, double cycleBeats);

/** How far before its cycle's end a saw's top (Hold) point sits, so the drop lands on the next cycle's start. */
double sawDropBeats(double cycleBeats) noexcept;

} // namespace synth::ui
