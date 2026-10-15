#pragma once

// The struck-bar voice behind MalletModule: modal synthesis. A bar (or bell) is a handful of sine-like modes
// at fixed frequency ratios; each mode is a two-pole resonator that rings and decays on its own, and the
// mallet is a short raised-cosine pulse that excites them all at once. The design follows the classic modal
// synthesis literature, written from the textbook description. Header-only, allocation-free, driven one
// sample at a time. Nothing here reads a parameter: the module hands over plain numbers when a note starts.

#include "Modules/DrumKit/DrumVoices.h"
#include "Modules/InstrumentVoiceUtil.h"
#include <array>

namespace synth::mallet {

using synth::drums::kDecayLn;
using synth::drums::kTwoPi;
using synth::drums::NoiseSource;

inline constexpr int kNumVoices = 8;
inline constexpr int kMaxModes = 8;

/** Partial frequency ratios and relative gains of one kind of bar. `nominal` is the mode that sets the note's
    pitch (ratio 1) and that the tube resonator boosts. */
struct MaterialSpec {
    int count;
    int nominal;
    std::array<double, kMaxModes> ratio;
    std::array<double, kMaxModes> gain;
};

inline const MaterialSpec& materialSpec(int index) noexcept {
    static const std::array<MaterialSpec, 5> specs = {{
        {4, 0, {1.0, 3.9, 9.2, 16.0}, {1.0, 0.45, 0.2, 0.08}},                            // Marimba
        {3, 0, {1.0, 4.0, 10.0}, {1.0, 0.3, 0.12}},                                       // Vibraphone
        {4, 0, {1.0, 3.0, 6.0, 9.8}, {1.0, 0.6, 0.4, 0.25}},                              // Xylophone
        {4, 0, {1.0, 2.76, 5.40, 8.93}, {1.0, 0.6, 0.4, 0.3}},                            // Glockenspiel
        {7, 1, {0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 3.0}, {0.5, 1.0, 0.7, 0.6, 0.5, 0.4, 0.3}}, // Bell
    }};
    return specs[static_cast<std::size_t>(std::clamp(index, 0, 4))];
}

/** Everything a bar needs to start, as plain numbers. */
struct Strike {
    int material = 0;
    double hz = 261.6;
    float velocity = 0.8f;
    float decay = 1.2f;      // seconds for the nominal mode to fall 60 dB
    float damping = 0.5f;    // 0..1, how much faster the upper modes die
    float brightness = 0.5f; // 0..1, already including the velocity effect
    float hardness = 0.5f;   // 0..1, already including the velocity effect
    float resonator = 0.5f;
    double sampleRate = 44100.0;
};

/** T60 of a mode: decay / (1 + damping * 0.6 * max(0, ratio - 1)). Ratio 1 (and below) keeps the full decay. */
inline double modeT60(double decay, double damping, double ratio) noexcept {
    return decay / (1.0 + damping * 0.6 * std::max(0.0, ratio - 1.0));
}

struct MalletVoice {
    struct Mode {
        double a1 = 0, a2 = 0, y1 = 0, y2 = 0, in = 0;
    };
    std::array<Mode, kMaxModes> modes;
    int numModes = 0;
    int pulseLen = 0, pulseIdx = 0;
    double pulseCos = 1.0, pulseCosPrev = 1.0, pulseStep = 0.0; // recurrence for the raised cosine
    float clickEnv = 0.0f, clickCoef = 0.0f, clickAmount = 0.0f;
    float amp = 1.0f, norm = 1.0f;
    float relEnv = 1.0f, relCoef = 1.0f;
    bool active = false, held = false;
    std::uint32_t serial = 0;
    int note = -1;
    synth::voices::PeakWatch watch;

    void stop() noexcept {
        active = held = false;
        for (auto& m : modes)
            m = Mode{};
    }
    void trigger(const Strike& s, int midiNote, std::uint32_t order) noexcept;
    void release(float releaseSeconds, double sampleRate) noexcept {
        held = false;
        relCoef = static_cast<float>(std::exp(-kDecayLn / (std::max(releaseSeconds, 0.005f) * sampleRate)));
    }
    float tick(NoiseSource& noise) noexcept;
};

inline void MalletVoice::trigger(const Strike& s, int midiNote, std::uint32_t order) noexcept {
    const double fs = s.sampleRate;
    const double hz = std::clamp(s.hz, 16.0, fs * 0.45);
    const auto& spec = materialSpec(s.material);

    // The mallet: a raised-cosine pulse, 6 ms for the softest yarn down to 0.15 ms for a hard stick, never
    // longer than half the note's period (a long pulse would filter its own fundamental away).
    const double widthSec = 0.006 * std::pow(0.025, static_cast<double>(s.hardness));
    pulseLen = std::clamp(static_cast<int>(std::lround(widthSec * fs)), 2,
                          std::max(2, static_cast<int>(std::lround(0.5 * fs / hz))));
    pulseIdx = 0;
    pulseStep = kTwoPi / pulseLen;
    pulseCosPrev = std::cos(-pulseStep);
    pulseCos = 1.0;
    const double pulseNorm = 2.0 / pulseLen; // the pulse sums to pulseLen / 2: unit area

    // Brightness tilts the gains by ratio^tilt, then the set is rescaled to the same total, so it moves
    // energy between low and high modes without changing the loudness.
    const double tilt = (static_cast<double>(s.brightness) - 0.5) * 1.2;
    double sumBase = 0.0, sumTilted = 0.0;
    int count = 0;
    for (int k = 0; k < spec.count; ++k) {
        if (hz * spec.ratio[static_cast<std::size_t>(k)] > fs * 0.45)
            continue;
        sumBase += spec.gain[static_cast<std::size_t>(k)];
        sumTilted += spec.gain[static_cast<std::size_t>(k)] * std::pow(spec.ratio[static_cast<std::size_t>(k)], tilt);
        ++count;
    }
    const double rescale = sumTilted > 0.0 ? sumBase / sumTilted : 1.0;

    numModes = 0;
    for (int k = 0; k < count; ++k) {
        const auto idx = static_cast<std::size_t>(k);
        const double ratio = spec.ratio[idx];
        const double w = kTwoPi * hz * ratio / fs;
        const double r = std::exp(-kDecayLn / (modeT60(s.decay, s.damping, ratio) * fs));
        double gain = spec.gain[idx] * std::pow(ratio, tilt) * rescale;
        if (k == spec.nominal)
            gain *= 0.5 + static_cast<double>(s.resonator); // the tube under the bar: 0.5 to 1.5 times
        auto& m = modes[idx];
        m = Mode{};
        m.a1 = 2.0 * r * std::cos(w);
        m.a2 = r * r;
        m.in = gain * std::sin(w) * pulseNorm; // sin(w) makes a unit-area strike ring at the gain given
        ++numModes;
    }
    norm = static_cast<float>(1.0 / std::max(sumBase, 1.0e-6));

    clickAmount = 0.25f * s.hardness * s.hardness;
    clickEnv = 1.0f;
    clickCoef = static_cast<float>(std::exp(-1.0 / (0.0006 * fs)));
    amp = s.velocity;
    relEnv = 1.0f;
    relCoef = 1.0f;
    watch.reset(1024);
    note = midiNote;
    serial = order;
    held = true;
    active = true;
}

inline float MalletVoice::tick(NoiseSource& noise) noexcept {
    double x = 0.0;
    if (pulseIdx < pulseLen) {
        x = 0.5 * (1.0 - pulseCos);
        const double next = 2.0 * std::cos(pulseStep) * pulseCos - pulseCosPrev;
        pulseCosPrev = pulseCos;
        pulseCos = next;
        ++pulseIdx;
    }
    double sum = 0.0;
    for (int k = 0; k < numModes; ++k) {
        auto& m = modes[static_cast<std::size_t>(k)];
        const double y = m.a1 * m.y1 - m.a2 * m.y2 + m.in * x;
        m.y2 = m.y1;
        m.y1 = y;
        sum += y;
    }
    float out = static_cast<float>(sum) * norm;
    out += noise.next() * clickEnv * clickAmount;
    clickEnv *= clickCoef;
    out *= amp * relEnv;
    if (!held) {
        relEnv *= relCoef;
        if (relEnv < 1.0e-4f)
            active = false;
    }
    if (watch.quietAfter(out))
        active = false;
    return out;
}

} // namespace synth::mallet
