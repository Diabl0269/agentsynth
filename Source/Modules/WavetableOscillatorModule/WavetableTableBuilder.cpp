// Out-of-line bodies for wavetable::TableBuilder (FRO73). Split out of
// WavetableOscillatorModule.h, where these ran inline as a nested class; moved here verbatim
// (only the class qualifier was added and `inline` dropped) as part of getting that header
// under the file-size cap. No behavior change.

#include "WavetableTableBuilder.h"

namespace wavetable {

TableBuilder::TableBuilder() {
    for (int order = kMinOrder; order <= kMaxOrder; ++order)
        ffts[(size_t)(order - kMinOrder)] = std::make_unique<juce::dsp::FFT>(order);

    work.resize(2 * kFrameSize, 0.0f);

    // A unit fundamental in the spectrum must come out as a unit-amplitude sine.
    for (int m = 0; m < kNumMips; ++m) {
        const int len = mipLength(m);
        std::fill(work.begin(), work.begin() + 2 * len, 0.0f);
        work[3] = -1.0f; // bin 1, imaginary part
        fftFor(m).performRealOnlyInverseTransform(work.data());

        float peak = 0.0f;
        for (int i = 0; i < len; ++i)
            peak = std::max(peak, std::abs(work[(size_t)i]));
        invScale[m] = peak > 0.0f ? 1.0f / peak : 1.0f;
    }
}

std::unique_ptr<Wavetable> TableBuilder::allocate(int numFrames, const juce::String& name,
                                                  const juce::String& sourcePath) {
    auto wt = std::make_unique<Wavetable>();
    wt->numFrames = std::max(1, numFrames);
    wt->name = name;
    wt->sourcePath = sourcePath;
    for (int m = 0; m < kNumMips; ++m)
        wt->mips[(size_t)m].assign((size_t)wt->numFrames * (size_t)mipLength(m), 0.0f);
    return wt;
}

void TableBuilder::renderFrame(Wavetable& wt, int frame, const float* cosAmp, const float* sinAmp, int maxHarmonic) {
    for (int m = 0; m < kNumMips; ++m) {
        const int len = mipLength(m);
        const int limit = std::min(mipHarmonicLimit(m), maxHarmonic);

        std::fill(work.begin(), work.begin() + 2 * len, 0.0f);
        for (int h = 1; h <= limit; ++h) {
            work[(size_t)(2 * h)] = cosAmp[h];
            work[(size_t)(2 * h + 1)] = -sinAmp[h];
        }

        fftFor(m).performRealOnlyInverseTransform(work.data());

        float* dst = wt.mips[(size_t)m].data() + (size_t)frame * (size_t)len;
        const float scale = invScale[m];
        for (int i = 0; i < len; ++i)
            dst[i] = work[(size_t)i] * scale;
    }
}

void TableBuilder::analyseCycle(const float* cycle, int length, float* cosAmp, float* sinAmp) {
    const int len = juce::jlimit(mipLength(kNumMips - 1), kFrameSize, length);
    const int order = orderForLength(len);
    const int usable = (1 << order);

    std::fill(work.begin(), work.end(), 0.0f);
    std::copy_n(cycle, usable, work.begin());
    fftByOrder(order).performRealOnlyForwardTransform(work.data(), true);

    std::fill_n(cosAmp, kMaxHarmonic + 1, 0.0f);
    std::fill_n(sinAmp, kMaxHarmonic + 1, 0.0f);

    const int topHarmonic = std::min(kMaxHarmonic, usable / 2 - 1);
    for (int h = 1; h <= topHarmonic; ++h) {
        cosAmp[h] = work[(size_t)(2 * h)];
        sinAmp[h] = -work[(size_t)(2 * h + 1)];
    }
}

void TableBuilder::collapseToSinePhase(float* cosAmp, float* sinAmp) {
    for (int h = 1; h <= kMaxHarmonic; ++h) {
        const float mag = std::sqrt(cosAmp[h] * cosAmp[h] + sinAmp[h] * sinAmp[h]);
        cosAmp[h] = 0.0f;
        sinAmp[h] = mag;
    }
}

void TableBuilder::normalise(Wavetable& wt) {
    float peak = 0.0f;
    for (float v : wt.mips[0])
        peak = std::max(peak, std::abs(v));
    if (peak <= 1.0e-9f)
        return;

    const float gain = 1.0f / peak;
    for (auto& mip : wt.mips)
        for (float& v : mip)
            v *= gain;
}

int TableBuilder::orderForLength(int length) {
    int order = kMinOrder;
    while (order < kMaxOrder && (1 << (order + 1)) <= length)
        ++order;
    return order;
}

} // namespace wavetable
