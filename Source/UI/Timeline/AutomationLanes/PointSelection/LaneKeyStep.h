// LaneKeyStep.h -- how far one keyboard move of automation points goes, and how a held key speeds up.
#pragma once

#include "Transport/CursorGlide.h"
#include "UI/Layout/KeyGlide/KeyedPointTween.h"
#include <cmath>

namespace synth::ui::lane_key {

inline constexpr double kFineValueStep = 0.01;   // of the lane's range, per Up/Down press
inline constexpr double kCoarseValueStep = 0.10; // with Shift
inline constexpr double kFineScale = 0.25;       // Alt+Shift: a quarter of the plain step, in time and in value
inline constexpr double kTapPixels = 4.0;        // a time step with snap off, at the current zoom

inline constexpr double kTimeStartBeatsPerSecond = 1.0;
inline constexpr double kTimeMaxBeatsPerSecond = 16.0;
inline constexpr double kSnappedStepSeconds = 0.25; // a snapped hold starts at one grid step per this long
inline constexpr double kValueStartRangePerSecond = 0.15;
inline constexpr double kValueMaxRangePerSecond = 1.2;
inline constexpr double kValueShiftSpeedFactor = 3.0;
inline constexpr double kTapMs = synth::cursor_glide::kTapMs;
inline constexpr double kRampMs = synth::cursor_glide::kRampMs;

/** The key a point's eased drawn offset is filed under: its beat to a millionth of a beat. */
inline KeyedPointTween::Key tweenKey(double beat) { return (KeyedPointTween::Key)std::llround(beat * 1e6); }

} // namespace synth::ui::lane_key
