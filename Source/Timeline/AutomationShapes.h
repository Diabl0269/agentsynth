#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cstdint>
#include <vector>

// Automation shape generation for the timeline's Shape tool — pure math, no UI/Component
// dependency (Core target), so it is unit-testable without a display and reusable if a non-editor
// caller (a macro, a script) ever wants to stamp a waveform into a lane.
namespace synth {

// The periodic waveform a Shape-tool drag stamps into a lane. Transient UI/generation input only —
// never stored on a Breakpoint or serialised — so, unlike BreakpointCurve/TrackKind, it carries no
// int-stability contract and may gain members freely.
enum class ShapeKind { Sine, Triangle, Square, SawUp, SawDown, Random };

// Fills [startBeat, endBeat] with one or more cycles of `kind`, oscillating between lowValue and
// highValue (either order — the pair is sorted internally) every `periodBeats`. `phase` (radians)
// picks where in the cycle the shape starts: 0 starts at lowValue, PI at highValue; Random ignores
// it. `randomSeed` is Random's only source of variation — the same seed with the same span/period
// reproduces the exact same points, every call, on every platform.
//
// Contract (pinned by Tests/Timeline/AutomationShapesTests.cpp):
//   - endBeat <= startBeat, or a non-finite bound, -> empty (nothing to draw).
//   - periodBeats <= 0 (or non-finite) is treated as one cycle spanning the whole span.
//   - Every returned point's beat is within [startBeat, endBeat], strictly increasing, and the
//     LAST point's beat is always exactly endBeat, so the drawn shape ends there cleanly and
//     nothing after it is implied to move (AutomationKernel holds a lane flat past its last point).
//   - Every returned point's value is clamped into [min(lowValue, highValue), max(...)].
//   - Never returns a BreakpointCurve::Bezier point (reserved, no evaluator) — Hold for
//     Square/Random, Linear otherwise.
//   - Never returns more than TimelineDoc::kMaxBreakpointsPerLane points: a period tiny next to the
//     span coarsens (a larger effective period) rather than growing the run without bound.
std::vector<AutomationLane::Breakpoint> generateAutomationShape(ShapeKind kind, double startBeat, double endBeat,
                                                                double periodBeats, double lowValue, double highValue,
                                                                double phase = 0.0, std::uint32_t randomSeed = 1);

} // namespace synth
