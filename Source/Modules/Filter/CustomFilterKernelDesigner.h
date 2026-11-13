// CustomFilterKernelDesigner.h -- turns one magnitude curve (dB on a log-frequency grid) into a
// minimum-phase FIR kernel via the real cepstrum. Its own class so it can move to a worker thread
// later; every buffer and FFT is allocated in prepare(), so design() is real-time safe.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace synth {

class CustomFilterKernelDesigner {
public:
    static constexpr int kKernelLength = 2048;        // L, taps
    static constexpr int kFftOrder = 13;              // FFT size 4L
    static constexpr int kFftSize = 1 << kFftOrder;   // 8192
    static constexpr int kNumBins = kFftSize / 2 + 1; // 0..Nyquist
    static constexpr int kGridSize = 1024;            // log-frequency grid points, 20 Hz..20 kHz

    /** Allocates every buffer and the FFT. Not real-time safe; call before design(). */
    void prepare(double sampleRate);
    bool isPrepared() const noexcept { return fft_ != nullptr; }

    /** `gridDb`: kGridSize dB values at CustomFilterShapes x = i / (kGridSize - 1). `shiftSemitones`
     *  moves the curve along frequency, `depth` scales its dB. Writes kKernelLength taps to `out`.
     *  No allocation, locks or system calls. Precondition: isPrepared(). */
    void design(const float* gridDb, float shiftSemitones, float depth, float* out) noexcept;
    /** As above with the Custom filter's Cutoff and Resonance: the response also moves by
     *  customfilter::cutoffShiftSemitones(cutoffHz), and after Depth a resonant bell
     *  (customfilter::resonanceBellDb) is added in dB at `cutoffHz`. */
    void design(const float* gridDb, float shiftSemitones, float depth, float resonance, float cutoffHz,
                float* out) noexcept;

private:
    void fillLogMagnitude(const float* gridDb, float shiftSemitones, float depth, float resonance,
                          float cutoffHz) noexcept;

    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float> binX_;  // kNumBins: each bin's curve x before shift (may lie outside [0, 1])
    std::vector<float> work_;  // 2 * kFftSize floats: JUCE real-only FFT layout
    std::vector<float> taper_; // fade-out applied to the last taps
};

} // namespace synth
