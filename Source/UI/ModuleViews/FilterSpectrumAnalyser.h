#pragma once

// FilterSpectrumAnalyser.h -- the live spectrum the Filter's views draw: the module's spectrum buffer as dB at 1024
// log-spaced points from 20 Hz to 20 kHz (the same points FrequencyGrid::indexToFreq gives), so a view maps point i to
// x = i / (n - 1) of its own log-frequency axis. The Filter card's response view and the shape window share it.
#include "Modules/VisualBuffer.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace synth::ui {

/** One analyser per view: it owns the FFT and the ballistics state. Message thread only. */
class FilterSpectrumAnalyser {
public:
    static constexpr int kNumPoints = 1024;
    static constexpr int kFftOrder = 12; // 4096-point FFT: about 11 Hz bins at 48 kHz
    static constexpr int kFftSize = 1 << kFftOrder;
    /** The dB range a view maps to its full height (below kMinDb is the floor). */
    static constexpr float kMinDb = -80.0f;
    static constexpr float kMaxDb = 0.0f;
    /** Ballistics, in seconds of real time between readings: a rising point closes 63 % of the gap in kAttackSeconds,
     *  a falling one in kReleaseSeconds (on the magnitude, so a silenced point falls at a steady dB rate). */
    static constexpr double kAttackSeconds = 0.010;
    static constexpr double kReleaseSeconds = 0.145;
    /** The longest gap one reading may account for (a stalled timer does not drop the trace to the floor). */
    static constexpr double kMaxStepSeconds = 0.25;

    FilterSpectrumAnalyser();

    /** Windows and transforms the last kFftSize of `numSamples` samples (zero-padded when fewer) recorded at
     *  `sampleRate`, and moves every point toward its new reading as far as `elapsedSeconds` of the ballistics allow.
     */
    void analyse(const float* samples, int numSamples, double sampleRate, double elapsedSeconds);
    /** Copies the whole of `buffer` and analyses it, timed by the real time since the previous call. */
    void analyse(const VisualBuffer& buffer, double sampleRate);

    /** kNumPoints values in kMinDb..kMaxDb, the floor until something is analysed. */
    const std::vector<float>& getDb() const noexcept { return db_; }
    /** Back to the floor. */
    void reset();

    /** Tests: the clock (seconds) the VisualBuffer overload times itself by; null restores the real one. */
    static void setClockForTest(double (*clock)());

private:
    void updateBinMap(double sampleRate);

    juce::dsp::FFT fft_{kFftOrder};
    juce::dsp::WindowingFunction<float> window_{kFftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData_;
    std::vector<float> scratch_;
    std::vector<float> magnitude_; // smoothed linear magnitude per point
    std::vector<float> db_;
    struct PointBins {
        int first = 0, last = -1; // bins whose centre falls in the point's span: their maximum is the reading
        int below = 0;            // otherwise: power-interpolated between bin `below` and the next
        float frac = 0.0f;
    };
    std::vector<PointBins> bins_;
    double binMapRate_ = 0.0;
    double lastTime_ = -1.0;
};

} // namespace synth::ui
