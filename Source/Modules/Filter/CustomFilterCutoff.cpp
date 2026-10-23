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

float resonanceBellDb(float resonance, float octavesFromCutoff) noexcept {
    const float r = std::isfinite(resonance) ? juce::jlimit(0.0f, 1.0f, resonance) : 0.0f;
    const float sigma = 1.0f + (1.0f / 6.0f - 1.0f) * r;
    const float t = octavesFromCutoff / sigma;
    return kMaxResonancePeakDb * r * r * std::exp(-0.5f * t * t);
}

} // namespace synth::customfilter
