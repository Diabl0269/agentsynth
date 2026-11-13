#pragma once

namespace synth {

/** Stateless 4-point, 3rd-order Hermite (Catmull-Rom) interpolation: the value at `x` in [0, 1)
 *  between `y0` and `y1`, with `ym1` before and `y2` after them. Exact at x = 0 (returns `y0`) and
 *  reproduces a straight line exactly. Used by the clip streamer's fill step to convert a file's
 *  frames to engine-rate frames. */
inline float hermite4(float ym1, float y0, float y1, float y2, float x) noexcept {
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * x + c2) * x + c1) * x + y0;
}

} // namespace synth
