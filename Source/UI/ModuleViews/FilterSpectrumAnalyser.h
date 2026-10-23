#pragma once

// FilterSpectrumAnalyser.h -- the live spectrum the Filter's views draw: the module's visual buffer as smoothed dB at
// 1024 log-spaced points from 20 Hz to 20 kHz (the same points FrequencyGrid::indexToFreq gives), so a view maps point
// i to x = i / (n - 1) of its own log-frequency axis. The Filter card's response view and the shape window share it.
#include "Modules/VisualBuffer.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace synth::ui {

/** One analyser per view: it owns the FFT and the smoothing state. Message thread only. */
class FilterSpectrumAnalyser {
public:
    static constexpr int kNumPoints = 1024;
    static constexpr int kFftOrder = 10; // 1024-point FFT
    static constexpr int kFftSize = 1 << kFftOrder;
    /** The dB range a view maps to its full height (below kMinDb is the floor). */
    static constexpr float kMinDb = -80.0f;
    static constexpr float kMaxDb = 0.0f;
    /** The share of the distance to a new reading each analyse() moves (an exponential moving average). */
    static constexpr float kSmoothing = 0.3f;

    explicit FilterSpectrumAnalyser(float smoothing = kSmoothing);

    /** Windows and transforms `numSamples` samples (zero-padded or cut to the FFT size) recorded at `sampleRate`, and
     *  moves every point toward its new reading. */
    void analyse(const float* samples, int numSamples, double sampleRate);
    /** Copies the whole of `buffer` and analyses it. */
    void analyse(const VisualBuffer& buffer, double sampleRate);

    /** kNumPoints values in kMinDb..kMaxDb, the floor until something is analysed. */
    const std::vector<float>& getDb() const noexcept { return db_; }
    /** Back to the floor. */
    void reset();

private:
    float smoothing_;
    juce::dsp::FFT fft_{kFftOrder};
    juce::dsp::WindowingFunction<float> window_{kFftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData_;
    std::vector<float> db_;
};

} // namespace synth::ui
