// CustomFilterPeakMorph.h -- the Custom filter's morph blend: each shape's prominent peaks, found once per
// shape list, glide toward each other as the cursor moves, so a vowel morph sweeps its formants instead of
// fading one set out while the other fades in. The engine's kernel path and blendedGridDb both blend here.
#pragma once

#include "CustomFilterEngine.h"

namespace synth::customfilter {

/** Peaks rising at least this far above the higher of their two cols are matched and moved. */
constexpr float kMinPeakProminenceDb = 5.0f;

/** Writes the grid positions of up to ShapeTable::kMaxPeaks prominent peaks of `db` (`n` points) to `out`,
 *  ascending; returns how many. */
int findPeaks(const float* db, int n, float* out) noexcept;

/** Blends `table`'s shapes with `weights` (table.count values, summing to 1) into `out` (kGridSize values).
 *  No allocation, locks or system calls. */
void blendShapes(const CustomFilterEngine::ShapeTable& table, const float* weights, float* out) noexcept;

} // namespace synth::customfilter
