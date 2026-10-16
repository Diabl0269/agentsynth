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

void PartitionedConvolver::prepare(int kernelLength, int numChannels, int numGroups) {
    kernelLength_ = juce::jmax(P, kernelLength);
    numPartitions_ = juce::jmax(1, (kernelLength_ - P + P - 1) / P);
    numBins_ = P + 1;
    numChannels_ = juce::jlimit(1, kMaxChannels, numChannels);
    numGroups_ = juce::jlimit(1, kMaxGroups, numGroups);
    fft_ = std::make_unique<juce::dsp::FFT>(kFftOrder);
    fftWork_.assign((size_t)(4 * P), 0.0f);
    accum_.assign((size_t)numBins_, Complex{});
    scratchOld_.assign((size_t)P, 0.0f);
    scratchNew_.assign((size_t)P, 0.0f);
    groups_.assign((size_t)numGroups_, KernelGroup{});
    for (auto& group : groups_)
        for (auto& slot : group.slots) {
            slot.headReversed.assign((size_t)P, 0.0f);
            slot.spectra.assign((size_t)(numPartitions_ * numBins_), Complex{});
        }
    channels_.resize((size_t)numChannels_);
    for (auto& s : channels_) {
        s.history.assign((size_t)(2 * P), 0.0f);
        s.fdl.assign((size_t)(numPartitions_ * numBins_), Complex{});
        for (auto& t : s.tail)
            t.assign((size_t)P, 0.0f);
        s.group = 0;
    }
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

/** Writes the kernel into the group's slot that is not live. With no live kernel yet it simply
    becomes live (there is nothing to fade from). Otherwise the tail for the in-progress sub-block of
    each channel on this group is recomputed lazily for the new slot on its next process() call;
    channels on other groups are untouched. */
void PartitionedConvolver::setKernel(int groupIndex, const float* kernel, int fadeSamples) noexcept {
    KernelGroup& group = groups_[(size_t)groupIndex];
    const int slotIndex = group.hasKernel ? 1 - group.active : group.active;
    KernelSlot& slot = group.slots[slotIndex];
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

    invalidateTails(groupIndex, slotIndex);

    if (!group.hasKernel) {
        group.hasKernel = true;
        return;
    }
    group.fading = true;
    group.fadePos = 0;
    group.fadeLength = juce::jmax(fadeSamples, P);
}

void PartitionedConvolver::setKernel(const float* kernel, int fadeSamples) noexcept {
    setKernel(0, kernel, fadeSamples);
}

bool PartitionedConvolver::isCrossfading(int group) const noexcept {
    return group >= 0 && group < numGroups_ && groups_[(size_t)group].fading;
}

bool PartitionedConvolver::hasKernel(int group) const noexcept {
    return group >= 0 && group < numGroups_ && groups_[(size_t)group].hasKernel;
}

void PartitionedConvolver::invalidateTails(int group, int slot) noexcept {
    for (auto& s : channels_)
        if (s.group == group)
            s.tailValid[slot] = false;
}

/** Copies the source's live slot (head and spectra, a few tens of kilobytes) over the destination's live
    slot and cancels any fade there, so a channel moved onto `to` hears exactly what it heard on `from`.
    A fade still running on `from` is not carried over: the copy is the kernel it fades away from. */
void PartitionedConvolver::copyLiveKernel(int from, int to) noexcept {
    if (from == to || from < 0 || to < 0 || from >= numGroups_ || to >= numGroups_)
        return;
    const KernelGroup& src = groups_[(size_t)from];
    KernelGroup& dst = groups_[(size_t)to];
    const KernelSlot& in = src.slots[src.active];
    KernelSlot& out = dst.slots[dst.active];
    std::copy(in.headReversed.begin(), in.headReversed.end(), out.headReversed.begin());
    std::copy(in.spectra.begin(), in.spectra.end(), out.spectra.begin());
    dst.hasKernel = src.hasKernel;
    dst.fading = false;
    invalidateTails(to, 0);
    invalidateTails(to, 1);
}

/** The delay line holds input spectra only, so a channel can change kernels between any two samples: its
    tails are recomputed from the new group's kernels on the next process() call. */
void PartitionedConvolver::setChannelGroup(int channel, int group) noexcept {
    if (channel < 0 || channel >= numChannels_ || group < 0 || group >= numGroups_)
        return;
    ChannelState& s = channels_[(size_t)channel];
    if (s.group == group)
        return;
    s.group = group;
    s.tailValid[0] = s.tailValid[1] = false;
}

int PartitionedConvolver::getChannelGroup(int channel) const noexcept {
    return channel >= 0 && channel < numChannels_ ? channels_[(size_t)channel].group : 0;
}

bool PartitionedConvolver::isChannelIdle(int channel) const noexcept {
    return channel >= 0 && channel < numChannels_ && channels_[(size_t)channel].silentRun >= kernelLength_ + 2 * P;
}

/** Each group's fade advances once per call, after every channel on it has used this block's fade
    positions, so all channels of a group (both legs of a voice) cross over sample-aligned. */
void PartitionedConvolver::process(float* const* channels, int numChannels, int numSamples) noexcept {
    if (numSamples <= 0)
        return;
    const int n = juce::jmin(numChannels, numChannels_);
    for (int c = 0; c < n; ++c)
        if (channels[c] != nullptr && groups_[(size_t)channels_[(size_t)c].group].hasKernel)
            processChannel(channels_[(size_t)c], channels[c], numSamples);

    for (auto& group : groups_) {
        if (!group.fading)
            continue;
        group.fadePos += numSamples;
        if (group.fadePos >= group.fadeLength) {
            group.fading = false;
            group.active = 1 - group.active;
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

    const KernelGroup& group = groups_[(size_t)s.group];
    const bool fading = group.fading;
    const int oldSlot = group.active;
    const int newSlot = 1 - group.active;
    if (!s.tailValid[oldSlot])
        computeTail(s, oldSlot);
    if (fading && !s.tailValid[newSlot])
        computeTail(s, newSlot);

    const float* headOld = group.slots[oldSlot].headReversed.data();
    const float* headNew = group.slots[newSlot].headReversed.data();
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
        if (fading) {
            std::copy(s.tail[newSlot].begin() + s.pos, s.tail[newSlot].begin() + s.pos + chunk, yNew);
            for (int t = 0; t < P; ++t)
                juce::FloatVectorOperations::addWithMultiply(yNew, hist + s.pos + 1 + t, headNew[t], chunk);
            for (int k = 0; k < chunk; ++k) {
                const float g = juce::jmin(1.0f, (float)(group.fadePos + i + k + 1) / (float)group.fadeLength);
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

    const KernelGroup& group = groups_[(size_t)s.group];
    computeTail(s, group.active);
    if (group.fading)
        computeTail(s, 1 - group.active);
    else
        s.tailValid[1 - group.active] = false;
}

void PartitionedConvolver::computeTail(ChannelState& s, int slot) noexcept {
    std::fill(accum_.begin(), accum_.end(), Complex{});
    const Complex* spectra = groups_[(size_t)s.group].slots[slot].spectra.data();
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
