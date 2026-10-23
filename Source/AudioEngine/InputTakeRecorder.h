#pragma once

#include "../Timeline/TakeCaptureWriter.h"
#include <array>
#include <atomic>
#include <memory>

namespace synth {

/**
 * Records audio takes straight from the device input, one slot per armed track, beside the graph rather than
 * through it. Each slot is a TakeCaptureWriter fed a mono channel or a stereo pair of the engine's per-block
 * input snapshot; all slots drain on one shared writer thread.
 *
 * Threads: startSlot/stopSlot/copyLivePeaks/stopAll on the message thread; push() on the audio thread
 * (no allocation, no locks); latchHaltIfRateDiffers() from the prepare path.
 */
class InputTakeRecorder {
public:
    static constexpr int kMaxSlots = 16;
    static constexpr int kMaxInputChannels = 8;
    using TakeResult = TakeCaptureWriter::TakeResult;

    explicit InputTakeRecorder(int ringCapacityFrames = TakeCaptureWriter::kDefaultRingCapacityFrames);
    ~InputTakeRecorder();

    /** MESSAGE THREAD. 0-based `firstChannel`, `numChannels` 1 or 2. Returns the slot, or -1 when the channels
     *  are out of range, every slot is busy, or the WAV cannot be opened. */
    int startSlot(int firstChannel, int numChannels, const juce::File& wavFile, const juce::File& peaksFile,
                  double sampleRate);
    /** MESSAGE THREAD. Safe for any index, including -1 and a slot that is not capturing (ok == false). */
    TakeResult stopSlot(int slot);
    /** MESSAGE THREAD. Stops every capturing slot, finalising its files. */
    void stopAll();

    bool isSlotCapturing(int slot) const noexcept;
    bool isAnyCapturing() const noexcept { return capturingCount_.load(std::memory_order_relaxed) > 0; }
    int getSlotChannelCount(int slot) const noexcept;
    bool getSlotCaptureStart(int slot, juce::int64& timelineSample) const noexcept;
    /** MESSAGE THREAD. `out` is cleared for an unknown slot. */
    void copyLivePeaks(int slot, std::vector<std::pair<float, float>>& out) const;

    /** Prepare path: halts every capturing slot whose take rate differs from `sampleRate`. */
    void latchHaltIfRateDiffers(double sampleRate) noexcept;

    /** AUDIO THREAD. `inputs[c]` is input channel c (null = silence); channels past `numInputs` record silence. */
    void push(const float* const* inputs, int numInputs, int numSamples, juce::int64 blockStartTimelineSample,
              bool anchorValid) noexcept;

private:
    struct Slot {
        std::unique_ptr<TakeCaptureWriter> writer;
        std::atomic<int> firstChannel{0};
    };

    const Slot* slotAt(int slot) const noexcept;

    juce::TimeSliceThread writerThread_{"Input Take Writer"};
    std::array<Slot, kMaxSlots> slots_;
    std::atomic<int> capturingCount_{0};

    JUCE_DECLARE_NON_COPYABLE(InputTakeRecorder)
};

} // namespace synth
