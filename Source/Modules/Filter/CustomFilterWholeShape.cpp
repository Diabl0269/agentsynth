// CustomFilterWholeShape.cpp -- Whole-shape resonance: the drawn response resonates on its own features.
#include "CustomFilterWholeShape.h"
#include "CustomFilterShapes.h"
#include <algorithm>
#include <cmath>

namespace synth::customfilter {

// Measured on a drawable pole-zero filter (a vowel shape, cutoff 300 Hz / 1 kHz / 4 kHz): resonance is not a bell at
// the cutoff. At 25% the cut region already rises 5-10 dB, at 50% by 13-48 dB with the peaks standing on the shape's
// own features, and from 75% the whole response lifts and rings. Three terms reproduce that on the dB curve:
//   fill:    the curve's dB scaled by 1 - wholeShapeFill(r), so deep cuts rise most (r^3 / (r^3 + 0.42^3) * 0.9:
//            about +9 dB on a -60 dB cut at 25%, +34 dB at 50%).
//   sharpen: the curve minus its own ~0.3-octave smoothing (its local relief) added back r^2 times, 5x where the
//            curve stands above its surroundings (peaks rise; a plain low-pass gets a resonant hump at its knee)
//            and 1.5x where it sits below (notches stay notches).
//   level:   then the whole curve moves down so its power is no more than it was at resonance 0, under two weights:
//            f^-0.75 (between a saw's spectrum and pink noise, the plain level) and a saw's f^-1 with a +3 dB
//            presence shelf loudness meters add above ~1.5 kHz, rolled off below 100 Hz (what the ear hears as louder).
//            The larger rise wins, so neither the level nor the loudness grows as Resonance turns up. Never up.
// The design ceiling (+30 dB in the kernel designer) caps how high a peak can ring.
namespace {
constexpr float kFillMax = 0.9f;
constexpr float kFillKnee = 0.42f;
constexpr float kSmoothOctaves = 0.3f; // half-width of each of the three box passes
constexpr float kPeakGain = 5.0f;
constexpr float kDipGain = 1.5f;
constexpr double kLevelSlope = -0.75;   // the level weight is f^kLevelSlope
constexpr double kLoudnessSlope = -1.0; // the loudness weight is f^kLoudnessSlope times the presence shelf
constexpr double kShelfHz = 1500.0;
constexpr double kLowCutHz = 100.0; // the loudness weight rolls off below here, as a loudness meter does
constexpr double kShelfPower = 1.0; // +3 dB: power gain 1 + 1.0 well above kShelfHz
constexpr float kMaxDb = 30.0f;

float heldResonance(float r) noexcept { return std::isfinite(r) ? std::clamp(r, 0.0f, 1.0f) : 0.0f; }

/** Three passes of a centred moving average (edges held), `halfWidth` points each side: a near-Gaussian smoothing in
    O(n). `in` and `out` may not alias; `tmp` holds n + 1 floats. Each pass builds its running sum and reads its end
    values before writing `out`, so the later passes can read and write `out` in place. */
void smooth(const float* in, float* out, float* tmp, int n, int halfWidth) noexcept {
    const float* src = in;
    const float width = (float)(2 * halfWidth + 1);
    for (int pass = 0; pass < 3; ++pass) {
        tmp[0] = 0.0f;
        for (int i = 0; i < n; ++i)
            tmp[i + 1] = tmp[i] + src[i];
        const float first = src[0], last = src[n - 1];
        for (int i = 0; i < n; ++i) {
            const int lo = i - halfWidth, hi = i + halfWidth;
            const int a = std::max(lo, 0), b = std::min(hi, n - 1);
            const float sum = tmp[b + 1] - tmp[a] + (float)(a - lo) * first + (float)(hi - b) * last;
            out[i] = sum / width;
        }
        src = out;
    }
}

struct PowerDb {
    float level, loudness;
};

/** 10 * log10 of the mean power of `db` under the level weight and under the loudness weight. */
PowerDb weightedPowerDb(const float* db, int n) noexcept {
    const double minHz = (double)CustomFilterShapes::kMinHz;
    const double ratio = std::pow((double)CustomFilterShapes::kMaxHz / minHz, 1.0 / (double)(n - 1));
    double hz = minHz, levelSum = 0.0, levelW = 0.0, loudSum = 0.0, loudW = 0.0;
    for (int i = 0; i < n; ++i) {
        const double power = std::pow(10.0, (double)db[i] / 10.0);
        const double l2 = (hz / kLowCutHz) * (hz / kLowCutHz);
        const double lowCut = l2 / (1.0 + l2);
        const double wl = std::pow(hz / minHz, kLevelSlope);
        const double h2 = (hz / kShelfHz) * (hz / kShelfHz);
        const double wk = lowCut * std::pow(hz / minHz, kLoudnessSlope) * (1.0 + kShelfPower * h2 / (1.0 + h2));
        levelSum += wl * power;
        levelW += wl;
        loudSum += wk * power;
        loudW += wk;
        hz *= ratio;
    }
    return {(float)(10.0 * std::log10(std::max(levelSum / levelW, 1.0e-30))),
            (float)(10.0 * std::log10(std::max(loudSum / loudW, 1.0e-30)))};
}
} // namespace

float wholeShapeFill(float resonance) noexcept {
    const float r = heldResonance(resonance);
    const float r3 = r * r * r;
    return kFillMax * r3 / (r3 + kFillKnee * kFillKnee * kFillKnee);
}

void applyWholeShapeResonance(float* db, int n, float resonance, float* scratch) noexcept {
    const float r = heldResonance(resonance);
    if (!(r > 0.0f) || n < 2)
        return;
    float* smoothed = scratch; // n
    float* tmp = scratch + n;  // n + 1
    const float octaves = std::log2(CustomFilterShapes::kMaxHz / CustomFilterShapes::kMinHz);
    const int halfWidth = std::max(1, (int)std::lround(kSmoothOctaves * (float)(n - 1) / octaves));
    smooth(db, smoothed, tmp, n, halfWidth);

    const auto before = weightedPowerDb(db, n);
    const float keep = 1.0f - wholeShapeFill(r);
    const float sharpen = r * r;
    for (int i = 0; i < n; ++i) {
        const float relief = db[i] - smoothed[i];
        const float gain = relief > 0.0f ? kPeakGain : kDipGain;
        db[i] = std::min(kMaxDb, keep * db[i] + sharpen * gain * relief);
    }
    const auto after = weightedPowerDb(db, n);
    const float excess = std::max(after.level - before.level, after.loudness - before.loudness);
    if (excess > 0.0f)
        for (int i = 0; i < n; ++i)
            db[i] -= excess;
}

} // namespace synth::customfilter
