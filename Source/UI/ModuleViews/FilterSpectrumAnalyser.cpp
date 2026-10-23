// FilterSpectrumAnalyser.cpp -- see FilterSpectrumAnalyser.h.
#include "FilterSpectrumAnalyser.h"
#include "FrequencyGrid.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

FilterSpectrumAnalyser::FilterSpectrumAnalyser(float smoothing)
    : smoothing_(smoothing)
    , fftData_((size_t)kFftSize * 2, 0.0f)
    , db_((size_t)kNumPoints, kMinDb) {}

void FilterSpectrumAnalyser::reset() { std::fill(db_.begin(), db_.end(), kMinDb); }

void FilterSpectrumAnalyser::analyse(const VisualBuffer& buffer, double sampleRate) {
    std::vector<float> samples((size_t)buffer.getSize(), 0.0f);
    buffer.copyTo(samples);
    analyse(samples.data(), (int)samples.size(), sampleRate);
}

/** The reading at each point is the FFT magnitude interpolated between its two neighbouring bins, scaled so a full
    scale sine reads about 0 dB, then limited to the dB range and smoothed. */
void FilterSpectrumAnalyser::analyse(const float* samples, int numSamples, double sampleRate) {
    std::fill(fftData_.begin(), fftData_.end(), 0.0f);
    const int copySize = std::min(numSamples, kFftSize);
    for (int i = 0; i < copySize; ++i)
        fftData_[(size_t)i] = samples[i];

    window_.multiplyWithWindowingTable(fftData_.data(), kFftSize);
    fft_.performFrequencyOnlyForwardTransform(fftData_.data());

    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(kFftSize);
    const float normFactor = 2.0f / static_cast<float>(kFftSize);

    for (int i = 0; i < kNumPoints; ++i) {
        const float freq = FrequencyGrid::indexToFreq(i, kNumPoints);
        const float exactBin = freq / binWidth;
        const int bin0 = static_cast<int>(exactBin);
        const int bin1 = bin0 + 1;
        const float frac = exactBin - static_cast<float>(bin0);

        float mag = 0.0f;
        if (bin0 >= 0 && bin1 < kFftSize / 2)
            mag = fftData_[(size_t)bin0] * (1.0f - frac) + fftData_[(size_t)bin1] * frac;
        else if (bin0 >= 0 && bin0 < kFftSize / 2)
            mag = fftData_[(size_t)bin0];
        mag *= normFactor;

        const float reading = 20.0f * std::log10(std::max(mag, 0.00001f));
        const float target = juce::jlimit(kMinDb, kMaxDb, reading);
        db_[(size_t)i] += smoothing_ * (target - db_[(size_t)i]);
    }
}

} // namespace synth::ui
