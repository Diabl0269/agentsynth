#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

// Pure, headless edit maths behind PianoRollControllerLanes' gestures: what a velocity ramp or a CC
// stroke turns a clip's data INTO. No component, no doc mutation — the strip previews with these and
// commits their result in one TimelineDoc call. All beats here are CLIP-RELATIVE.
namespace synth::ui::lanes {

// The velocity lane's id; 0..127 is a CC lane for that controller number.
inline constexpr int kVelocityLane = -1;

// Display name of a lane ("Velocity", "CC1 Mod Wheel", "CC74").
juce::String laneName(int laneId);
// The curve new points get on this controller: Hold for the switch pedals (64..69), else Linear.
int defaultCurveFor(int ccNumber) noexcept;

// Velocities for every note whose start lies in [min(beatA, beatB), max(...)], interpolated along
// the straight line (beatA, valueA) -> (beatB, valueB), rounded and clamped to 1..127. Notes at or
// past the clip end are skipped; a non-empty `restrictTo` limits the result to those notes.
std::vector<std::pair<synth::NoteId, int>> velocityLine(const synth::Clip& clip,
                                                        const std::vector<synth::NoteId>& restrictTo, double beatA,
                                                        double valueA, double beatB, double valueB);

// `existing` with every point in [from, to] replaced by `inserted` (which must lie inside it);
// the result is sorted, same-beat collisions resolved in favour of `inserted`.
std::vector<synth::ControllerPoint> replaceSpan(const std::vector<synth::ControllerPoint>& existing, double from,
                                                double to, const std::vector<synth::ControllerPoint>& inserted);

// A raw freehand stroke thinned with AutomationRecorder::thinPoints; `pixelValue` is one pixel of the
// lane in value units (the tolerance floor). Every kept point gets `curve`. Input sorted by beat.
std::vector<synth::ControllerPoint> thinStroke(const std::vector<synth::ControllerPoint>& raw, int curve,
                                               double pixelValue);

} // namespace synth::ui::lanes
