// A held Unison 7 saw stack is as wide and beats as deeply as the reference stack, measured as a black box (the
// reference synth rendered offline with Unison 7, Detune and Blend and Width at its defaults, Rand 100, Phase 0, notes
// C2 / A3 / C5; Tools/ReferenceCompare/width.py, docs/modules/oscillator-voice.md "Width and beating"). The pinned
// numbers are that synth's, in the 2-8 kHz band of the settled A3 stack: the side/mid power ratio, the left/right
// correlation and the beating depth of the mid signal's envelope. A law change that moves any of them out of the
// tolerance fails here.

#include "Modules/OscillatorModule.h"
#include "UnisonStackMeasure.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace {

constexpr double kReferenceSideMidDb = -0.68;
constexpr double kReferenceCorrelation = 0.079;
constexpr double kReferenceBeatDepth = 0.206;
constexpr double kSideMidTolDb = 0.35;
constexpr double kCorrelationTol = 0.03;
constexpr double kBeatDepthTol = 0.08;

constexpr int kOrder = 18;
constexpr int kLen = 1 << kOrder;

// The band's analytic signal (positive-frequency bins of [lo, hi] only), by FFT.
std::vector<std::complex<float>> analyticBand(const std::vector<float>& x, double lo, double hi) {
    juce::dsp::FFT fft(kOrder);
    std::vector<std::complex<float>> in(kLen), spec(kLen);
    for (int i = 0; i < kLen; ++i)
        in[(size_t)i] = {x[(size_t)i], 0.0f};
    fft.perform(in.data(), spec.data(), false);
    for (int k = 0; k < kLen; ++k) {
        const double hz = k * unison_measure::kRate / kLen;
        if (k > kLen / 2 || hz < lo || hz > hi)
            spec[(size_t)k] = 0.0f;
    }
    std::vector<std::complex<float>> out(kLen);
    fft.perform(spec.data(), out.data(), true);
    return out;
}

} // namespace

TEST(UnisonWidthReference, HeldUnisonSevenSawIsAsWideAndBeatsAsDeeplyAsTheReference) {
    OscillatorModule m;
    unison_measure::setActual(m, "level", 1.0f);
    unison_measure::setActual(m, "waveform", 2.0f);
    unison_measure::setActual(m, "unison", 7.0f);
    unison_measure::setActual(m, "detune", 35.0f);
    unison_measure::setActual(m, "blend", 0.75f);
    unison_measure::setActual(m, "width", 1.0f);
    unison_measure::setActual(m, "randomPhase", 100.0f);
    unison_measure::setActual(m, "phase", 0.0f);
    std::vector<float> l, r;
    unison_measure::renderHeld(m, l, r);
    ASSERT_GE((int)l.size(), kLen);
    l.resize(kLen);
    r.resize(kLen);

    const auto al = analyticBand(l, 2000.0, 8000.0), ar = analyticBand(r, 2000.0, 8000.0);
    double ss = 0, mm = 0, lr = 0, ll = 0, rr = 0;
    std::vector<double> env(kLen);
    for (int i = 0; i < kLen; ++i) {
        const double a = al[(size_t)i].real(), b = ar[(size_t)i].real();
        const double mid = 0.5 * (a + b), side = 0.5 * (a - b);
        mm += mid * mid;
        ss += side * side;
        lr += a * b;
        ll += a * a;
        rr += b * b;
        env[(size_t)i] = std::abs(0.5f * (al[(size_t)i] + ar[(size_t)i]));
    }
    const int k = (int)(0.005 * unison_measure::kRate);
    std::vector<double> smooth;
    double run = 0;
    for (int i = 0; i < kLen; ++i) {
        run += env[(size_t)i];
        if (i >= k)
            run -= env[(size_t)(i - k)];
        if (i >= 2 * k && i < kLen - 2 * k)
            smooth.push_back(run / k);
    }
    std::sort(smooth.begin(), smooth.end());
    const double p5 = smooth[smooth.size() * 5 / 100], p95 = smooth[smooth.size() * 95 / 100];

    const double sideMidDb = 10.0 * std::log10(ss / mm);
    const double corr = lr / std::sqrt(ll * rr);
    const double beat = (p95 - p5) / (p95 + p5);
    RecordProperty("sideMidDb", std::to_string(sideMidDb));
    RecordProperty("correlation", std::to_string(corr));
    RecordProperty("beatDepth", std::to_string(beat));
    std::printf("width: sideMidDb %.3f corr %.4f beat %.3f\n", sideMidDb, corr, beat);
    EXPECT_NEAR(sideMidDb, kReferenceSideMidDb, kSideMidTolDb);
    EXPECT_NEAR(corr, kReferenceCorrelation, kCorrelationTol);
    EXPECT_NEAR(beat, kReferenceBeatDepth, kBeatDepthTol);
}
