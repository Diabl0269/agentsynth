#pragma once

// CurveWaveConversion.h -- the Free-mode CurveModel <-> synth::LfoCustomWave mapping every wave-shaped curve
// editor shares (the LFO card's custom waveform, the Filter's shape editor).
#include "CurveModel.h"
#include "Modules/Lfo/LfoCustomWave.h"

namespace synth::ui {

/** A Free-mode model of `wave`. The two end points are pinned in x (a wave always spans 0..1); every point is
 *  movable in y over 0..1. */
CurveModel curveModelFromWave(const LfoCustomWave& wave);

/** The wave `model` draws, sanitised. */
LfoCustomWave waveFromCurveModel(const CurveModel& model);

} // namespace synth::ui
