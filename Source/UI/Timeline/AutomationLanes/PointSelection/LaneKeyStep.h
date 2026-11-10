// LaneKeyStep.h -- how far one keyboard move of automation points goes, and how a held key speeds up.
#pragma once

#include "Transport/CursorGlide.h"
#include "UI/Layout/KeyGlide/KeyedPointTween.h"
#include <algorithm>
#include <cmath>

namespace synth::ui::lane_key {

inline constexpr double kFineValueStep = 0.01;   // of the lane's range, per Up/Down press
inline constexpr double kCoarseValueStep = 0.10; // with Shift
inline constexpr double kFineScale = 0.25;       // Alt+Shift: a quarter of the plain step, in time and in value
inline constexpr double kTapPixels = 4.0;        // a time step with snap off, at the current zoom

inline constexpr double kTimeStartBeatsPerSecond = 1.0;
inline constexpr double kTimeMaxBeatsPerSecond = 16.0;
inline constexpr double kSnappedStepSeconds = 0.25;       // a snapped hold starts at one grid step per this long
inline constexpr double kSnappedMaxStepsPerSecond = 12.0; // and tops out at this many grid steps a second
inline constexpr double kValueStartRangePerSecond = 0.15;
inline constexpr double kValueMaxRangePerSecond = 1.2;
inline constexpr double kValueShiftSpeedFactor = 3.0;
inline constexpr double kTapMs = synth::cursor_glide::kTapMs;
inline constexpr double kRampMs = synth::cursor_glide::kRampMs;

/** A held key's pace, in the axis's units per second: where its glide starts and the cap it ramps up to. */
struct HoldPace {
    double start = 0.0;
    double max = 0.0;
};

/** A snapped hold's pace for grid steps of `step` units: one step per kSnappedStepSeconds at the start (never slower
 *  than `freeStart`, an unsnapped hold's start), and kSnappedMaxStepsPerSecond steps at the cap, whatever the step's
 *  size, so a coarse grid does not race and every grid steps at an even, countable pace. The cap wins over the start:
 *  a grid so fine that the free start would pass it starts at the cap. Both point editors use it. */
inline HoldPace snappedHoldPace(double step, double freeStart) {
    const double max = kSnappedMaxStepsPerSecond * step;
    return {std::min(max, std::max(freeStart, step / kSnappedStepSeconds)), max};
}

/** Whether a snapped step `msSinceLastStep` after the one before eases its drawn point. Steps that come faster than
 *  the ease runs would restart it before it lands, so the drawn point would trail further behind on every step; those
 *  move the drawn point with the hold instead. */
inline bool snappedStepEases(double msSinceLastStep) { return msSinceLastStep >= KeyedPointTween::kMs; }

/** The key a point's eased drawn offset is filed under: its beat to a millionth of a beat. */
inline KeyedPointTween::Key tweenKey(double beat) { return (KeyedPointTween::Key)std::llround(beat * 1e6); }

} // namespace synth::ui::lane_key
