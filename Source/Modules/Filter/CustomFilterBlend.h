// CustomFilterBlend.h -- the Custom type's blended response as plain numbers, for everything that
// shows or copies it (the card's response view, "add a shape here", the editor window). It uses the
// engine's own weights and grid, so what is drawn is what the kernel designer is asked for.
#pragma once

#include "CustomFilterEngine.h"
#include <vector>

namespace synth::customfilter {

constexpr int kGridSize = CustomFilterEngine::kGridSize;

/** The shapes' dB curves blended at map position (x, y), on the engine's kGridSize log-frequency grid
 *  (grid point i is x = i / (kGridSize - 1)). No shift, no depth. */
std::vector<float> blendedGridDb(const CustomFilterShapes& shapes, float x, float y);

/** blendedGridDb with the designer's shift and depth applied: what the filter's magnitude is asked to be. */
std::vector<float> responseGridDb(const CustomFilterShapes& shapes, float x, float y, float shiftSemitones,
                                  float depth);

/** responseGridDb with Cutoff and Resonance as the designer applies them: the curve also moves by
 *  cutoffShiftSemitones(cutoffHz) and, after depth, the resonant bell at cutoffHz is added. At the reference
 *  cutoff (440 Hz) and resonance 0 it equals the overload above. */
std::vector<float> responseGridDb(const CustomFilterShapes& shapes, float x, float y, float shiftSemitones, float depth,
                                  float cutoffHz, float resonance);

/** Samples `gridDb` back into a curve of `numPoints` evenly spaced points (the first at x = 0, the last at
 *  x = 1; at least LfoCustomWave::kMinPoints). The result is sanitised and every y is in [0, 1]. */
LfoCustomWave curveFromGridDb(const std::vector<float>& gridDb, int numPoints);

} // namespace synth::customfilter
