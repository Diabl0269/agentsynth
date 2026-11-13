// SyncMeasure.h -- how the oscillator-sync tests measure a synced note: the share of its energy that is not at a
// multiple of the master's pitch. Header-only, shared by the Oscillator and Wavetable sync tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace syncmeasure {

inline double acEnergy(const std::vector<float>& x) {
    double total = 0.0, mean = 0.0;
    for (float s : x)
        mean += (double)s / (double)x.size();
    for (float s : x)
        total += ((double)s - mean) * ((double)s - mean);
    return total;
}

inline float peakOf(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x)
        p = std::max(p, std::abs(v));
    return p;
}

// The share of the energy that is not at a multiple of the master's pitch, in dB. A synced wave repeats exactly
// once per master cycle, so everything else is what the ear hears as grit: restarts landing unevenly, random phases
// or folded-back overtones. The window is one second, so with a whole number of Hz each harmonic falls on its own bin.
// The result is relative to `reference`, the energy of the same note left free, or to the wave's own energy if that is
// larger: a cycle that happens to sit near a slow wave's zero is quiet, and its small remainder is not louder grit.
inline double offHarmonicDb(const std::vector<float>& x, int masterHz, double reference, double sampleRate) {
    constexpr double kTwoPi = 6.283185307179586;
    const double total = acEnergy(x);
    double mean = 0.0;
    for (float s : x)
        mean += (double)s / (double)x.size();
    double harmonic = 0.0;
    const double n = (double)x.size();
    for (int h = masterHz; h < (int)(sampleRate / 2.0); h += masterHz) {
        const double w = 2.0 * std::cos(kTwoPi * (double)h / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (float s : x) {
            const double s0 = (double)s - mean + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        harmonic += (s1 * s1 + s2 * s2 - w * s1 * s2) * 2.0 / n;
    }
    return 10.0 *
           std::log10(std::max(1e-12, (total - std::min(total, harmonic)) / std::max({1e-12, total, reference})));
}

} // namespace syncmeasure
