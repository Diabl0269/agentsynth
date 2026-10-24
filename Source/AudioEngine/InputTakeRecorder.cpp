#include "InputTakeRecorder.h"

namespace synth {

// Every slot's writer exists from construction, but its ring is only allocated on the slot's first take, so an
// engine that never records from its input costs one idle thread object and no ring memory.
InputTakeRecorder::InputTakeRecorder(int ringCapacityFrames) {
    for (auto& slot : slots_) {
        slot.writer = std::make_unique<TakeCaptureWriter>(writerThread_, ringCapacityFrames, /*allocateRingNow*/ false);
        slot.stripBuffer.setSize(TakeCaptureWriter::kMaxChannels, kStripBufferFrames);
        slot.stripBuffer.clear();
    }
}

// Writers detach from the shared thread before it stops; the slots (declared after the thread) die first anyway.
InputTakeRecorder::~InputTakeRecorder() {
    stopAll();
    writerThread_.stopThread(2000);
}

const InputTakeRecorder::Slot* InputTakeRecorder::slotAt(int slot) const noexcept {
    if (slot < 0 || slot >= kMaxSlots)
        return nullptr;
    return &slots_[(std::size_t)slot];
}

// The channel selection is stored before the writer arms (its release-store of the armed flag publishes it), so
// the audio thread never reads a selection from the previous take on this slot.
int InputTakeRecorder::startSlot(int firstChannel, int numChannels, const juce::File& wavFile,
                                 const juce::File& peaksFile, double sampleRate) {
    if (firstChannel < 0 || numChannels < 1 || numChannels > TakeCaptureWriter::kMaxChannels ||
        firstChannel + numChannels > kMaxInputChannels)
        return -1;
    for (int index = 0; index < kMaxSlots; ++index) {
        auto& slot = slots_[(std::size_t)index];
        if (slot.writer->isCapturing())
            continue;
        slot.firstChannel.store(firstChannel, std::memory_order_relaxed);
        slot.stripFed.store(false, std::memory_order_relaxed);
        if (!slot.writer->start(wavFile, peaksFile, sampleRate, numChannels))
            return -1;
        capturingCount_.fetch_add(1, std::memory_order_relaxed);
        return index;
    }
    return -1;
}

// The buffer is cleared before the writer arms, so the first block never carries a previous take's tail.
int InputTakeRecorder::startStripSlot(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate) {
    for (int index = 0; index < kMaxSlots; ++index) {
        auto& slot = slots_[(std::size_t)index];
        if (slot.writer->isCapturing())
            continue;
        slot.stripBuffer.clear();
        slot.stripFed.store(true, std::memory_order_relaxed);
        if (!slot.writer->start(wavFile, peaksFile, sampleRate, TakeCaptureWriter::kMaxChannels)) {
            slot.stripFed.store(false, std::memory_order_relaxed);
            return -1;
        }
        capturingCount_.fetch_add(1, std::memory_order_relaxed);
        return index;
    }
    return -1;
}

juce::AudioBuffer<float>* InputTakeRecorder::getStripSlotBuffer(int slot) noexcept {
    if (slot < 0 || slot >= kMaxSlots)
        return nullptr;
    return &slots_[(std::size_t)slot].stripBuffer;
}

InputTakeRecorder::TakeResult InputTakeRecorder::stopSlot(int slot) {
    const auto* found = slotAt(slot);
    if (found == nullptr || !found->writer->isCapturing())
        return {};
    capturingCount_.fetch_sub(1, std::memory_order_relaxed);
    return found->writer->stop();
}

void InputTakeRecorder::stopAll() {
    for (int index = 0; index < kMaxSlots; ++index)
        stopSlot(index);
}

bool InputTakeRecorder::isSlotCapturing(int slot) const noexcept {
    const auto* found = slotAt(slot);
    return found != nullptr && found->writer->isCapturing();
}

int InputTakeRecorder::getSlotChannelCount(int slot) const noexcept {
    const auto* found = slotAt(slot);
    return found != nullptr ? found->writer->getCaptureChannels() : 0;
}

bool InputTakeRecorder::getSlotCaptureStart(int slot, juce::int64& timelineSample) const noexcept {
    const auto* found = slotAt(slot);
    return found != nullptr && found->writer->getCaptureStartTimelineSample(timelineSample);
}

void InputTakeRecorder::copyLivePeaks(int slot, std::vector<std::pair<float, float>>& out) const {
    const auto* found = slotAt(slot);
    if (found == nullptr) {
        out.clear();
        return;
    }
    found->writer->copyLivePeaks(out);
}

void InputTakeRecorder::latchHaltIfRateDiffers(double sampleRate) noexcept {
    for (auto& slot : slots_)
        slot.writer->latchHaltIfRateDiffers(sampleRate);
}

// One pass's slice of the input for every capturing slot. A selected channel the device did not deliver this pass
// is pushed as silence rather than skipped, so frame f of every take stays at its anchor + f.
void InputTakeRecorder::push(const float* const* inputs, int numInputs, int numSamples,
                             juce::int64 blockStartTimelineSample, bool anchorValid) noexcept {
    if (!isAnyCapturing() || numSamples <= 0)
        return;
    const int available = inputs != nullptr ? numInputs : 0;
    for (auto& slot : slots_) {
        if (!slot.writer->isCapturing() || slot.stripFed.load(std::memory_order_relaxed))
            continue;
        const int first = slot.firstChannel.load(std::memory_order_relaxed);
        const float* pair[TakeCaptureWriter::kMaxChannels] = {first < available ? inputs[first] : nullptr,
                                                              first + 1 < available ? inputs[first + 1] : nullptr};
        slot.writer->push(pair, TakeCaptureWriter::kMaxChannels, numSamples, blockStartTimelineSample, anchorValid);
    }
}

// A pass longer than the buffer records its first kStripBufferFrames frames and silence for the rest, so frame f of the
// take stays at its anchor + f. The anchor is the pass's block start, like a device slot's.
void InputTakeRecorder::pushStripSlots(int numSamples, juce::int64 blockStartTimelineSample) noexcept {
    if (!isAnyCapturing() || numSamples <= 0)
        return;
    const int filled = juce::jmin(numSamples, kStripBufferFrames);
    for (auto& slot : slots_) {
        if (!slot.writer->isCapturing() || !slot.stripFed.load(std::memory_order_relaxed))
            continue;
        const float* const channels[TakeCaptureWriter::kMaxChannels] = {slot.stripBuffer.getReadPointer(0),
                                                                        slot.stripBuffer.getReadPointer(1)};
        slot.writer->push(channels, TakeCaptureWriter::kMaxChannels, filled, blockStartTimelineSample,
                          /*anchorValid*/ true);
        if (numSamples > filled)
            slot.writer->push(nullptr, 0, numSamples - filled, blockStartTimelineSample + filled,
                              /*anchorValid*/ false);
        slot.stripBuffer.clear(0, filled);
    }
}

} // namespace synth
