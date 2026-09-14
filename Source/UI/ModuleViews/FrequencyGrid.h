#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace synth::ui {

/** Log-frequency / dB coordinate mapping shared by every frequency-domain visualiser.
 *
 *  Both FrequencyResponseComponent (Filter) and EQCurveComponent (Parametric EQ) plot a
 *  magnitude curve over the same 20 Hz - 20 kHz log axis, so the mapping, the sampling of
 *  frequency points, and the "100Hz / 1kHz / 10kHz" label formatting live here once.
 *
 *  The dB axis is NOT fixed: the filter view uses an asymmetric -40..+50 dB window (resonance
 *  peaks overshoot a long way) while the EQ view uses a symmetric +/-30 dB window, so
 *  minDb/maxDb are passed in per call rather than baked in as constants.
 *
 *  Everything here is a pure function — unit-testable without constructing a Component.
 */
struct FrequencyGrid {
    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;

    /** Frequency (Hz) -> x pixel across a log-scaled view of width `width`. */
    static float freqToX(float freq, float width) noexcept {
        return (std::log(freq / kMinFreq) / std::log(kMaxFreq / kMinFreq)) * width;
    }

    /** x pixel -> frequency (Hz). Inverse of freqToX. Returns kMinFreq when width <= 0. */
    static float xToFreq(float x, float width) noexcept {
        if (width <= 0.0f)
            return kMinFreq;
        return kMinFreq * std::pow(kMaxFreq / kMinFreq, x / width);
    }

    /** Frequency of sample point `index` of `numPoints` log-spaced points spanning the axis. */
    static float indexToFreq(int index, int numPoints) noexcept {
        if (numPoints <= 1)
            return kMinFreq;
        float t = static_cast<float>(index) / static_cast<float>(numPoints - 1);
        return kMinFreq * std::pow(kMaxFreq / kMinFreq, t);
    }

    /** dB -> y pixel in a view of height `height`; y=0 is maxDb (top), y=height is minDb. */
    static float dbToY(float db, float height, float minDb, float maxDb) noexcept {
        return ((db - maxDb) / (minDb - maxDb)) * height;
    }

    /** y pixel -> dB. Inverse of dbToY. Returns maxDb when height <= 0. */
    static float yToDb(float y, float height, float minDb, float maxDb) noexcept {
        if (height <= 0.0f)
            return maxDb;
        return maxDb + (y / height) * (minDb - maxDb);
    }

    /** Human-readable frequency label.
     *  100 -> "100Hz", 1000 -> "1kHz", 10000 -> "10kHz", 1500 -> "1.5kHz".
     *  Values >= 1000 render as kHz (1 decimal unless whole); below that as integer Hz.
     */
    static juce::String formatHzLabel(float hz) {
        if (hz >= 1000.0f) {
            float kHz = hz / 1000.0f;
            // Suppress the decimal when it's a whole number
            if (std::fmod(kHz, 1.0f) < 0.05f)
                return juce::String((int)std::round(kHz)) + "kHz";
            return juce::String(kHz, 1) + "kHz";
        }
        return juce::String((int)std::round(hz)) + "Hz";
    }

    /** Index of the maximum value in mags[0..numBins-1], or -1 if numBins <= 0 / mags is null. */
    static int findPeakBin(const float* mags, int numBins) noexcept {
        if (mags == nullptr || numBins <= 0)
            return -1;
        int peak = 0;
        for (int i = 1; i < numBins; ++i)
            if (mags[i] > mags[peak])
                peak = i;
        return peak;
    }
};

} // namespace synth::ui
