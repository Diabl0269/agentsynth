// PartitionedConvolver.cpp -- direct-form head + uniform-partitioned overlap-save tail.
//
// Kernel h (L taps) = head h[0..P) + tail h[P..L). The head runs sample by sample (P MACs per
// sample), so the output has no latency. The tail is split into K = ceil((L - P) / P) partitions
// of P taps; partition m starts at tap P + mP, so its contribution to output sub-block j depends
// only on input sub-blocks j-1-m and earlier -- all complete by the time sub-block j starts. Each
// completed input sub-block b goes through one 2P FFT of [x_{b-1}, x_b] into the frequency-domain
// delay line; the tail for the next sub-block is then IFFT(sum_m X_{j-1-m} * H_m), last P samples.
//
// The delay line holds INPUT spectra only, so it is independent of the kernel: during a crossfade
// the old and the new kernel both read the same history, the new one is exact from its first
// sample, and the fade costs a second head and tail only for its duration.
#include "PartitionedConvolver.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {
constexpr int P = PartitionedConvolver::kPartitionSize;
constexpr int kFftOrder = 7; // 2P = 128
static_assert((1 << kFftOrder) == 2 * PartitionedConvolver::kPartitionSize);
constexpr float kSilence = 1.0e-9f;
} // namespace

void PartitionedConvolver::prepare(int kernelLength, int numChannels) {
    kernelLength_ = juce::jmax(P, kernelLength);
    numPartitions_ = juce::jmax(1, (kernelLength_ - P + P - 1) / P);
    numBins_ = P + 1;
    numChannels_ = juce::jlimit(1, kMaxChannels, numChannels);
    fft_ = std::make_unique<juce::dsp::FFT>(kFftOrder);
    fftWork_.assign((size_t)(4 * P), 0.0f);
    accum_.assign((size_t)numBins_, Complex{});
    scratchOld_.assign((size_t)P, 0.0f);
    scratchNew_.assign((size_t)P, 0.0f);
    for (auto& slot : slots_) {
        slot.headReversed.assign((size_t)P, 0.0f);
        slot.spectra.assign((size_t)(numPartitions_ * numBins_), Complex{});
    }
    channels_.resize((size_t)numChannels_);
    for (auto& s : channels_) {
        s.history.assign((size_t)(2 * P), 0.0f);
        s.fdl.assign((size_t)(numPartitions_ * numBins_), Complex{});
        for (auto& t : s.tail)
            t.assign((size_t)P, 0.0f);
    }
    active_ = 0;
    hasKernel_ = false;
    fading_ = false;
    reset();
}

void PartitionedConvolver::reset() noexcept {
    for (auto& s : channels_) {
        std::fill(s.history.begin(), s.history.end(), 0.0f);
        std::fill(s.fdl.begin(), s.fdl.end(), Complex{});
        for (int k = 0; k < 2; ++k) {
            std::fill(s.tail[k].begin(), s.tail[k].end(), 0.0f);
            s.tailValid[k] = true; // all-zero history: a zero tail is exact for any kernel
        }
        s.pos = 0;
        s.fdlHead = 0;
        s.silentRun = 0;
    }
}

/** Writes the kernel into the slot that is not live. With no live kernel yet it simply becomes
    live (there is nothing to fade from). Otherwise each channel's tail for the in-progress
    sub-block is recomputed lazily for the new slot on its next process() call. */
void PartitionedConvolver::setKernel(const float* kernel, int fadeSamples) noexcept {
    const int slotIndex = hasKernel_ ? 1 - active_ : active_;
    KernelSlot& slot = slots_[slotIndex];
    for (int t = 0; t < P; ++t)
        slot.headReversed[(size_t)t] = t < kernelLength_ ? kernel[P - 1 - t] : 0.0f;

    float* w = fftWork_.data();
    for (int m = 0; m < numPartitions_; ++m) {
        std::fill(w, w + 4 * P, 0.0f);
        for (int i = 0; i < P; ++i) {
            const int tap = P + m * P + i;
            w[i] = tap < kernelLength_ ? kernel[tap] : 0.0f;
        }
        fft_->performRealOnlyForwardTransform(w, true);
        const auto* bins = reinterpret_cast<const Complex*>(w);
        std::copy(bins, bins + numBins_, slot.spectra.begin() + (ptrdiff_t)(m * numBins_));
    }

    for (auto& s : channels_)
        s.tailValid[slotIndex] = false;

    if (!hasKernel_) {
        hasKernel_ = true;
        return;
    }
    fading_ = true;
    fadePos_ = 0;
    fadeLength_ = juce::jmax(fadeSamples, P);
}

void PartitionedConvolver::process(float* const* channels, int numChannels, int numSamples) noexcept {
    if (!hasKernel_ || numSamples <= 0)
        return;
    const int n = juce::jmin(numChannels, numChannels_);
    for (int c = 0; c < n; ++c)
        if (channels[c] != nullptr)
            processChannel(channels_[(size_t)c], channels[c], numSamples);

    if (fading_) {
        fadePos_ += numSamples;
        if (fadePos_ >= fadeLength_) {
            fading_ = false;
            active_ = 1 - active_;
        }
    }
}

/** Once a channel's input has been silent for longer than the kernel plus two sub-blocks, every
    sample of its history and delay line is (near) zero, so its output is zero for any kernel and
    the whole convolution is skipped -- an idle poly voice costs a silence scan only. */
void PartitionedConvolver::processChannel(ChannelState& s, float* data, int numSamples) noexcept {
    bool silent = true;
    for (int i = 0; i < numSamples && silent; ++i)
        silent = std::abs(data[i]) <= kSilence;
    if (silent && s.silentRun >= kernelLength_ + 2 * P) {
        std::fill(data, data + numSamples, 0.0f);
        for (int k = 0; k < 2; ++k) {
            std::fill(s.tail[k].begin(), s.tail[k].end(), 0.0f);
            s.tailValid[k] = true;
        }
        return;
    }
    s.silentRun = silent ? s.silentRun + numSamples : 0;

    const int oldSlot = active_;
    const int newSlot = 1 - active_;
    if (!s.tailValid[oldSlot])
        computeTail(s, oldSlot);
    if (fading_ && !s.tailValid[newSlot])
        computeTail(s, newSlot);

    const float* headOld = slots_[oldSlot].headReversed.data();
    const float* headNew = slots_[newSlot].headReversed.data();
    float* hist = s.history.data();
    float* yOld = scratchOld_.data();
    float* yNew = scratchNew_.data();
    int i = 0;
    while (i < numSamples) {
        const int chunk = juce::jmin(numSamples - i, P - s.pos);
        std::copy(data + i, data + i + chunk, hist + P + s.pos);
        // Output sample idx needs x[idx - t] = hist[P + idx - t], t = 0..P-1; with the head taps
        // stored reversed that is headReversed[t] * hist[idx + 1 + t] -- one vector multiply-add
        // across the chunk per tap.
        std::copy(s.tail[oldSlot].begin() + s.pos, s.tail[oldSlot].begin() + s.pos + chunk, yOld);
        for (int t = 0; t < P; ++t)
            juce::FloatVectorOperations::addWithMultiply(yOld, hist + s.pos + 1 + t, headOld[t], chunk);
        if (fading_) {
            std::copy(s.tail[newSlot].begin() + s.pos, s.tail[newSlot].begin() + s.pos + chunk, yNew);
            for (int t = 0; t < P; ++t)
                juce::FloatVectorOperations::addWithMultiply(yNew, hist + s.pos + 1 + t, headNew[t], chunk);
            for (int k = 0; k < chunk; ++k) {
                const float g = juce::jmin(1.0f, (float)(fadePos_ + i + k + 1) / (float)fadeLength_);
                data[i + k] = yOld[k] + (yNew[k] - yOld[k]) * g;
            }
        } else {
            std::copy(yOld, yOld + chunk, data + i);
        }
        s.pos += chunk;
        i += chunk;
        if (s.pos == P)
            finishSubBlock(s);
    }
}

/** A full input sub-block is in history: push its spectrum, slide the history, and compute the
    next sub-block's tail for every kernel that will be heard in it. */
void PartitionedConvolver::finishSubBlock(ChannelState& s) noexcept {
    float* w = fftWork_.data();
    std::copy(s.history.begin(), s.history.end(), w);
    std::fill(w + 2 * P, w + 4 * P, 0.0f);
    fft_->performRealOnlyForwardTransform(w, true);
    s.fdlHead = (s.fdlHead + 1) % numPartitions_;
    const auto* bins = reinterpret_cast<const Complex*>(w);
    std::copy(bins, bins + numBins_, s.fdl.begin() + (ptrdiff_t)(s.fdlHead * numBins_));

    std::copy(s.history.begin() + P, s.history.end(), s.history.begin());
    s.pos = 0;

    computeTail(s, active_);
    if (fading_)
        computeTail(s, 1 - active_);
    else
        s.tailValid[1 - active_] = false;
}

void PartitionedConvolver::computeTail(ChannelState& s, int slot) noexcept {
    std::fill(accum_.begin(), accum_.end(), Complex{});
    const Complex* spectra = slots_[slot].spectra.data();
    for (int m = 0; m < numPartitions_; ++m) {
        const int fdlIndex = (s.fdlHead - m + numPartitions_) % numPartitions_;
        const Complex* x = s.fdl.data() + fdlIndex * numBins_;
        const Complex* h = spectra + m * numBins_;
        for (int k = 0; k < numBins_; ++k)
            accum_[(size_t)k] += x[k] * h[k];
    }
    float* w = fftWork_.data();
    std::fill(w, w + 4 * P, 0.0f);
    std::copy(accum_.begin(), accum_.end(), reinterpret_cast<Complex*>(w));
    fft_->performRealOnlyInverseTransform(w);
    std::copy(w + P, w + 2 * P, s.tail[slot].begin());
    s.tailValid[slot] = true;
}

} // namespace synth
