// CustomFilterCutoff.cpp -- Cutoff as a shift and Resonance as a bell for the Custom filter.
#include "CustomFilterCutoff.h"
#include "CustomFilterShapes.h"
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

// The bell is the gain a resonant two-pole low-pass of quality Q has over one of Q 1: with x the frequency
// over the cutoff and u = x - 1/x, |gain|^2 = (u^2 + 1) / (u^2 + 1/Q^2). It peaks at Q exactly at the cutoff,
// is 1 far from it on both sides (symmetric in octaves) and its -3 dB width is about 1.44 / Q octaves: the
// widths the measured filter shows at each resonance (1.2, 0.50, 0.20, 0.077 octave at 0.25, 0.5, 0.75, 0.9).
float resonanceBellDb(float resonance, float octavesFromCutoff) noexcept {
    const float peakDb = resonancePeakDb(resonance);
    if (peakDb <= 0.0f || !std::isfinite(octavesFromCutoff))
        return 0.0f;
    const float invQ = std::pow(10.0f, -peakDb / 20.0f);
    const float u = 2.0f * std::sinh(juce::jlimit(-20.0f, 20.0f, octavesFromCutoff) * 0.69314718f);
    const float u2 = u * u;
    return 10.0f * std::log10((u2 + 1.0f) / (u2 + invQ * invQ));
}

} // namespace synth::customfilter
