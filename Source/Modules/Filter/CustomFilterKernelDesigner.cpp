// CustomFilterKernelDesigner.cpp -- minimum-phase FIR design by the real-cepstrum method.
#include "CustomFilterKernelDesigner.h"
#include "CustomFilterCutoff.h"
#include "CustomFilterShapes.h"
#include "CustomFilterWholeShape.h"
#include <cmath>

namespace synth {

namespace {
constexpr int kTaperLength = 256;
constexpr float kMinDesignDb = -120.0f; // floor so log() never sees zero
constexpr float kMaxDesignDb = 30.0f;
} // namespace

/** Bin x positions are the only sample-rate-dependent part, so they are precomputed here and a
    shift becomes a constant x offset per design. The taper is the second half of a Hann window over
    the last kTaperLength taps: a minimum-phase kernel has almost no energy there, so this only
    removes the truncation step. */
void CustomFilterKernelDesigner::prepare(double sampleRate) {
    fft_ = std::make_unique<juce::dsp::FFT>(kFftOrder);
    work_.assign((size_t)kFftSize * 2, 0.0f);
    binX_.resize((size_t)kNumBins);
    const double logSpan = std::log((double)CustomFilterShapes::kMaxHz / (double)CustomFilterShapes::kMinHz);
    for (int k = 0; k < kNumBins; ++k) {
        const double hz = (double)k * sampleRate / (double)kFftSize;
        binX_[(size_t)k] = k == 0 ? -1.0f : (float)(std::log(hz / (double)CustomFilterShapes::kMinHz) / logSpan);
    }
    taper_.resize((size_t)kTaperLength);
    for (int i = 0; i < kTaperLength; ++i)
        taper_[(size_t)i] = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * (float)(i + 1) / kTaperLength));
    shapedDb_.assign((size_t)kGridSize, 0.0f);
    shapeScratch_.assign((size_t)customfilter::wholeShapeScratchSize(kGridSize), 0.0f);
}

/** Natural-log magnitude per bin into work_ (complex layout, imaginary parts zero). Outside the
    drawn 20 Hz..20 kHz range the curve holds its end values. A positive shift moves the response
    up: bin f reads the curve at f / 2^(shift/12). The resonant bell is added after Depth, in dB, centred on the
    cutoff's own frequency (the shift that Cutoff causes moves the curve, not the bell). */
void CustomFilterKernelDesigner::fillLogMagnitude(const float* gridDb, float shiftSemitones, float depth,
                                                  float resonance, float cutoffHz) noexcept {
    const float totalShift = shiftSemitones + customfilter::cutoffShiftSemitones(cutoffHz);
    const float dx =
        totalShift / 12.0f * std::log(2.0f) / std::log(CustomFilterShapes::kMaxHz / CustomFilterShapes::kMinHz);
    const bool bell = resonance > 0.0f;
    const float bellX = customfilter::cutoffX(cutoffHz);
    const float octavesPerX = customfilter::axisOctaves();
    constexpr float dbToNeper = 0.11512925464970229f; // ln(10) / 20
    for (int k = 0; k < kNumBins; ++k) {
        const float x = juce::jlimit(0.0f, 1.0f, binX_[(size_t)k] - dx);
        const float pos = x * (float)(kGridSize - 1);
        const int i0 = juce::jmin((int)pos, kGridSize - 2);
        const float frac = pos - (float)i0;
        const float db = gridDb[i0] + (gridDb[i0 + 1] - gridDb[i0]) * frac;
        float scaledDb = db * depth;
        if (bell)
            scaledDb += customfilter::resonanceBellDb(resonance, (binX_[(size_t)k] - bellX) * octavesPerX);
        const float scaled = juce::jlimit(kMinDesignDb, kMaxDesignDb, scaledDb);
        work_[(size_t)(2 * k)] = scaled * dbToNeper;
        work_[(size_t)(2 * k + 1)] = 0.0f;
    }
}

/** Whole shape: the curve is shifted and scaled onto the grid first (as customfilter::responseGridDb does, so the
    view and the kernel share one curve), given Whole-shape resonance there, then read per bin with no further shift.
    Bins past 20 kHz hold the grid's last value. No bell. */
void CustomFilterKernelDesigner::fillWholeShapeLogMagnitude(const float* gridDb, float shiftSemitones, float depth,
                                                            float resonance, float cutoffHz) noexcept {
    const float totalShift = shiftSemitones + customfilter::cutoffShiftSemitones(cutoffHz);
    const float dx =
        totalShift / 12.0f * std::log(2.0f) / std::log(CustomFilterShapes::kMaxHz / CustomFilterShapes::kMinHz);
    float* shaped = shapedDb_.data();
    for (int k = 0; k < kGridSize; ++k) {
        const float at = juce::jlimit(0.0f, 1.0f, (float)k / (float)(kGridSize - 1) - dx);
        const float pos = at * (float)(kGridSize - 1);
        const int i0 = juce::jmin((int)pos, kGridSize - 2);
        const float frac = pos - (float)i0;
        shaped[k] = (gridDb[i0] + (gridDb[i0 + 1] - gridDb[i0]) * frac) * depth;
    }
    customfilter::applyWholeShapeResonance(shaped, kGridSize, resonance, shapeScratch_.data());
    constexpr float dbToNeper = 0.11512925464970229f; // ln(10) / 20
    for (int k = 0; k < kNumBins; ++k) {
        const float x = juce::jlimit(0.0f, 1.0f, binX_[(size_t)k]);
        const float pos = x * (float)(kGridSize - 1);
        const int i0 = juce::jmin((int)pos, kGridSize - 2);
        const float frac = pos - (float)i0;
        const float db = shaped[i0] + (shaped[i0 + 1] - shaped[i0]) * frac;
        work_[(size_t)(2 * k)] = juce::jlimit(kMinDesignDb, kMaxDesignDb, db) * dbToNeper;
        work_[(size_t)(2 * k + 1)] = 0.0f;
    }
}

/** No resonance and the reference cutoff: the design before Cutoff and Resonance existed. */
void CustomFilterKernelDesigner::design(const float* gridDb, float shiftSemitones, float depth, float* out) noexcept {
    design(gridDb, shiftSemitones, depth, 0.0f, customfilter::kReferenceCutoffHz, out);
}

/** Peak mode: the bell on the shifted curve, read per bin (fillLogMagnitude). */
void CustomFilterKernelDesigner::design(const float* gridDb, float shiftSemitones, float depth, float resonance,
                                        float cutoffHz, float* out) noexcept {
    fillLogMagnitude(gridDb, shiftSemitones, depth, resonance, cutoffHz);
    transformToKernel(out);
}

void CustomFilterKernelDesigner::design(const float* gridDb, float shiftSemitones, float depth, float resonance,
                                        float cutoffHz, customfilter::ResonanceMode mode, float* out) noexcept {
    if (mode == customfilter::ResonanceMode::WholeShape)
        fillWholeShapeLogMagnitude(gridDb, shiftSemitones, depth, resonance, cutoffHz);
    else
        fillLogMagnitude(gridDb, shiftSemitones, depth, resonance, cutoffHz);
    transformToKernel(out);
}

/** Real cepstrum of log|H|, folded onto positive quefrencies (c[0] and c[N/2] once, 1..N/2-1
    doubled, the rest zero), back to the frequency domain, exponentiated: that is the
    minimum-phase spectrum with the same magnitude. Its inverse FFT's first L samples are the
    kernel. N = 4L keeps cepstral aliasing negligible for any curve the point model can draw. Three
    real FFTs of size N; juce::dsp::FFT's real-only transforms run in place on work_. */
void CustomFilterKernelDesigner::transformToKernel(float* out) noexcept {
    float* w = work_.data();
    fft_->performRealOnlyInverseTransform(w); // w[0..N) = real cepstrum

    for (int n = 1; n < kFftSize / 2; ++n)
        w[n] *= 2.0f;
    std::fill(w + kFftSize / 2 + 1, w + 2 * kFftSize, 0.0f);
    fft_->performRealOnlyForwardTransform(w, true); // bins 0..N/2: log of the min-phase spectrum

    for (int k = 0; k < kNumBins; ++k) {
        const float mag = std::exp(w[2 * k]);
        const float phase = w[2 * k + 1];
        w[2 * k] = mag * std::cos(phase);
        w[2 * k + 1] = mag * std::sin(phase);
    }
    fft_->performRealOnlyInverseTransform(w); // w[0..N) = minimum-phase impulse response

    std::copy(w, w + kKernelLength, out);
    for (int i = 0; i < kTaperLength; ++i)
        out[kKernelLength - kTaperLength + i] *= taper_[(size_t)i];
}

} // namespace synth
