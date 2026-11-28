// HumSynth.h -- a synthetic sung melody for the hum-to-melody tests: a harmonic voice on a phase-accumulated pitch with
// a scoop into the first note, glides between notes, vibrato, per-note detune, breath and room noise.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace humsynth {

struct Spec {
    std::vector<int> notes;
    double noteSec = 0.4;
    double gapSec = 0.0;  // silence after each note
    double leadSec = 0.0; // silence before the first note
    double vibCents = 20.0;
    double vibHz = 5.5;
    double detuneCents = 10.0; // standard deviation, clamped to +-45 cents so every note stays nearest its own pitch
    double breath = 0.0;
    double roomNoise = 0.0;
    double sampleRate = 22050.0;
    unsigned seed = 1234;
};

struct Truth {
    std::vector<double> startSec, endSec;
    std::vector<double> cents; // the pitch each note really sings, in MIDI numbers
};

// A standard normal from mt19937's raw output (Box-Muller). std::normal_distribution is implementation-defined, so
// libstdc++, libc++ and MSVC would each render a different hum from one seed and a test could pass on one platform
// only.
struct PortableGauss {
    explicit PortableGauss(std::uint32_t seed)
        : rng(seed) {}
    double operator()() {
        constexpr double kPi = 3.14159265358979323846;
        const double u1 = ((double)rng() + 1.0) / 4294967297.0;
        const double u2 = ((double)rng() + 1.0) / 4294967297.0;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
    }
    std::mt19937 rng;
};

inline std::vector<float> render(const Spec& spec, Truth* truth = nullptr) {
    constexpr double kPi = 3.14159265358979323846;
    PortableGauss gauss((std::uint32_t)spec.seed);
    const double sr = spec.sampleRate;
    const int count = (int)spec.notes.size();
    const double total = spec.leadSec + count * (spec.noteSec + spec.gapSec) + 0.1;
    const int n = (int)(total * sr);

    std::vector<double> pitch((size_t)count);
    for (int i = 0; i < count; ++i)
        pitch[(size_t)i] = spec.notes[(size_t)i] + std::clamp(gauss() * spec.detuneCents, -45.0, 45.0) / 100.0;
    if (truth != nullptr)
        *truth = {};

    std::vector<double> out((size_t)n, 0.0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = (double)i / sr;
        const double local = t - spec.leadSec;
        const int index = local < 0.0 ? -1 : (int)(local / (spec.noteSec + spec.gapSec));
        double amp = 0.0;
        double midi = index >= 0 && index < count ? pitch[(size_t)index] : 0.0;
        if (index >= 0 && index < count) {
            const double into = local - index * (spec.noteSec + spec.gapSec);
            if (into < spec.noteSec) {
                amp = std::min({1.0, into / 0.03, (spec.noteSec - into) / 0.03});
                if (index == 0 && into < 0.08)
                    midi = pitch[0] - 1.0 * (1.0 - into / 0.08);
                else if (index > 0 && spec.gapSec == 0.0 && into < 0.06)
                    midi = pitch[(size_t)index - 1] + (pitch[(size_t)index] - pitch[(size_t)index - 1]) * (into / 0.06);
            }
        }
        if (amp > 0.0) {
            const double fade = std::clamp((t - 0.3) / 0.3, 0.0, 1.0);
            midi += spec.vibCents / 100.0 * std::sin(2.0 * kPi * spec.vibHz * t) * fade;
            const double f0 = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
            phase += 2.0 * kPi * f0 / sr;
            double s = 0.0;
            for (int h = 1; h <= 15 && h * f0 < sr / 2.0; ++h)
                s += std::sin(h * phase) / std::pow((double)h, 1.6);
            out[(size_t)i] = amp * s + spec.breath * gauss() * amp * 0.3;
        }
    }
    double peak = 1.0e-9;
    for (auto& v : out) {
        v += spec.roomNoise * gauss();
        peak = std::max(peak, std::abs(v));
    }
    std::vector<float> result((size_t)n);
    for (int i = 0; i < n; ++i)
        result[(size_t)i] = (float)(out[(size_t)i] / peak * 0.7);
    if (truth != nullptr)
        for (int i = 0; i < count; ++i) {
            truth->startSec.push_back(spec.leadSec + i * (spec.noteSec + spec.gapSec));
            truth->endSec.push_back(truth->startSec.back() + spec.noteSec);
            truth->cents.push_back(pitch[(size_t)i]);
        }
    return result;
}

} // namespace humsynth
