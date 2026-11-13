// FilterSpectrumAnalyser.cpp -- see FilterSpectrumAnalyser.h.
#include "FilterSpectrumAnalyser.h"
#include "FrequencyGrid.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
double realClock() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
double (*clock_)() = &realClock;

/** Full scale reads 0 dB; below the floor stays at the floor. */
constexpr float kFloorMagnitude = 1.0e-4f; // -80 dB
} // namespace

void FilterSpectrumAnalyser::setClockForTest(double (*clock)()) { clock_ = clock != nullptr ? clock : &realClock; }

FilterSpectrumAnalyser::FilterSpectrumAnalyser()
    : fftData_((size_t)kFftSize * 2, 0.0f)
    , magnitude_((size_t)kNumPoints, 0.0f)
    , db_((size_t)kNumPoints, kMinDb)
    , bins_((size_t)kNumPoints) {}

void FilterSpectrumAnalyser::reset() {
    std::fill(magnitude_.begin(), magnitude_.end(), 0.0f);
    std::fill(db_.begin(), db_.end(), kMinDb);
    lastTime_ = -1.0;
}

/** The first call after a reset has no previous time, so it counts as one 30 Hz frame. */
void FilterSpectrumAnalyser::analyse(const VisualBuffer& buffer, double sampleRate) {
    scratch_.resize((size_t)buffer.getSize());
    buffer.copyTo(scratch_);
    const double now = clock_();
    const double elapsed = lastTime_ < 0.0 ? 1.0 / 30.0 : now - lastTime_;
    lastTime_ = now;
    analyse(scratch_.data(), (int)scratch_.size(), sampleRate, elapsed);
}

/** Each point covers the span halfway to its neighbours on the log axis. Where that span holds a bin centre (from
    about 500 Hz up at 48 kHz) the reading is the largest of those bins, so a narrow peak between two points is never
    lost; below, where the points are closer together than the bins, it is interpolated in power between the two bins
    around the point, which keeps two low notes an octave apart as two separate humps. */
void FilterSpectrumAnalyser::updateBinMap(double sampleRate) {
    if (sampleRate == binMapRate_)
        return;
    binMapRate_ = sampleRate;
    const double binWidth = sampleRate / kFftSize;
    const int lastBin = kFftSize / 2 - 1;
    for (int i = 0; i < kNumPoints; ++i) {
        const double f = FrequencyGrid::indexToFreq(i, kNumPoints);
        const double lo = std::sqrt(f * FrequencyGrid::indexToFreq(std::max(0, i - 1), kNumPoints));
        const double hi = std::sqrt(f * FrequencyGrid::indexToFreq(std::min(kNumPoints - 1, i + 1), kNumPoints));
        auto& b = bins_[(size_t)i];
        b.first = (int)std::ceil(lo / binWidth);
        b.last = std::min(lastBin, (int)std::floor(hi / binWidth));
        const double exact = f / binWidth;
        b.below = juce::jlimit(0, lastBin - 1, (int)exact);
        b.frac = (float)juce::jlimit(0.0, 1.0, exact - b.below);
    }
}

/** Ballistics per point on the linear magnitude: a one-pole toward the reading with the attack time constant while
    it rises and the release one while it falls, its coefficient 1 - exp(-dt / tau) from the real time since the last
    reading, so a 30 Hz and a 60 Hz caller show the same trace. The window is normalised to unit mean, so a sine reads
   its own peak level (a full scale sine 0 dB). */
void FilterSpectrumAnalyser::analyse(const float* samples, int numSamples, double sampleRate, double elapsedSeconds) {
    updateBinMap(sampleRate);
    std::fill(fftData_.begin(), fftData_.end(), 0.0f);
    const int copySize = std::min(numSamples, kFftSize);
    const int start = numSamples - copySize; // the newest samples
    for (int i = 0; i < copySize; ++i)
        fftData_[(size_t)i] = samples[start + i];

    window_.multiplyWithWindowingTable(fftData_.data(), kFftSize);
    fft_.performFrequencyOnlyForwardTransform(fftData_.data());

    const double dt = juce::jlimit(0.0, kMaxStepSeconds, elapsedSeconds);
    const float attack = (float)(1.0 - std::exp(-dt / kAttackSeconds));
    const float release = (float)(1.0 - std::exp(-dt / kReleaseSeconds));
    const float normFactor = 2.0f / static_cast<float>(kFftSize);

    for (int i = 0; i < kNumPoints; ++i) {
        const auto& b = bins_[(size_t)i];
        float mag = 0.0f;
        if (b.first <= b.last) {
            for (int k = b.first; k <= b.last; ++k)
                mag = std::max(mag, fftData_[(size_t)k]);
        } else {
            const float p0 = fftData_[(size_t)b.below] * fftData_[(size_t)b.below];
            const float p1 = fftData_[(size_t)b.below + 1] * fftData_[(size_t)b.below + 1];
            mag = std::sqrt(p0 + (p1 - p0) * b.frac);
        }
        mag *= normFactor;

        auto& m = magnitude_[(size_t)i];
        m += (mag > m ? attack : release) * (mag - m);
        db_[(size_t)i] = juce::jlimit(kMinDb, kMaxDb, 20.0f * std::log10(std::max(m, kFloorMagnitude)));
    }
}

} // namespace synth::ui
