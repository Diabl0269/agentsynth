#include "Timeline/Melody/PitchTracker.h"

#include <algorithm>
#include <cmath>

namespace synth::melody {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kSilenceDb = -200.0f;

// 10 * log10(mean square), which is 20 * log10(rms).
float levelDb(double meanSquare) { return meanSquare <= 1.0e-20 ? kSilenceDb : (float)(10.0 * std::log10(meanSquare)); }
} // namespace

double PitchTracker::Biquad::process(double x) noexcept {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

PitchTracker::PitchTracker(const PitchTrackerSettings& settings)
    : settings_(settings) {
    reset(44100.0);
}

// The input is lowpassed (two Butterworth sections) and linearly interpolated down to the analysis rate: YIN needs
// nothing above a few kHz, and the shorter lags keep the per-frame cost small. A rate at or below the analysis rate is
// tracked as it is.
void PitchTracker::reset(double sampleRate) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    resampling_ = sampleRate_ > settings_.analysisRate * 1.001;
    rate_ = resampling_ ? settings_.analysisRate : sampleRate_;
    ratio_ = sampleRate_ / rate_;
    if (resampling_) {
        const double w0 = 2.0 * kPi * (rate_ * 0.45) / sampleRate_;
        const double cosW = std::cos(w0);
        const double sinW = std::sin(w0);
        static constexpr double kQ[2] = {0.5411961, 1.3065630}; // 4th-order Butterworth
        for (int i = 0; i < 2; ++i) {
            const double alpha = sinW / (2.0 * kQ[i]);
            const double a0 = 1.0 + alpha;
            lowpass_[i] = Biquad{};
            lowpass_[i].b0 = (1.0 - cosW) / 2.0 / a0;
            lowpass_[i].b1 = (1.0 - cosW) / a0;
            lowpass_[i].b2 = (1.0 - cosW) / 2.0 / a0;
            lowpass_[i].a1 = -2.0 * cosW / a0;
            lowpass_[i].a2 = (1.0 - alpha) / a0;
        }
    }
    inputIndex_ = 0.0;
    nextPosition_ = 0.0;
    previous_ = 0.0;
    buffer_.clear();
    bufferStart_ = 0;
    frameCount_ = 0;
    peakDb_ = kSilenceDb;
    minLag_ = std::max(2, (int)std::floor(rate_ / settings_.maxHz));
    maxLag_ = std::min(settings_.window / 2, (int)std::ceil(rate_ / settings_.minHz));
    normalised_.assign((size_t)maxLag_ + 2, 0.0f);
}

void PitchTracker::push(const float* mono, int numSamples, std::vector<PitchFrame>& out) {
    for (int i = 0; i < numSamples; ++i) {
        if (!resampling_) {
            pushAnalysed(mono[i], out);
            continue;
        }
        double y = mono[i];
        for (auto& section : lowpass_)
            y = section.process(y);
        // The analysed sample m sits at input position m * ratio; it is ready once the sample after it has arrived.
        while (nextPosition_ <= inputIndex_) {
            const double fraction = nextPosition_ - (inputIndex_ - 1.0);
            pushAnalysed((float)(previous_ + (y - previous_) * fraction), out);
            nextPosition_ += ratio_;
        }
        previous_ = y;
        inputIndex_ += 1.0;
    }
}

void PitchTracker::pushAnalysed(float sample, std::vector<PitchFrame>& out) {
    buffer_.push_back(sample);
    const auto window = (std::size_t)settings_.window;
    while (buffer_.size() - bufferStart_ >= window) {
        analyseFrame(buffer_.data() + bufferStart_, out);
        bufferStart_ += (std::size_t)settings_.hop;
    }
    // Drop consumed audio now and then so a long take does not keep every sample.
    if (bufferStart_ > 16384) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + (std::ptrdiff_t)bufferStart_);
        bufferStart_ = 0;
    }
}

// One frame: the cumulative-mean-normalised difference over lags 1..maxLag_, the first dip under the threshold (else
// the global minimum), refined by a parabola through its neighbours. Every lag compares the same `span` samples.
void PitchTracker::analyseFrame(const float* window, std::vector<PitchFrame>& out) {
    const int size = settings_.window;
    const int span = size - maxLag_;
    double energy = 0.0;
    for (int i = 0; i < size; ++i)
        energy += (double)window[i] * window[i];

    double head = 0.0; // sum of window[0..span)^2
    for (int i = 0; i < span; ++i)
        head += (double)window[i] * window[i];
    double tail = head; // sum of window[lag..lag+span)^2, slid along per lag

    double running = 0.0;
    normalised_[0] = 1.0f;
    for (int lag = 1; lag <= maxLag_; ++lag) {
        tail += (double)window[lag + span - 1] * window[lag + span - 1] - (double)window[lag - 1] * window[lag - 1];
        double cross = 0.0;
        const float* shifted = window + lag;
        for (int i = 0; i < span; ++i)
            cross += (double)window[i] * shifted[i];
        const double d = std::max(0.0, head + tail - 2.0 * cross);
        running += d;
        normalised_[(size_t)lag] = running > 1.0e-12 ? (float)(d * lag / running) : 1.0f;
    }

    int best = -1;
    for (int lag = minLag_; lag < maxLag_; ++lag) {
        if (normalised_[(size_t)lag] < settings_.threshold) {
            while (lag + 1 < maxLag_ && normalised_[(size_t)lag + 1] < normalised_[(size_t)lag])
                ++lag;
            best = lag;
            break;
        }
    }
    if (best < 0) {
        float lowest = 2.0f;
        for (int lag = minLag_; lag < maxLag_; ++lag)
            if (normalised_[(size_t)lag] < lowest) {
                lowest = normalised_[(size_t)lag];
                best = lag;
            }
    }

    PitchFrame frame{};
    frame.timeSec = ((double)frameCount_ * settings_.hop + settings_.window / 2.0) / rate_;
    frame.rmsDb = levelDb(energy / size);
    if (best > 0) {
        double period = best;
        if (best > minLag_ && best < maxLag_) {
            const double left = normalised_[(size_t)best - 1];
            const double mid = normalised_[(size_t)best];
            const double right = normalised_[(size_t)best + 1];
            const double denominator = left - 2.0 * mid + right;
            if (std::abs(denominator) > 1.0e-12)
                period += std::clamp(0.5 * (left - right) / denominator, -1.0, 1.0);
        }
        frame.midi = (float)(69.0 + 12.0 * std::log2(rate_ / period / 440.0));
        frame.clarity = std::clamp(1.0f - normalised_[(size_t)best], 0.0f, 1.0f);
    }
    peakDb_ = std::max(peakDb_, frame.rmsDb);
    frame.voiced = best > 0 && frame.clarity >= settings_.minClarity && frame.rmsDb > settings_.absoluteGateDb &&
                   frame.rmsDb > peakDb_ - settings_.relativeGateDb;
    out.push_back(frame);
    ++frameCount_;
}

void PitchTracker::applyPeakGate(std::vector<PitchFrame>& frames, const PitchTrackerSettings& settings) {
    float peak = kSilenceDb;
    for (const auto& frame : frames)
        peak = std::max(peak, frame.rmsDb);
    for (auto& frame : frames)
        if (frame.rmsDb <= peak - settings.relativeGateDb)
            frame.voiced = false;
}

std::vector<PitchFrame> trackPitch(const float* mono, int numSamples, double sampleRate,
                                   const PitchTrackerSettings& settings) {
    PitchTracker tracker(settings);
    tracker.reset(sampleRate);
    std::vector<PitchFrame> frames;
    if (mono != nullptr && numSamples > 0) {
        frames.reserve((std::size_t)((double)numSamples / sampleRate * 100.0) + 4);
        tracker.push(mono, numSamples, frames);
    }
    PitchTracker::applyPeakGate(frames, settings);
    return frames;
}

} // namespace synth::melody
