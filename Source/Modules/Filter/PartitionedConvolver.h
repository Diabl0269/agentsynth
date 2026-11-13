// PartitionedConvolver.h -- zero-latency FIR convolution for the Filter's Custom type: a
// direct-form head plus a uniform-partitioned FFT tail, up to kMaxChannels channels (each with its
// own history) reading one of up to kMaxGroups kernels ("groups": one shared, or one per poly
// voice), and a synchronous, sample-exact kernel swap with a linear crossfade per group.
// Everything is allocated in prepare(); process(), setKernel() and the group calls are real-time safe.
#pragma once

#include <complex>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace synth {

class PartitionedConvolver {
public:
    static constexpr int kPartitionSize = 64; // head length and FFT partition size (P)
    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxGroups = 9;

    /** Allocates every buffer for kernels of `kernelLength` taps over `numChannels` channels (at
     *  most kMaxChannels) and `numGroups` kernels (at most kMaxGroups), and clears all state. Every
     *  channel starts on group 0. Not real-time safe. */
    void prepare(int kernelLength, int numChannels, int numGroups = 1);
    bool isPrepared() const noexcept { return fft_ != nullptr; }

    /** Clears every channel's signal history (the kernels and channel groups stay). Real-time safe. */
    void reset() noexcept;

    /** Installs `kernel` (kernelLength taps) as group `group`'s kernel. The group's first kernel takes
     *  effect at once; later ones crossfade linearly from the live kernel over max(fadeSamples,
     *  kPartitionSize) samples, starting with the next process() call. Precondition: !isCrossfading(group). */
    void setKernel(int group, const float* kernel, int fadeSamples) noexcept;
    /** Group 0. */
    void setKernel(const float* kernel, int fadeSamples) noexcept;
    bool isCrossfading(int group = 0) const noexcept;
    bool hasKernel(int group = 0) const noexcept;

    /** Makes group `to`'s live kernel a copy of group `from`'s, at once (no fade). Meant for a group no
     *  audible channel reads yet. */
    void copyLiveKernel(int from, int to) noexcept;
    /** Points `channel` at group `group`'s kernel from its next sample on. */
    void setChannelGroup(int channel, int group) noexcept;
    int getChannelGroup(int channel) const noexcept;
    /** True while `channel`'s input has been silent long enough that its output is zero for any kernel. */
    bool isChannelIdle(int channel) const noexcept;

    /** Convolves each non-null `channels[c]` in place, c < numChannels <= the prepared count.
     *  A null entry is skipped (its history is left as it was). */
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    using Complex = std::complex<float>;

    struct KernelSlot {
        std::vector<float> headReversed; // P taps, h[P-1] first, so the head is a forward dot product
        std::vector<Complex> spectra;    // numPartitions * numBins, partition-major
    };

    struct KernelGroup {
        KernelSlot slots[2];
        int active = 0;
        bool hasKernel = false;
        bool fading = false;
        int fadePos = 0;
        int fadeLength = 0;
    };

    struct ChannelState {
        std::vector<float> history; // 2P: previous sub-block, then the current one
        std::vector<Complex> fdl;   // frequency-domain delay line: numPartitions * numBins
        std::vector<float> tail[2]; // P: tail output for the current sub-block, per kernel slot
        bool tailValid[2] = {false, false};
        int pos = 0;       // samples of the current sub-block already in history
        int fdlHead = 0;   // fdl slot holding the newest spectrum
        int silentRun = 0; // consecutive silent input samples
        int group = 0;     // which kernel group this channel reads
    };

    void processChannel(ChannelState& s, float* data, int numSamples) noexcept;
    void finishSubBlock(ChannelState& s) noexcept;
    void computeTail(ChannelState& s, int slot) noexcept;
    void invalidateTails(int group, int slot) noexcept;

    std::unique_ptr<juce::dsp::FFT> fft_; // size 2P
    int kernelLength_ = 0;
    int numPartitions_ = 0;
    int numBins_ = 0;
    int numChannels_ = 0;
    int numGroups_ = 0;
    std::vector<KernelGroup> groups_;
    std::vector<ChannelState> channels_;
    std::vector<float> fftWork_;                 // 4P floats
    std::vector<Complex> accum_;                 // numBins
    std::vector<float> scratchOld_, scratchNew_; // P: head + tail output for one chunk
};

} // namespace synth
