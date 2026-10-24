#pragma once

#include "PeaksFile.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <utility>
#include <vector>

namespace synth {

/**
 * One audio take on its way to disk: an SPSC ring the audio thread pushes into, a writer-thread client that
 * drains it into a 32-bit float WAV while accumulating peaks, and the .agpk sidecar written at stop().
 * The Rec Tap (whole mix) and the engine's input recorder (one per armed input) both capture through it.
 *
 * Threads: push() on the audio thread (no allocation, no locks); start()/stop()/copyLivePeaks() on the
 * message thread; the drain on `writerThread`, which the owner provides and must outlive this object.
 */
class TakeCaptureWriter {
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kDefaultRingCapacityFrames = 1 << 17;
    static constexpr int kPeakBucketSize = 256;
    static constexpr int kDrainChunkFrames = 8192;
    /** The take files' sample format: 32 bits makes an IEEE-float WAV, a bit-exact copy of what was captured. */
    static constexpr int kWavBitDepth = 32;
    static juce::String describeTakeFormat() { return "WAV 32-bit float"; }

    /** What a finished take amounts to. `lengthSamples` is exactly the WAV's length; `overran` means samples
     *  were dropped; the capture-start fields are the transport sample of frame 0, valid only when
     *  `captureStartValid` (see RecordTapModule for the full contract). */
    struct TakeResult {
        bool ok = false;
        juce::int64 lengthSamples = 0;
        bool overran = false;
        bool captureStartValid = false;
        juce::int64 captureStartTimelineSample = 0;
        int captureStartBlockOffset = 0;
    };

    /** `allocateRingNow` false defers the ring to the first start(), so an idle writer costs no memory. */
    TakeCaptureWriter(juce::TimeSliceThread& writerThread, int ringCapacityFrames = kDefaultRingCapacityFrames,
                      bool allocateRingNow = true);
    ~TakeCaptureWriter();

    /** MESSAGE THREAD. False, changing nothing, while capturing, for a nonsense format, or when the WAV cannot
     *  be opened. */
    bool start(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate, int numChannels);
    /** MESSAGE THREAD. A no-op returning `ok == false` when not capturing. */
    TakeResult stop();

    /** AUDIO THREAD. A null or missing channel records silence; does nothing unless capturing. */
    void push(const float* const* channels, int numChannels, int numSamples, juce::int64 blockStartTimelineSample,
              bool anchorValid) noexcept;
    /** AUDIO THREAD helper: whether the next push() still needs a frame-0 anchor. */
    bool needsAnchor() const noexcept { return !captureStartValid_.load(std::memory_order_relaxed); }

    /** Any thread except the audio thread: stops pushes for good when `sampleRate` differs from the take's. */
    void latchHaltIfRateDiffers(double sampleRate) noexcept;

    bool isCapturing() const noexcept { return capturing_.load(std::memory_order_acquire); }
    int getCaptureChannels() const noexcept { return captureChannels_.load(std::memory_order_relaxed); }
    juce::int64 getCapturedSamples() const noexcept { return capturedFrames_.load(std::memory_order_relaxed); }
    bool hadOverrun() const noexcept { return overrun_.load(std::memory_order_relaxed); }
    bool wasHaltedByFormatChange() const noexcept { return captureHalted_.load(std::memory_order_acquire); }
    /** False, leaving `timelineSample` untouched, until the first block was captured. */
    bool getCaptureStartTimelineSample(juce::int64& timelineSample) const noexcept;

    /** MESSAGE THREAD. The complete buckets so far this take, channel-interleaved; `out` is replaced. */
    void copyLivePeaks(std::vector<std::pair<float, float>>& out) const;

private:
    struct WriterClient : public juce::TimeSliceClient {
        explicit WriterClient(TakeCaptureWriter& o)
            : owner(o) {}
        int useTimeSlice() override;
        TakeCaptureWriter& owner;
    };

    void writeToRing(const float* const* channels, int numChannels, int numSamples,
                     juce::int64 blockStartTimelineSample, bool anchorValid) noexcept;
    bool drainOnce();
    bool writePeaksFile() const;

    juce::TimeSliceThread& writerThread_;
    const int ringCapacityFrames_;
    juce::AbstractFifo ring_;
    std::vector<float> ringStorage_;
    juce::AudioBuffer<float> drainScratch_;

    std::atomic<bool> capturing_{false};
    std::atomic<int> pushesInFlight_{0};
    std::atomic<bool> overrun_{false};
    std::atomic<juce::int64> capturedFrames_{0};
    std::atomic<juce::int64> writtenFrames_{0};
    std::atomic<int> captureChannels_{kMaxChannels};
    std::atomic<double> captureSampleRate_{0.0};
    std::atomic<bool> captureHalted_{false};
    std::atomic<bool> captureStartValid_{false};
    std::atomic<juce::int64> captureStartTimelineSample_{0};
    std::atomic<int> captureStartBlockOffset_{0};

    juce::File peaksFile_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;
    mutable juce::CriticalSection peaksLock_;
    PeaksFile::Accumulator peaksAccumulator_{kPeakBucketSize, kMaxChannels};
    WriterClient writerClient_{*this};

    JUCE_DECLARE_NON_COPYABLE(TakeCaptureWriter)
};

} // namespace synth
