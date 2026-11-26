// CustomFilterCutoff.h -- what Cutoff and Resonance mean to the Custom filter, as pure functions: the
// kernel designer and the card's response view both call them, so the curve drawn is the curve designed.
#pragma once

namespace synth::customfilter {

/** The Cutoff at which the drawn response is not moved (Cutoff's default). */
constexpr float kReferenceCutoffHz = 440.0f;
/** Highest peak resonance adds, in dB, from Resonance 0.65 up. */
constexpr float kMaxResonancePeakDb = 32.0f;

/** Semitones the drawn response moves for `cutoffHz`: 12 * log2(cutoffHz / 440), with `cutoffHz` held to
 *  20 Hz..20 kHz (a non-finite value reads as the reference, so 0). Added to Shift and Key Track. */
float cutoffShiftSemitones(float cutoffHz) noexcept;

/** The curve x (0 at 20 Hz, 1 at 20 kHz, log axis) of `cutoffHz`, held to that range. */
float cutoffX(float cutoffHz) noexcept;

/** The resonant peak, in dB, `octavesFromCutoff` octaves from the cutoff: 0 everywhere at resonance 0, rising
 *  to resonancePeakDb(resonance) at the cutoff (a narrowing needle on a broad hill, ~4 dB an octave out). `resonance`
 * is held to 0..1. Added after Depth. */
float resonanceBellDb(float resonance, float octavesFromCutoff) noexcept;

/** The height of that peak, in dB: nothing up to 0.30, about 1.2 dB per percent from there (17.6 dB at 0.5), reaching
 *  kMaxResonancePeakDb at 0.65 and staying there to 1, as the measured reference does. `resonance` is held to
 *  0..1. */
float resonancePeakDb(float resonance) noexcept;

/** The octave span of the curve axis, log2(20 kHz / 20 Hz). */
float axisOctaves() noexcept;

} // namespace synth::customfilter
