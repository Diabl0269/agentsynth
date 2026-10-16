// PartitionedConvolver.h -- zero-latency FIR convolution for the Filter's Custom type: a
// direct-form head plus a uniform-partitioned FFT tail, one shared kernel across up to
// kMaxChannels channels (each with its own history), and a synchronous, sample-exact kernel swap
// with a linear crossfade. Everything is allocated in prepare(); process() and setKernel() are
// real-time safe.
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

    /** Allocates every buffer for kernels of `kernelLength` taps over `numChannels` channels (at
     *  most kMaxChannels) and clears all state. Not real-time safe. */
    void prepare(int kernelLength, int numChannels);
    bool isPrepared() const noexcept { return fft_ != nullptr; }

    /** Clears every channel's signal history (the kernel stays). Real-time safe. */
    void reset() noexcept;

    /** Installs `kernel` (kernelLength taps). The first call after prepare() takes effect at once;
     *  later calls crossfade linearly from the live kernel over max(fadeSamples, kPartitionSize)
     *  samples, starting with the next process() call. Precondition: !isCrossfading(). */
    void setKernel(const float* kernel, int fadeSamples) noexcept;
    bool isCrossfading() const noexcept { return fading_; }
    bool hasKernel() const noexcept { return hasKernel_; }

    /** Convolves each non-null `channels[c]` in place, c < numChannels <= the prepared count.
     *  A null entry is skipped (its history is left as it was). */
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    using Complex = std::complex<float>;

    struct KernelSlot {
        std::vector<float> headReversed; // P taps, h[P-1] first, so the head is a forward dot product
        std::vector<Complex> spectra;    // numPartitions * numBins, partition-major
    };

    struct ChannelState {
        std::vector<float> history; // 2P: previous sub-block, then the current one
        std::vector<Complex> fdl;   // frequency-domain delay line: numPartitions * numBins
        std::vector<float> tail[2]; // P: tail output for the current sub-block, per kernel slot
        bool tailValid[2] = {false, false};
        int pos = 0;       // samples of the current sub-block already in history
        int fdlHead = 0;   // fdl slot holding the newest spectrum
        int silentRun = 0; // consecutive silent input samples
    };

    void processChannel(ChannelState& s, float* data, int numSamples) noexcept;
    void finishSubBlock(ChannelState& s) noexcept;
    void computeTail(ChannelState& s, int slot) noexcept;

    std::unique_ptr<juce::dsp::FFT> fft_; // size 2P
    int kernelLength_ = 0;
    int numPartitions_ = 0;
    int numBins_ = 0;
    int numChannels_ = 0;
    KernelSlot slots_[2];
    std::vector<ChannelState> channels_;
    std::vector<float> fftWork_;                 // 4P floats
    std::vector<Complex> accum_;                 // numBins
    std::vector<float> scratchOld_, scratchNew_; // P: head + tail output for one chunk

    int active_ = 0;
    bool hasKernel_ = false;
    bool fading_ = false;
    int fadePos_ = 0;
    int fadeLength_ = 0;
};

} // namespace synth
