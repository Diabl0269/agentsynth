// CustomFilterDriveHelpers.h -- shared helpers for the Filter Drive tests: steady tones through a mono
// FilterModule, and harmonic measurement by direct DFT over whole periods.
#pragma once

#include "Modules/FilterModule.h"
#include <cmath>
#include <vector>

namespace synth::test::drive {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kChannels = 35;

/** Sets a parameter of `m` to a plain value by id. */
inline void setParam(FilterModule& m, const juce::String& id, float plain) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id) {
            r->setValueNotifyingHost(r->convertTo0to1(plain));
            return;
        }
    jassertfalse;
}

/** `peak` * a sine at `hz`. */
inline std::vector<float> sineTone(double hz, float peak, int n) {
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = peak * (float)std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kSampleRate);
    return x;
}

/** A band-limited (additive, up to 20 kHz) saw at `hz`, scaled so its peak is `peak`. */
inline std::vector<float> sawTone(double hz, float peak, int n) {
    std::vector<double> y((size_t)n, 0.0);
    for (int k = 1; k * hz < 20000.0; ++k)
        for (int i = 0; i < n; ++i)
            y[(size_t)i] +=
                ((k & 1) ? 1.0 : -1.0) * std::sin(2.0 * juce::MathConstants<double>::pi * k * hz * i / kSampleRate) / k;
    double pk = 0.0;
    for (double v : y)
        pk = std::max(pk, std::abs(v));
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = (float)(peak * y[(size_t)i] / pk);
    return x;
}

/** Runs `in` through ch0 of `m` (already configured; prepared here) and returns ch0. `beforeBlock(b)` runs first. */
template <typename Fn>
std::vector<float> run(FilterModule& m, const std::vector<float>& in, Fn&& beforeBlock) {
    m.prepareToPlay(kSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out(in.size());
    for (size_t start = 0, b = 0; start < in.size(); start += kBlock, ++b) {
        const int n = (int)std::min<size_t>(kBlock, in.size() - start);
        buf.clear();
        std::copy_n(in.data() + start, n, buf.getWritePointer(0));
        beforeBlock((int)b, buf);
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), kChannels, n);
        m.processBlock(view, midi);
        std::copy_n(buf.getReadPointer(0), n, out.data() + start);
    }
    return out;
}

inline std::vector<float> run(FilterModule& m, const std::vector<float>& in) {
    return run(m, in, [](int, juce::AudioBuffer<float>&) {});
}

/** Amplitude of harmonic k (1-based) of `hz` over the last `n` samples of `x` (n a whole number of periods). */
inline double harmonic(const std::vector<float>& x, double hz, int k, int n) {
    double re = 0.0, im = 0.0;
    const size_t off = x.size() - (size_t)n;
    const double w = 2.0 * juce::MathConstants<double>::pi * hz * k / kSampleRate;
    for (int i = 0; i < n; ++i) {
        re += x[off + (size_t)i] * std::cos(w * i);
        im -= x[off + (size_t)i] * std::sin(w * i);
    }
    return 2.0 * std::sqrt(re * re + im * im) / n;
}

inline double rms(const std::vector<float>& x, int n) {
    double s = 0.0;
    for (size_t i = x.size() - (size_t)n; i < x.size(); ++i)
        s += (double)x[i] * x[i];
    return std::sqrt(s / n);
}

inline double db(double v) { return 20.0 * std::log10(std::max(v, 1.0e-12)); }

/** Total harmonic distortion of a tone at `hz`: harmonics 2..`maxHarmonic` against the fundamental. */
inline double thd(const std::vector<float>& x, double hz, int n, int maxHarmonic = 40) {
    double s = 0.0;
    for (int k = 2; k <= maxHarmonic && k * hz < 20000.0; ++k)
        s += std::pow(harmonic(x, hz, k, n), 2.0);
    return std::sqrt(s) / harmonic(x, hz, 1, n);
}

} // namespace synth::test::drive
