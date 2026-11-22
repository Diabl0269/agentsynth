// CustomFilterWholeShape.h -- the Custom filter's "Whole shape" Resonance mode as a pure function on the dB grid: the
// kernel designer and the response views both call it, so the curve drawn is the curve designed.
#pragma once

namespace synth::customfilter {

/** Scratch floats applyWholeShapeResonance needs for `numPoints` grid points. */
constexpr int wholeShapeScratchSize(int numPoints) noexcept { return 2 * numPoints + 2; }

/** Applies Whole-shape resonance in place to `db`: `numPoints` (at least 2) dB values on the curve axis (point i at
 *  x = i / (numPoints - 1), 20 Hz..20 kHz), already shifted and scaled by Depth. `resonance` is held to 0..1; at 0
 *  `db` is left untouched. `scratch` holds wholeShapeScratchSize(numPoints) floats. No allocation, locks or system
 *  calls. */
void applyWholeShapeResonance(float* db, int numPoints, float resonance, float* scratch) noexcept;

/** How much of the shape's depth resonance `resonance` fills in (0 at 0, about 0.84 at 1). */
float wholeShapeFill(float resonance) noexcept;

} // namespace synth::customfilter
