// FilterSpectrumAnalyserTests.cpp -- the shared spectrum analyser's ballistics: how fast the trace rises and falls on
// a sine stepping in and out, measured in real time so the answer is the same at any poll rate, how steady it holds
// on a sustained saw, and whether two low notes an octave apart stay two peaks.
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include "UI/ModuleViews/FrequencyGrid.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {

constexpr double kRate = 48000.0;

int gridIndexFor(float hz) {
    return (int)std::lround(std::log(hz / FrequencyGrid::kMinFreq) /
                            std::log(FrequencyGrid::kMaxFreq / FrequencyGrid::kMinFreq) *
                            (FilterSpectrumAnalyser::kNumPoints - 1));
}

/** A signal source the analyser reads like a module's visual buffer: the newest kFftSize samples at time `t`. */
struct Signal {
    double onAt = 0.0, offAt = 1.0e9;
    std::function<float(double)> wave = [](double t) {
        return (float)(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
    };
    std::vector<float> frameAt(double t) const {
        std::vector<float> out((size_t)FilterSpectrumAnalyser::kFftSize);
        for (int i = 0; i < (int)out.size(); ++i) {
            const double ts = t - (double)(out.size() - 1 - (size_t)i) / kRate;
            out[(size_t)i] = (ts >= onAt && ts < offAt) ? wave(ts) : 0.0f;
        }
        return out;
    }
};

/** The trace at `hz` read every 1 / `pollHz` seconds from t = 0 to `until`: (time, dB) pairs. */
std::vector<std::pair<double, float>> track(const Signal& s, double pollHz, double until, float hz) {
    FilterSpectrumAnalyser a;
    std::vector<std::pair<double, float>> out;
    const double dt = 1.0 / pollHz;
    for (double t = 0.0; t <= until + 1e-9; t += dt) {
        const auto frame = s.frameAt(t);
        a.analyse(frame.data(), (int)frame.size(), kRate, dt);
        out.push_back({t, a.getDb()[(size_t)gridIndexFor(hz)]});
    }
    return out;
}

float valueAt(const std::vector<std::pair<double, float>>& trace, double t) {
    for (size_t i = 1; i < trace.size(); ++i)
        if (trace[i].first >= t - 1e-9) {
            const auto [t0, v0] = trace[i - 1];
            const auto [t1, v1] = trace[i];
            return v0 + (v1 - v0) * (float)((t - t0) / (t1 - t0));
        }
    return trace.back().second;
}

} // namespace

// Regression test for FRO1191: the trace smoothed by a fixed share per call, so its speed hung on the poll rate, and
// rose as slowly as it fell.
TEST(FilterSpectrumAnalyserBallistics, RisesFastFallsOverAboutHalfASecondAndIgnoresThePollRate) {
    Signal s;
    s.onAt = 0.1;
    s.offAt = 1.0;
    const auto at30 = track(s, 30.0, 3.0, 1000.0f);
    const auto at60 = track(s, 60.0, 3.0, 1000.0f);
    const float held = valueAt(at60, 0.95);
    const double frame = FilterSpectrumAnalyser::kFftSize / kRate;
    // Rise: within one FFT frame plus a few attack time constants of the sine reaching the window's centre.
    const double risen = s.onAt + frame / 2.0 + 5.0 * FilterSpectrumAnalyser::kAttackSeconds;
    EXPECT_GT(valueAt(at60, risen + 1.0 / 60.0), held - 3.0f);
    EXPECT_NEAR(held, 20.0f * std::log10(0.5f), 1.5f) << "a 0.5 sine reads its own level";
    // Fall: about 30 dB in the half second after the note stops, never all at once and not forever.
    const float fall = held - valueAt(at60, s.offAt + 0.5);
    const float early = held - valueAt(at60, s.offAt + 0.1);
    printf("[ FRO1191 ] held %.1f dB at 1 kHz; after the note stops: -%.1f dB at 0.1 s, -%.1f dB at 0.5 s\n", held,
           early, fall);
    EXPECT_NEAR(fall, 30.0f, 4.0f);
    EXPECT_LT(early, 12.0f) << "not at once";
    EXPECT_LT(valueAt(at60, s.offAt + 1.9), FilterSpectrumAnalyser::kMinDb + 1.0f) << "and not forever";
    for (const double t : {0.15, 0.2, 0.3, 0.6, 0.95, 1.1, 1.3, 1.6})
        EXPECT_NEAR(valueAt(at30, t), valueAt(at60, t), 1.5f) << "30 Hz and 60 Hz polls agree at t = " << t;
}

TEST(FilterSpectrumAnalyserBallistics, AHeldSawIsSteady) {
    Signal s;
    s.wave = [](double t) {
        const double phase = std::fmod(220.0 * t, 1.0);
        return (float)(0.5 * (2.0 * phase - 1.0));
    };
    FilterSpectrumAnalyser a;
    const double dt = 1.0 / 30.0;
    std::vector<std::vector<float>> frames;
    for (int n = 0; n < 60; ++n) {
        const auto frame = s.frameAt(n * dt + 0.0137 * n); // the poll never lines up with the period
        a.analyse(frame.data(), (int)frame.size(), kRate, dt);
        if (n >= 30)
            frames.push_back(a.getDb());
    }
    float worst = 0.0f;
    for (const float hz : {220.0f, 440.0f, 660.0f, 1320.0f, 2640.0f}) {
        const auto i = (size_t)gridIndexFor(hz);
        float lo = 1e9f, hi = -1e9f;
        for (const auto& f : frames) {
            lo = std::min(lo, f[i]);
            hi = std::max(hi, f[i]);
        }
        worst = std::max(worst, hi - lo);
    }
    printf("[ FRO1191 ] held saw: largest frame-to-frame swing at its harmonics %.2f dB\n", worst);
    EXPECT_LT(worst, 1.0f);
}

TEST(FilterSpectrumAnalyserBallistics, TwoLowNotesAnOctaveApartAreTwoPeaks) {
    Signal s;
    s.wave = [](double t) {
        const double w = 2.0 * juce::MathConstants<double>::pi * t;
        return (float)(0.25 * std::sin(w * 100.0) + 0.25 * std::sin(w * 200.0));
    };
    FilterSpectrumAnalyser a;
    for (int n = 0; n < 20; ++n) {
        const auto frame = s.frameAt(n / 30.0);
        a.analyse(frame.data(), (int)frame.size(), kRate, 1.0 / 30.0);
    }
    const auto& db = a.getDb();
    const float at100 = db[(size_t)gridIndexFor(100.0f)];
    const float at200 = db[(size_t)gridIndexFor(200.0f)];
    const float between = db[(size_t)gridIndexFor(141.4f)];
    printf("[ FRO1191 ] 100 + 200 Hz: %.1f / %.1f dB, %.1f dB between\n", at100, at200, between);
    EXPECT_GT(at100 - between, 12.0f);
    EXPECT_GT(at200 - between, 12.0f);
}

TEST(FilterSpectrumAnalyserBallistics, ANarrowHighPeakBetweenTwoPointsIsNotLost) {
    // At the top the points are wider than a bin: the reading is the largest bin, so the level stays the sine's.
    Signal s;
    s.wave = [](double t) { return (float)(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 12345.0 * t)); };
    FilterSpectrumAnalyser a;
    for (int n = 0; n < 20; ++n) {
        const auto frame = s.frameAt(n / 30.0);
        a.analyse(frame.data(), (int)frame.size(), kRate, 1.0 / 30.0);
    }
    const auto& db = a.getDb();
    const float peak = *std::max_element(db.begin(), db.end());
    EXPECT_NEAR(peak, 20.0f * std::log10(0.5f), 1.5f) << "a 0.5 sine reads its own level";
}
