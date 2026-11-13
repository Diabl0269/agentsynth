#pragma once

// The plucked-string voice behind PluckModule: an extended Karplus-Strong string. The design follows the
// classic Karplus-Strong algorithm and its Jaffe-Smith extensions (a loop lowpass for damping, a fractional
// delay for tuning, a dispersion allpass for stiffness, a comb on the excitation for the pick position),
// written from the textbook description. Header-only, allocation-free after prepare(), driven one sample at
// a time. Nothing here reads a parameter: the module hands over plain numbers when a note starts.

#include "Modules/DrumKit/DrumVoices.h"
#include "Modules/InstrumentVoiceUtil.h"
#include <array>
#include <vector>

namespace synth::pluck {

using synth::drums::kDecayLn;
using synth::drums::kTwoPi;
using synth::drums::NoiseSource;

inline constexpr int kNumVoices = 8;
/** Lowest pitch a string is tuned to; the delay lines are sized for it. */
inline constexpr double kMinHz = 20.0;
/** Allpass sections in the stiffness dispersion chain (none at all when stiffness is 0). */
inline constexpr int kDispersionStages = 4;

/** Phase delay, in samples, of H(z) = (b0 + b1 z^-1) / (1 + a1 z^-1) at angular frequency `w`. */
inline double phaseDelay(double b0, double b1, double a1, double w) noexcept {
    const double c = std::cos(w);
    const double s = std::sin(w);
    const double phase = std::atan2(-b1 * s, b0 + b1 * c) - std::atan2(-a1 * s, 1.0 + a1 * c);
    return -phase / w;
}

/** Gain of the same filter at `w`. */
inline double magnitude(double b0, double b1, double a1, double w) noexcept {
    const double c = std::cos(w);
    const double s = std::sin(w);
    return std::hypot(b0 + b1 * c, b1 * s) / std::hypot(1.0 + a1 * c, a1 * s);
}

/** First-order allpass (a + z^-1) / (1 + a z^-1). */
struct Allpass {
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process(float x) noexcept {
        const float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

/** Coefficient of the allpass whose phase delay at `w` is `delay` samples (0.5 to 1.5 is well behaved). */
inline double allpassForDelay(double delay, double w) noexcept {
    double target = delay;
    double a = (1.0 - target) / (1.0 + target);
    for (int i = 0; i < 4; ++i) { // the low-frequency formula is close; nudge it onto the exact delay at w
        target += delay - phaseDelay(a, 1.0, a, w);
        a = (1.0 - target) / (1.0 + target);
    }
    return a;
}

/** One-pole lowpass y = b x + (1 - b) y1 whose gain at `w` is `h` (0 < h <= 1); returns b. */
inline double lowpassForGain(double h, double w) noexcept {
    const double eps = 1.0 - h * h;
    if (eps < 1.0e-12)
        return 1.0;
    const double m = 1.0 - h * h * std::cos(w);
    const double q = (m - std::sqrt(std::max(0.0, m * m - eps * eps))) / eps;
    return 1.0 - std::clamp(q, 0.0, 0.985);
}

/** Everything a string needs to start, as plain numbers. */
struct Strike {
    double hz = 220.0;
    float velocity = 0.8f;
    float decay = 3.0f;      // seconds for the fundamental to fall 60 dB while held
    float brightness = 0.5f; // 0..1, already including the velocity effect
    float pickPosition = 0.13f;
    float hardness = 0.6f; // 0..1, already including the velocity effect
    float stiffness = 0.0f;
    double sampleRate = 44100.0;
};

struct PluckVoice {
    std::vector<float> ring;    // the string: `length` samples, read then overwritten in place
    std::vector<float> scratch; // excitation work area, same size as ring
    int length = 1;
    int pos = 0;
    Allpass tuning;
    std::array<Allpass, kDispersionStages> dispersion;
    int dispersionStages = 0;
    float lowpassB = 1.0f, lowpassY1 = 0.0f;
    float gain = 0.99f, gainTarget = 0.99f, gainSlew = 0.001f;
    double hz = 220.0;
    double lowpassGain = 1.0; // the loop lowpass's gain at the fundamental
    bool active = false, held = false;
    std::uint32_t serial = 0;
    int note = -1;
    synth::voices::PeakWatch watch;

    /** Size the delay lines for the lowest pitch at `sampleRate`. Message thread only. */
    void prepare(double sampleRate) {
        const auto size = static_cast<std::size_t>(sampleRate / kMinHz) + 8;
        ring.assign(size, 0.0f);
        scratch.assign(size, 0.0f);
        stop();
    }
    void stop() noexcept {
        active = held = false;
        std::fill(ring.begin(), ring.end(), 0.0f);
        tuning.reset();
        for (auto& d : dispersion)
            d.reset();
        lowpassY1 = 0.0f;
    }

    void trigger(const Strike& s, NoiseSource& noise, int midiNote, std::uint32_t order) noexcept;
    void release(float releaseSeconds, double sampleRate) noexcept;
    float tick() noexcept;

private:
    void excite(const Strike& s, NoiseSource& noise) noexcept;
};

inline void PluckVoice::trigger(const Strike& s, NoiseSource& noise, int midiNote, std::uint32_t order) noexcept {
    const double fs = s.sampleRate;
    hz = std::clamp(s.hz, kMinHz, fs / 8.0);
    const double w = kTwoPi * hz / fs;
    const double period = fs / hz;

    // Loop loss at the fundamental, in nepers per round trip, from the requested T60.
    const double lambda = kDecayLn / (std::max(s.decay, 0.01f) * hz);
    // A dark string takes most of that loss in the lowpass (which also damps the highs); a bright one in
    // the broadband gain.
    const double kappa = 0.1 + 0.8 * (1.0 - std::clamp(s.brightness, 0.0f, 1.0f));
    const double b = lowpassForGain(std::exp(-kappa * lambda), w);
    lowpassB = static_cast<float>(b);
    lowpassGain = magnitude(b, 0.0, -(1.0 - b), w);

    const double dispersionA = -0.45 * std::clamp(s.stiffness, 0.0f, 1.0f); // negative: highs run sharp
    dispersionStages = s.stiffness > 0.0f ? kDispersionStages : 0;
    for (auto& d : dispersion)
        d.a = static_cast<float>(dispersionA);

    // Total loop delay must equal one period AT the fundamental: take the filters' exact phase delay off it.
    const double lowpassDelay = phaseDelay(b, 0.0, -(1.0 - b), w);
    const double dispersionDelay = dispersionStages * phaseDelay(dispersionA, 1.0, dispersionA, w);
    const double target = period - lowpassDelay - dispersionDelay;
    length = std::clamp(static_cast<int>(std::floor(target - 0.5)), 2, static_cast<int>(ring.size()));
    // Out of reach (a very high, very stiff string): the pitch gives way before the allpass can go unstable.
    tuning.a = static_cast<float>(allpassForDelay(std::clamp(target - length, 0.5, 1.5), w));

    gain = gainTarget = static_cast<float>(std::min(std::exp(-lambda) / lowpassGain, 0.9999));
    gainSlew = static_cast<float>(1.0 - std::exp(-1.0 / (0.003 * fs)));

    tuning.reset();
    for (auto& d : dispersion)
        d.reset();
    lowpassY1 = 0.0f;
    pos = 0;
    excite(s, noise);
    watch.reset(length);
    note = midiNote;
    serial = order;
    held = true;
    active = true;
}

// A noise burst, softened by the pick hardness and combed by the pick position, fills the string.
inline void PluckVoice::excite(const Strike& s, NoiseSource& noise) noexcept {
    const double fs = s.sampleRate;
    const double cutoff = 400.0 * std::pow(30.0, std::clamp(s.hardness, 0.0f, 1.0f));
    const auto p = static_cast<float>(std::exp(-kTwoPi * std::min(cutoff, fs * 0.45) / fs));
    float state = 0.0f;
    for (int i = 0; i < length; ++i) {
        state = (1.0f - p) * noise.next() + p * state;
        scratch[static_cast<std::size_t>(i)] = state;
    }
    const int comb = std::clamp(static_cast<int>(std::lround(s.pickPosition * length)), 1, length - 1);
    float peak = 1.0e-9f;
    for (int i = 0; i < length; ++i) {
        const int j = i >= comb ? i - comb : i - comb + length;
        const float v = scratch[static_cast<std::size_t>(i)] - scratch[static_cast<std::size_t>(j)];
        ring[static_cast<std::size_t>(i)] = v;
        peak = std::max(peak, std::fabs(v));
    }
    const float scale = s.velocity / peak;
    for (int i = 0; i < length; ++i)
        ring[static_cast<std::size_t>(i)] *= scale;
}

inline void PluckVoice::release(float releaseSeconds, double sampleRate) noexcept {
    held = false;
    const double rate = kDecayLn / (std::max(releaseSeconds, 0.005f) * hz);
    const double wanted = std::exp(-rate) / lowpassGain;
    gainTarget = static_cast<float>(std::min(wanted, static_cast<double>(gain)));
    gainSlew = static_cast<float>(1.0 - std::exp(-1.0 / (0.002 * sampleRate)));
}

inline float PluckVoice::tick() noexcept {
    const auto i = static_cast<std::size_t>(pos);
    const float out = ring[i];
    float v = tuning.process(out);
    lowpassY1 = lowpassB * v + (1.0f - lowpassB) * lowpassY1;
    v = lowpassY1;
    for (int k = 0; k < dispersionStages; ++k)
        v = dispersion[static_cast<std::size_t>(k)].process(v);
    gain += (gainTarget - gain) * gainSlew;
    v *= gain;
    ring[i] = std::fabs(v) < 1.0e-20f ? 0.0f : v;
    if (++pos >= length)
        pos = 0;
    if (watch.quietAfter(out))
        active = false;
    return out;
}

} // namespace synth::pluck
