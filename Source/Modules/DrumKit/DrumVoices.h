#pragma once

// The synthesized drum voices behind DrumKitModule. Every voice is monophonic (a new hit of the same
// drum restarts it), header-only, allocation-free and driven one sample at a time by the module.
// Levels, tune and decay arrive as plain numbers when a hit starts; nothing here reads a parameter.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace synth::drums {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = 6.28318530717959f;
/** ln(1000): an exponential decay reaches -60 dB after exactly its decay time. */
inline constexpr double kDecayLn = 6.90775527898;
/** An envelope below this is treated as finished (-80 dB), which is 1.33 x the decay time. */
inline constexpr float kSilence = 1.0e-4f;

/** Semitones to frequency ratio. */
inline float semitoneRatio(float semitones) noexcept { return std::exp2(semitones * (1.0f / 12.0f)); }

/** White noise in [-1, 1) from a xorshift generator; one per kit, shared by every voice. */
struct NoiseSource {
    std::uint32_t state = 0x2545F491u;
    float next() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(static_cast<std::int32_t>(state)) * (1.0f / 2147483648.0f);
    }
};

/** Exponential decay that falls 60 dB in `seconds`. */
struct DecayEnv {
    float level = 0.0f;
    float coef = 0.0f;

    void trigger(float seconds, double sampleRate) noexcept {
        level = 1.0f;
        setDecay(seconds, sampleRate);
    }
    void setDecay(float seconds, double sampleRate) noexcept {
        coef = static_cast<float>(std::exp(-kDecayLn / (std::max(seconds, 0.0005f) * sampleRate)));
    }
    float tick() noexcept {
        const float v = level;
        level *= coef;
        return v;
    }
    bool active() const noexcept { return level > kSilence; }
    void stop() noexcept { level = 0.0f; }
};

/** Zero-delay-feedback state variable filter (12 dB per octave). */
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;

    void set(float cutoffHz, float q, double sampleRate) noexcept {
        const float nyquistGuard = static_cast<float>(sampleRate) * 0.45f;
        const float g = std::tan(kPi * std::min(cutoffHz, nyquistGuard) / static_cast<float>(sampleRate));
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = lp = bp = hp = 0.0f; }
    void process(float x) noexcept {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

/** Settings captured when a hit starts. */
struct Hit {
    float velocity = 1.0f; // 0..1
    float gain = 1.0f;     // group level
    float tune = 1.0f;     // frequency ratio
    float decay = 0.3f;    // seconds
};

/** Brightness factor for filter cutoffs: a soft hit is a little duller than a hard one. */
inline float brightness(float velocity) noexcept { return 0.85f + 0.3f * velocity; }

/** Sine with an exponential pitch sweep, a short filtered-noise click and soft saturation. */
struct KickVoice {
    DecayEnv amp, click;
    float phase = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f, startHz = 0.0f, endHz = 0.0f, gain = 0.0f;
    Svf clickFilter;

    void trigger(const Hit& h, double sr) noexcept {
        startHz = 160.0f * h.tune;
        endHz = 50.0f * h.tune;
        pitchEnv = 1.0f;
        pitchCoef = static_cast<float>(std::exp(-1.0 / (0.012 * sr))); // about 40 ms to settle
        phase = 0.25f;                                                 // starts at the sine peak: a punch, not a ramp
        amp.trigger(h.decay, sr);
        click.trigger(0.002f, sr);
        clickFilter.set(3500.0f * brightness(h.velocity), 0.8f, sr);
        gain = h.velocity * h.gain;
    }
    bool active() const noexcept { return amp.active(); }
    void stop() noexcept { amp.stop(); }
    float tick(double sr, NoiseSource& noise) noexcept {
        const float hz = endHz + (startHz - endHz) * pitchEnv;
        pitchEnv *= pitchCoef;
        phase += hz / static_cast<float>(sr);
        phase -= std::floor(phase);
        clickFilter.process(noise.next());
        const float body = std::sin(kTwoPi * phase) * amp.tick();
        const float transient = clickFilter.bp * click.tick() * 0.5f;
        return std::tanh(1.6f * (body + transient)) * 0.78f * gain;
    }
};

/** Pitch-dropping sine plus high-passed noise. `rim` swaps the body for a short triangle click. */
struct SnareVoice {
    DecayEnv body, noiseEnv;
    Svf hpf, lpf;
    float phase = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f, baseHz = 0.0f, gain = 0.0f;
    bool isRim = false;

    void trigger(const Hit& h, bool rim, double sr) noexcept {
        isRim = rim;
        phase = 0.0f;
        pitchEnv = 1.0f;
        gain = h.velocity * h.gain;
        hpf.reset();
        lpf.reset();
        if (rim) {
            baseHz = 1750.0f * h.tune;
            pitchCoef = static_cast<float>(std::exp(-1.0 / (0.002 * sr)));
            body.trigger(0.012f, sr);
            noiseEnv.trigger(0.03f, sr);
            hpf.set(3000.0f * brightness(h.velocity), 0.707f, sr);
            lpf.set(9000.0f, 0.707f, sr);
            return;
        }
        baseHz = 185.0f * h.tune;
        pitchCoef = static_cast<float>(std::exp(-1.0 / (0.010 * sr)));
        body.trigger(h.decay * 0.55f, sr);
        noiseEnv.trigger(h.decay, sr);
        hpf.set(1500.0f * brightness(h.velocity), 0.707f, sr);
        lpf.set(6500.0f, 0.707f, sr);
    }
    bool active() const noexcept { return body.active() || noiseEnv.active(); }
    void stop() noexcept {
        body.stop();
        noiseEnv.stop();
    }
    float tick(double sr, NoiseSource& noise) noexcept {
        const float hz = baseHz * (1.0f + (isRim ? 0.25f : 0.35f) * pitchEnv);
        pitchEnv *= pitchCoef;
        phase += hz / static_cast<float>(sr);
        phase -= std::floor(phase);
        const float tri = 4.0f * std::fabs(phase - 0.5f) - 1.0f;
        const float tone = (isRim ? tri : std::sin(kTwoPi * phase)) * body.tick();
        hpf.process(noise.next());
        lpf.process(hpf.hp);
        const float hiss = lpf.lp * noiseEnv.tick();
        const float mix = isRim ? 0.75f * tone + 0.55f * hiss : 0.62f * tone + 0.6f * hiss;
        return std::tanh(1.3f * mix) * 0.85f * gain;
    }
};

/** Band-passed noise: three quick bursts about 10 ms apart, then a tail. */
struct ClapVoice {
    DecayEnv env;
    Svf bpf;
    float gain = 0.0f, tail = 0.2f;
    int samplesIn = 0, burstSpacing = 0, burstsLeft = 0;

    void trigger(const Hit& h, double sr) noexcept {
        bpf.reset();
        bpf.set(1200.0f * h.tune * brightness(h.velocity), 1.2f, sr);
        gain = h.velocity * h.gain;
        tail = h.decay;
        burstSpacing = static_cast<int>(0.010 * sr);
        burstsLeft = 3;
        samplesIn = 0;
        env.trigger(0.008f, sr);
        activeFlag = true;
        sampleRateHz = sr;
    }
    bool active() const noexcept { return activeFlag; }
    void stop() noexcept {
        env.stop();
        activeFlag = false;
        burstsLeft = 0;
    }
    float tick(NoiseSource& noise) noexcept {
        if (burstsLeft > 0 && ++samplesIn >= burstSpacing) {
            samplesIn = 0;
            --burstsLeft;
            // The last step of the sequence is the tail: same noise, slower decay.
            env.trigger(burstsLeft == 0 ? tail : 0.008f, sampleRateHz);
        }
        bpf.process(noise.next());
        const float out = bpf.bp * env.tick();
        if (burstsLeft == 0 && !env.active())
            activeFlag = false;
        return std::tanh(1.4f * out * 2.2f) * 0.7f * gain;
    }

private:
    bool activeFlag = false;
    double sampleRateHz = 44100.0;
};

/** Six detuned square oscillators at 808-style ratios; the raw material of hats and cymbals. */
struct MetallicBank {
    static constexpr std::array<float, 6> kHz = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
    std::array<float, 6> phase{};

    void reset() noexcept { phase.fill(0.0f); }
    float tick(float tune, double sr) noexcept {
        float sum = 0.0f;
        for (std::size_t i = 0; i < kHz.size(); ++i) {
            phase[i] += kHz[i] * tune / static_cast<float>(sr);
            phase[i] -= std::floor(phase[i]);
            sum += phase[i] < 0.5f ? 1.0f : -1.0f;
        }
        return sum * (1.0f / 6.0f);
    }
};

enum class MetallicKind { ClosedHat, OpenHat, Crash, Ride };

/** Hats, crash and ride: the metallic bank, a little noise, a high-pass and a low-pass (a band for the ride). */
struct MetallicVoice {
    DecayEnv env;
    MetallicBank bank;
    Svf hpf, lpf;
    float tune = 1.0f, gain = 0.0f, noiseMix = 0.0f, outScale = 1.0f;

    void trigger(const Hit& h, MetallicKind kind, double sr) noexcept {
        bank.reset();
        hpf.reset();
        lpf.reset();
        tune = h.tune;
        gain = h.velocity * h.gain;
        const float bright = brightness(h.velocity);
        switch (kind) {
        case MetallicKind::ClosedHat:
        case MetallicKind::OpenHat:
            noiseMix = 0.15f;
            outScale = 7.4f;
            hpf.set(7000.0f * bright, 0.8f, sr);
            lpf.set(12000.0f, 0.7f, sr); // tames the aliasing of the square oscillators
            break;
        case MetallicKind::Crash:
            noiseMix = 0.4f;
            outScale = 3.6f;
            hpf.set(5000.0f * bright, 0.7f, sr);
            lpf.set(12000.0f, 0.7f, sr);
            break;
        case MetallicKind::Ride:
            noiseMix = 0.0f;
            outScale = 4.3f;
            hpf.set(3000.0f * bright, 0.7f, sr);
            lpf.set(8000.0f, 0.7f, sr);
            break;
        }
        env.trigger(h.decay, sr);
    }
    bool active() const noexcept { return env.active(); }
    void stop() noexcept { env.stop(); }
    /** Cuts the ring-out in about 3 ms (the closed hat choking the open hat). */
    void choke(double sr) noexcept { env.setDecay(0.003f, sr); }
    float tick(double sr, NoiseSource& noise) noexcept {
        const float src = bank.tick(tune, sr) * 0.8f + noise.next() * noiseMix;
        hpf.process(src);
        lpf.process(hpf.hp);
        return lpf.lp * env.tick() * outScale * 0.6f * gain;
    }
};

/** Sine with a small pitch drop, tuned per note. */
struct TomVoice {
    DecayEnv amp;
    float phase = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f, baseHz = 0.0f, gain = 0.0f;

    void trigger(const Hit& h, float noteHz, double sr) noexcept {
        baseHz = noteHz * h.tune;
        phase = 0.25f;
        pitchEnv = 1.0f;
        pitchCoef = static_cast<float>(std::exp(-1.0 / (0.020 * sr)));
        amp.trigger(h.decay, sr);
        gain = h.velocity * h.gain;
    }
    bool active() const noexcept { return amp.active(); }
    void stop() noexcept { amp.stop(); }
    float tick(double sr) noexcept {
        const float hz = baseHz * (1.0f + 0.3f * pitchEnv);
        pitchEnv *= pitchCoef;
        phase += hz / static_cast<float>(sr);
        phase -= std::floor(phase);
        return std::tanh(1.2f * std::sin(kTwoPi * phase) * amp.tick()) * 0.8f * gain;
    }
};

} // namespace synth::drums
