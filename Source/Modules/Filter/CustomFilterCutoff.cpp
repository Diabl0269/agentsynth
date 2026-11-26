// CustomFilterCutoff.cpp -- Cutoff as a shift and Resonance as a bell for the Custom filter.
#include "CustomFilterCutoff.h"
#include "CustomFilterShapes.h"
#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

namespace synth::customfilter {

namespace {
float heldCutoff(float cutoffHz) noexcept {
    return std::isfinite(cutoffHz) ? juce::jlimit(CustomFilterShapes::kMinHz, CustomFilterShapes::kMaxHz, cutoffHz)
                                   : kReferenceCutoffHz;
}
} // namespace

float axisOctaves() noexcept { return std::log2(CustomFilterShapes::kMaxHz / CustomFilterShapes::kMinHz); }

float cutoffShiftSemitones(float cutoffHz) noexcept {
    return 12.0f * std::log2(heldCutoff(cutoffHz) / kReferenceCutoffHz);
}

float cutoffX(float cutoffHz) noexcept {
    return std::log2(heldCutoff(cutoffHz) / CustomFilterShapes::kMinHz) / axisOctaves();
}

// The peak law is the measured resonance of a drawable pole-zero filter (the reference synth's PZ SVF) loaded with a
// plain low-pass shape, rendered offline as a saw's harmonic response (cutoff 1 kHz and 3 kHz agree within 0.2 dB up
// to 65%). Its Resonance does nothing below 30%, then the peak over the pass band rises about 1.2 dB per percent:
//   resonance  30%   35%   40%   45%   50%   55%   60%   65%   70%   75% and up
//   peak dB    0.1   1.4   5.5   11.5  17.6  23.3  28.3  32.4  35.8  38 to 41 (self-oscillation, not measurable)
// A 2048-tap kernel cannot hold a ring that narrow, so the law ends at kMaxResonancePeakDb (reached at 65%, where
// the reference is at 32.4 dB); from there the knob changes nothing, as the reference's own 75% to 100% do not. The
// numbers are the table in Tests/Modules/Filter/CustomFilterResonanceLawTests.cpp; the stored sweep is the sound-match
// table. The kernel falls about 1.5 dB short of the top at a 1 kHz cutoff, where the ring outlasts it.
namespace {
struct PeakKnot {
    float resonance, db;
};
constexpr PeakKnot kPeakKnots[] = {{0.00f, 0.0f},
                                   {0.30f, 0.1f},
                                   {0.35f, 1.4f},
                                   {0.40f, 5.5f},
                                   {0.45f, 11.5f},
                                   {0.50f, 17.6f},
                                   {0.55f, 23.3f},
                                   {0.60f, 28.3f},
                                   {0.65f, kMaxResonancePeakDb},
                                   {1.00f, kMaxResonancePeakDb}};
constexpr int kPeakKnotCount = (int)(sizeof(kPeakKnots) / sizeof(kPeakKnots[0]));

float heldResonance(float resonance) noexcept {
    return std::isfinite(resonance) ? juce::jlimit(0.0f, 1.0f, resonance) : 0.0f;
}

/** Slope at knot i that keeps the curve monotone (Fritsch-Carlson: the harmonic mean of the two chords, 0 where
 *  they differ in sign or one is flat), so the peak never dips between two knots and is continuous with a
 *  continuous slope. */
float knotSlope(int i) noexcept {
    const auto chord = [](int k) {
        return (kPeakKnots[k + 1].db - kPeakKnots[k].db) / (kPeakKnots[k + 1].resonance - kPeakKnots[k].resonance);
    };
    if (i == 0)
        return 0.0f;
    if (i == kPeakKnotCount - 1)
        return 0.0f;
    const float l = chord(i - 1), r = chord(i);
    if (!(l * r > 0.0f))
        return 0.0f;
    return 2.0f * l * r / (l + r);
}
} // namespace

float resonancePeakDb(float resonance) noexcept {
    const float r = heldResonance(resonance);
    int i = 0;
    while (i + 2 < kPeakKnotCount && r > kPeakKnots[i + 1].resonance)
        ++i;
    const auto& k0 = kPeakKnots[i];
    const auto& k1 = kPeakKnots[i + 1];
    const float h = k1.resonance - k0.resonance;
    const float t = (r - k0.resonance) / h;
    const float t2 = t * t, t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * k0.db + (t3 - 2.0f * t2 + t) * h * knotSlope(i) +
           (-2.0f * t3 + 3.0f * t2) * k1.db + (t3 - t2) * h * knotSlope(i + 1);
}

// The bell is a broad hill with a needle on it, as the measured PZ SVF shows (low-pass shape, peak at the shape's
// pole): from 45% up its skirts are the same at every Resonance, +15.4 dB a quarter octave from the peak, +9.5 at half
// an octave, +4.3 at one, +1.1 at two and +0.3 at three, and only the needle on top grows.
//   hill:   S * exp(-|o / 1.02|^1.43), with S the peak height capped at 9.6 dB (the p-norm 20 of 1 / peak and 1 / 9.6),
//           so it is never above the peak itself and below 45% it shrinks with the peak.
//   needle: the two-pole shape below, scaled down to peak - S so the total at the cutoff is exactly
//           resonancePeakDb; its sharpening m = 1.24 - 0.75 r is 1 from 0.31 up, so it is the plain two-pole.
// The constants were fitted offline to the measured rows (35% to 70%, cutoff 1 kHz and 3 kHz), 0.34 dB rms over
// 3 octaves either side; the table is in Tests/Modules/Filter/CustomFilterResonanceLawTests.cpp. Resonance 0 is
// exactly 0 and the shape is symmetric.
// The needle: the gain a resonant two-pole low-pass of quality Q has over one of Q 1: with x the frequency over
// the cutoff and u = x - 1/x, |gain|^2 = (u^2 + 1) / (u^2 + 1/Q^2). It peaks at Q exactly at the cutoff, is 1 far
// from it on both sides (symmetric in octaves) and its -3 dB width is about 1.44 / Q octaves.
namespace {
constexpr float kSkirtMaxDb = 9.5578f;       // the hill's height ceiling
constexpr float kSkirtHalfWidth = 1.0237f;   // octaves where the hill is 1/e
constexpr float kSkirtSquareness = 1.4297f;  // how flat its top is and how fast it falls
constexpr float kSkirtCapNorm = 20.0f;       // how hard the hill is capped at its ceiling
constexpr float kNeedleSharpBase = 1.2356f;  // the needle is shaped as a resonance m times the peak ...
constexpr float kNeedleSharpSlope = 0.7495f; // ... with m = base - slope * r, at least 1

float needleDb(float shapeDb, float octaves) noexcept {
    const float invQ = std::pow(10.0f, -shapeDb / 20.0f);
    const float u = 2.0f * std::sinh(juce::jlimit(-20.0f, 20.0f, octaves) * 0.69314718f);
    const float u2 = u * u;
    return 10.0f * std::log10((u2 + 1.0f) / (u2 + invQ * invQ));
}
} // namespace

float resonanceBellDb(float resonance, float octavesFromCutoff) noexcept {
    const float peakDb = resonancePeakDb(resonance);
    if (peakDb <= 0.0f || !std::isfinite(octavesFromCutoff))
        return 0.0f;
    const float skirtDb =
        std::pow(std::pow(peakDb, -kSkirtCapNorm) + std::pow(kSkirtMaxDb, -kSkirtCapNorm), -1.0f / kSkirtCapNorm);
    const float hill = skirtDb * std::exp(-std::pow(std::abs(octavesFromCutoff / kSkirtHalfWidth), kSkirtSquareness));
    const float sharp = std::max(1.0f, kNeedleSharpBase - kNeedleSharpSlope * heldResonance(resonance));
    const float shapeDb = sharp * peakDb;
    return hill + needleDb(shapeDb, octavesFromCutoff) * (peakDb - skirtDb) / shapeDb;
}

} // namespace synth::customfilter
