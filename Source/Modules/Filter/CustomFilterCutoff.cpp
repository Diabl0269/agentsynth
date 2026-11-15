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

// The peak law is fitted to a measured analogue-style resonant low-pass (12 and 24 dB per octave alike) at a
// 1 kHz cutoff: its peak over the pass band reads 2.0, 4.9, 9.9, 17.1 and 25.5 dB at resonance 0.1, 0.25,
// 0.5, 0.75 and 0.9, and this law stays within 0.8 dB of each. That filter self-oscillates at 1 (about
// 44 dB, 0.013 octave wide); a 2048-tap kernel cannot hold a ring that narrow, so the law ends at
// kMaxResonancePeakDb, about what the same synth's morphing 12 dB type reaches at 1 (33.6 dB). Even so the kernel
// falls about 1.5 dB short of that top at a 1 kHz cutoff (more below), where the ring outlasts it. See
// Tests/Modules/Filter/CustomFilterResonanceLawTests.cpp.
namespace {
constexpr float kPeakLinearDb = 3.1371f;   // dB per unit of resonance
constexpr float kPeakPoleWeight = 1.7487f; // how much of the 1 / (1 - c r) rise the peak takes
constexpr float kPeakPole = 0.8505f;       // c: how close resonance 1 comes to self-oscillation

float heldResonance(float resonance) noexcept {
    return std::isfinite(resonance) ? juce::jlimit(0.0f, 1.0f, resonance) : 0.0f;
}
} // namespace

float resonancePeakDb(float resonance) noexcept {
    const float r = heldResonance(resonance);
    return kPeakLinearDb * r - kPeakPoleWeight * 20.0f * std::log10(1.0f - kPeakPole * r);
}

// The bell is a broad hill with a needle on it, as a measured analogue-style resonant low-pass shows (cutoff 1 kHz,
// the pass band kept flat): its excess over resonance 0 is +3.8, +4.2, +4.4 dB one octave from the cutoff at 0.25,
// 0.5 and 0.75, and about +0.9 dB two octaves out, not the 1 to 1.6 and 0.3 dB a lone narrow bell gives.
//   hill:   S * exp(-|o / 1.9|^6), flat out to about an octave and gone by three; S is the peak height soft-
//           capped at 3.25 dB (the p-norm 8 of 1 / peak and 1 / 3.25), so it is never above the peak itself.
//   needle: the two-pole shape below, sharpened (height m * peak with m = 2.75 - 2.25 r, at least 1) and scaled
//           down to peak - S, so the total at the cutoff is exactly resonancePeakDb and the -3 dB width keeps the
//           measured 0.50 / 0.20 octave at 0.5 / 0.75 (1.2 at 0.25 is set by that filter's own skirt).
// The constants were fitted offline to those numbers; the table is in
// Tests/Modules/Filter/CustomFilterResonanceLawTests.cpp. Resonance 0 is exactly 0 and the shape is symmetric.
// The needle: the gain a resonant two-pole low-pass of quality Q has over one of Q 1: with x the frequency over
// the cutoff and u = x - 1/x, |gain|^2 = (u^2 + 1) / (u^2 + 1/Q^2). It peaks at Q exactly at the cutoff, is 1 far
// from it on both sides (symmetric in octaves) and its -3 dB width is about 1.44 / Q octaves.
namespace {
constexpr float kSkirtMaxDb = 3.25f;       // the hill's height ceiling
constexpr float kSkirtHalfWidth = 1.9f;    // octaves where the hill is 1/e
constexpr float kSkirtSquareness = 6.0f;   // how flat its top is and how fast it falls
constexpr float kSkirtCapNorm = 8.0f;      // how hard the hill is capped at its ceiling
constexpr float kNeedleSharpBase = 2.75f;  // the needle is shaped as a resonance m times the peak ...
constexpr float kNeedleSharpSlope = 2.25f; // ... with m = base - slope * r, at least 1

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
