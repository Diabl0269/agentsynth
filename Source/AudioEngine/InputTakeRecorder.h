#pragma once

#include "../Timeline/TakeCaptureWriter.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace synth {

/**
 * Records audio takes straight from the device input, one slot per armed track, beside the graph rather than
 * through it. Each slot is a TakeCaptureWriter fed a mono channel or a stereo pair of the engine's per-block
 * input snapshot; all slots drain on one shared writer thread.
 *
 * A STRIP slot records a mixer channel instead of a device input: it owns a stereo buffer, allocated once in the
 * constructor and never reallocated, that a ChannelStripModule's live tap fills while the graph renders. The audio
 * thread pushes it right after the graph (pushStripSlots) and clears it, so a block the strip did not write records
 * silence. Because the buffer never moves, the tap may be armed while rendering.
 *
 * Threads: startSlot/startStripSlot/stopSlot/copyLivePeaks/stopAll on the message thread; push() and
 * pushStripSlots() on the audio thread (no allocation, no locks); latchHaltIfRateDiffers() from the prepare path.
 */
class InputTakeRecorder {
public:
    static constexpr int kMaxSlots = 16;
    static constexpr int kMaxInputChannels = 8;
    static constexpr int kStripBufferFrames = 8192; // frames per pass a strip slot can take; more record as silence
    using TakeResult = TakeCaptureWriter::TakeResult;

    explicit InputTakeRecorder(int ringCapacityFrames = TakeCaptureWriter::kDefaultRingCapacityFrames);
    ~InputTakeRecorder();

    /** MESSAGE THREAD. 0-based `firstChannel`, `numChannels` 1 or 2. Returns the slot, or -1 when the channels
     *  are out of range, every slot is busy, or the WAV cannot be opened. */
    int startSlot(int firstChannel, int numChannels, const juce::File& wavFile, const juce::File& peaksFile,
                  double sampleRate);
    /** MESSAGE THREAD. A stereo slot fed by a strip's live tap (getStripSlotBuffer). Returns the slot, or -1 when
     *  every slot is busy or the WAV cannot be opened. */
    int startStripSlot(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate);
    /** The slot's fixed tap buffer (2 x kStripBufferFrames), or null for an unknown slot. Valid for the recorder's
     *  life; arm a strip with it only while the slot is capturing. */
    juce::AudioBuffer<float>* getStripSlotBuffer(int slot) noexcept;
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

    /** AUDIO THREAD, right after the graph rendered the same pass push() was given: feeds each capturing strip slot
     *  its buffer's first `numSamples` frames, then clears them. */
    void pushStripSlots(int numSamples, juce::int64 blockStartTimelineSample) noexcept;

private:
    struct Slot {
        std::unique_ptr<TakeCaptureWriter> writer;
        std::atomic<int> firstChannel{0};
        std::atomic<bool> stripFed{false};
        juce::AudioBuffer<float> stripBuffer;
    };

    const Slot* slotAt(int slot) const noexcept;

    juce::TimeSliceThread writerThread_{"Input Take Writer"};
    std::array<Slot, kMaxSlots> slots_;
    std::atomic<int> capturingCount_{0};

    JUCE_DECLARE_NON_COPYABLE(InputTakeRecorder)
};

} // namespace synth
