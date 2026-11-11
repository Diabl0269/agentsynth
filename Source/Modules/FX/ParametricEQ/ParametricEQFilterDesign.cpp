// The Parametric EQ's band maths: digital stages (RBJ cookbook biquads, bilinear first-order stages,
// Butterworth cascades for steep cuts) and the analog magnitudes they are derived from. See
// docs/modules/fx-modules.md#parametric-eq-module for what each type and slope does.
#include "Modules/FX/ParametricEQ/ParametricEQFilterDesign.h"

#include <algorithm>
#include <cmath>

namespace synth::eq {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clampedCentre(float centreHz, double fs) noexcept {
    // Keep the pole away from Nyquist, where the bilinear transform degenerates.
    return std::clamp(static_cast<double>(centreHz), 1.0, fs * 0.49);
}

void store(double b0, double b1, double b2, double a0, double a1, double a2, float* dest) noexcept {
    const double inv = (std::abs(a0) > 1.0e-12) ? 1.0 / a0 : 1.0;
    dest[0] = static_cast<float>(b0 * inv);
    dest[1] = static_cast<float>(b1 * inv);
    dest[2] = static_cast<float>(b2 * inv);
    dest[3] = static_cast<float>(a1 * inv);
    dest[4] = static_cast<float>(a2 * inv);
}

/** The bilinear transform of H(s) = (B1 s + B0) / (A1 s + A0), s normalised to the (prewarped) centre,
 *  as a biquad with b2 = a2 = 0, so every stage keeps the same order-2 coefficient layout. */
void writeFirstOrder(double B1, double B0, double A1, double A0, float centreHz, double sampleRate,
                     float* dest) noexcept {
    const double fs = (sampleRate > 0.0) ? sampleRate : 44100.0;
    const double K = std::tan(kPi * clampedCentre(centreHz, fs) / fs);
    store(B1 + B0 * K, B0 * K - B1, 0.0, A1 + A0 * K, A0 * K - A1, 0.0, dest);
}

/** A 12 dB/oct high- or low-pass biquad at `q`: one stage of a cut's cascade. */
void writeCutBiquad(bool highPass, float centreHz, double q, double sampleRate, float* dest) noexcept {
    const double fs = (sampleRate > 0.0) ? sampleRate : 44100.0;
    const double w0 = 2.0 * kPi * clampedCentre(centreHz, fs) / fs;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(0.05, q));
    const double edge = highPass ? (1.0 + cosW0) : (1.0 - cosW0);
    store(edge / 2.0, highPass ? -edge : edge, edge / 2.0, 1.0 + alpha, -2.0 * cosW0, 1.0 - alpha, dest);
}

/** Q of biquad `k` (1-based) of an order-`order` Butterworth cut: 1 / (2 sin((2k - 1) pi / 2N)). Stage 1 has the
 *  highest Q; the band's own Q scales it, so the default Q (1/sqrt 2) leaves the cascade maximally flat and a
 *  higher Q adds a resonant bump at the corner, exactly as the 12 dB/oct cut's single biquad does. */
double cascadeQ(int k, int order, float bandQ) noexcept {
    const double q = 1.0 / (2.0 * std::sin((2.0 * k - 1.0) * kPi / (2.0 * order)));
    return k == 1 ? q * std::max(0.05, static_cast<double>(bandQ)) / kButterworthQ : q;
}

float powerToDb(double numSq, double denSq) noexcept {
    if (!(denSq > 0.0))
        return 0.0f;
    // 10*log10 of a squared magnitude ratio == 20*log10 of the magnitude ratio.
    return static_cast<float>(10.0 * std::log10(std::max(numSq / denSq, 1.0e-24)));
}

/** The four-band module's float form, kept to the bit for the shelves and Peak. */
float legacyPowerToDb(float numSq, float denSq) noexcept {
    if (!(denSq > 0.0f))
        return 0.0f;
    return 10.0f * std::log10(std::max(numSq / denSq, 1.0e-12f));
}

/** |H|^2 of a second-order cut stage at normalised frequency w. */
float cutBiquadDb(bool highPass, double w, double q) noexcept {
    const double w2 = w * w;
    const double den = (1.0 - w2) * (1.0 - w2) + (w / q) * (w / q);
    return powerToDb(highPass ? w2 * w2 : 1.0, den);
}

float cutMagnitudeDb(bool highPass, double w, float bandQ, int slopeDbPerOct) noexcept {
    const int order = std::max(1, slopeDbPerOct / 6);
    float total = 0.0f;
    if (order % 2 == 1)
        total += powerToDb(highPass ? w * w : 1.0, 1.0 + w * w);
    if (order == 1)
        return total;
    for (int k = 1; k <= order / 2; ++k)
        total += cutBiquadDb(highPass, w, cascadeQ(k, order, bandQ));
    return total;
}

} // namespace

bool isCut(BandType type) noexcept { return type == BandType::LowCut || type == BandType::HighCut; }

int slopeIndexFor(int slopeDbPerOct) noexcept {
    for (int i = 0; i < (int)kSlopesDbPerOct.size(); ++i)
        if (kSlopesDbPerOct[(size_t)i] == slopeDbPerOct)
            return i;
    return kDefaultSlopeIndex;
}

int slopeDbFor(int index) noexcept {
    return kSlopesDbPerOct[(size_t)std::clamp(index, 0, (int)kSlopesDbPerOct.size() - 1)];
}

int stageCount(BandType type, int slopeDbPerOct) noexcept {
    if (!isCut(type))
        return 1;
    const int order = std::max(1, slopeDbPerOct / 6);
    return std::min(kMaxStages, (order + 1) / 2);
}

void writeUnity(float* dest) noexcept {
    dest[0] = 1.0f;
    dest[1] = 0.0f;
    dest[2] = 0.0f;
    dest[3] = 0.0f;
    dest[4] = 0.0f;
}

// The shelves and Peak are the four-band module's maths to the bit: a saved patch's bands 1-4 must render the same.
void writeBiquad(BandType type, float centreHz, float gainDb, float q, double sampleRate, float* dest) noexcept {
    const double fs = (sampleRate > 0.0) ? sampleRate : 44100.0;
    const double f0 = clampedCentre(centreHz, fs);
    const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double Q = std::max(0.05, static_cast<double>(q));
    const double w0 = 2.0 * kPi * f0 / fs;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double sqrtA = std::sqrt(A);

    switch (type) {
    case BandType::Peak:
        store(1.0 + alpha * A, -2.0 * cosW0, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cosW0, 1.0 - alpha / A, dest);
        return;
    case BandType::LowShelf:
        store(A * ((A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha), 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0),
              A * ((A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha),
              (A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha, -2.0 * ((A - 1.0) + (A + 1.0) * cosW0),
              (A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha, dest);
        return;
    case BandType::HighShelf:
        store(A * ((A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha), -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0),
              A * ((A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha),
              (A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha, 2.0 * ((A - 1.0) - (A + 1.0) * cosW0),
              (A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha, dest);
        return;
    case BandType::Notch:
        store(1.0, -2.0 * cosW0, 1.0, 1.0 + alpha, -2.0 * cosW0, 1.0 - alpha, dest);
        return;
    case BandType::BandPass: // constant 0 dB peak at the centre
        store(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cosW0, 1.0 - alpha, dest);
        return;
    case BandType::LowCut:
    case BandType::HighCut:
        writeCutBiquad(type == BandType::LowCut, centreHz, Q, sampleRate, dest);
        return;
    case BandType::Tilt:
        // H(s) = (A s + 1) / (s + A): -gain/2 at DC, +gain/2 towards Nyquist, 0 dB at the centre. A = 10^(gain/40),
        // so `gain` is the whole low-to-high tilt.
        writeFirstOrder(A, 1.0, 1.0, A, centreHz, sampleRate, dest);
        return;
    }
    writeUnity(dest);
}

// A cut's cascade: an odd multiple of 6 dB/oct leads with its first-order stage, then the biquads from the lowest Q
// to the highest, so the resonant stage (the one the band's Q shapes) runs last and an early stage never rings.
int designStages(BandType type, int slopeDbPerOct, float centreHz, float gainDb, float q, double sampleRate,
                 BandStages& dest) noexcept {
    if (!isCut(type)) {
        writeBiquad(type, centreHz, gainDb, q, sampleRate, dest[0].data());
        return 1;
    }
    const bool highPass = type == BandType::LowCut;
    const int order = std::clamp(slopeDbPerOct / 6, 1, 2 * kMaxStages);
    int count = 0;
    if (order % 2 == 1)
        writeFirstOrder(highPass ? 1.0 : 0.0, highPass ? 0.0 : 1.0, 1.0, 1.0, centreHz, sampleRate,
                        dest[(size_t)count++].data());
    for (int k = order / 2; k >= 1; --k)
        writeCutBiquad(highPass, centreHz, cascadeQ(k, order, q), sampleRate, dest[(size_t)count++].data());
    return count;
}

// The analog prototypes the digital stages are bilinear transforms of (prewarped at the centre), so the drawn curve
// matches the audio exactly at the centre and closely below about a quarter of the sample rate.
float bandMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq, int slopeDbPerOct) noexcept {
    if (centreHz <= 0.0f || freq <= 0.0f || !std::isfinite(centreHz) || !std::isfinite(freq))
        return 0.0f;

    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float Q = std::max(0.05f, q);
    const float w = freq / centreHz;
    const float w2 = w * w;
    const float sqrtA = std::sqrt(A);

    switch (type) {
    case BandType::Peak: {
        // H(s) = (s^2 + (A/Q)s + 1) / (s^2 + s/(A*Q) + 1),  s = jw,  w = f/f0
        const float base = (1.0f - w2) * (1.0f - w2);
        const float numImag = A * w / Q;
        const float denImag = w / (A * Q);
        return legacyPowerToDb(base + numImag * numImag, base + denImag * denImag);
    }
    case BandType::LowShelf: {
        // H(s) = A * (s^2 + (sqrt(A)/Q)s + A) / (A*s^2 + (sqrt(A)/Q)s + 1)
        const float imag = w * sqrtA / Q;
        return legacyPowerToDb(A * A * ((A - w2) * (A - w2) + imag * imag),
                               (1.0f - A * w2) * (1.0f - A * w2) + imag * imag);
    }
    case BandType::HighShelf: {
        // H(s) = A * (A*s^2 + (sqrt(A)/Q)s + 1) / (s^2 + (sqrt(A)/Q)s + A)
        const float imag = w * sqrtA / Q;
        return legacyPowerToDb(A * A * ((1.0f - A * w2) * (1.0f - A * w2) + imag * imag),
                               (A - w2) * (A - w2) + imag * imag);
    }
    case BandType::Notch: {
        const double base = (1.0 - w2) * (1.0 - w2);
        return powerToDb(base, base + (w / Q) * (w / Q));
    }
    case BandType::BandPass: {
        const double edge = (w / Q) * (w / Q);
        return powerToDb(edge, (1.0 - w2) * (1.0 - w2) + edge);
    }
    case BandType::LowCut:
    case BandType::HighCut:
        return cutMagnitudeDb(type == BandType::LowCut, w, q, slopeDbPerOct);
    case BandType::Tilt:
        return powerToDb(static_cast<double>(A) * A * w2 + 1.0, w2 + static_cast<double>(A) * A);
    }
    return 0.0f;
}

} // namespace synth::eq
