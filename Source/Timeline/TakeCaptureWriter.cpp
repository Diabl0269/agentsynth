#include "TakeCaptureWriter.h"
#include <thread>

namespace synth {

// The ring is the only thing the audio thread touches. It is allocated once and never resized: either here, or
// on the first start() (before capturing_ is released), so the audio thread can never see it change size.
TakeCaptureWriter::TakeCaptureWriter(juce::TimeSliceThread& writerThread, int ringCapacityFrames, bool allocateRingNow)
    : writerThread_(writerThread)
    , ringCapacityFrames_(juce::jmax(1, ringCapacityFrames))
    , ring_(juce::jmax(1, ringCapacityFrames)) {
    if (allocateRingNow) {
        ringStorage_.assign((std::size_t)ringCapacityFrames_ * (std::size_t)kMaxChannels, 0.0f);
        drainScratch_.setSize(kMaxChannels, kDrainChunkFrames);
    }
}

// Finalises an in-flight take rather than abandoning a half-written WAV, and detaches the writer client before
// any member it touches is destroyed. The thread itself belongs to the owner.
TakeCaptureWriter::~TakeCaptureWriter() {
    if (isCapturing())
        stop();
}

// The halt rides with the armed check and is read once per block: a take stops at a sample-rate boundary and
// not one block of the new rate may go in (see latchHaltIfRateDiffers). The anchor is taken before the push and
// only on the first captured block, so frame 0 of the file is exactly `blockStartTimelineSample`.
//
// The in-flight count brackets the armed check (sequentially consistent, paired with stop()): once stop() has
// disarmed and seen the count at zero, no push is still writing the ring, so the final drain and the next
// start()'s ring reset never overlap a write.
void TakeCaptureWriter::push(const float* const* channels, int numChannels, int numSamples,
                             juce::int64 blockStartTimelineSample, bool anchorValid) noexcept {
    if (numSamples <= 0)
        return;
    pushesInFlight_.fetch_add(1, std::memory_order_seq_cst);
    if (capturing_.load(std::memory_order_seq_cst) && !captureHalted_.load(std::memory_order_acquire))
        writeToRing(channels, numChannels, numSamples, blockStartTimelineSample, anchorValid);
    pushesInFlight_.fetch_sub(1, std::memory_order_seq_cst);
}

void TakeCaptureWriter::writeToRing(const float* const* channels, int numChannels, int numSamples,
                                    juce::int64 blockStartTimelineSample, bool anchorValid) noexcept {
    const int captureChannels = juce::jlimit(1, kMaxChannels, captureChannels_.load(std::memory_order_relaxed));
    const int availableChannels = channels != nullptr ? juce::jmin(captureChannels, numChannels) : 0;

    if (anchorValid && !captureStartValid_.load(std::memory_order_relaxed)) {
        captureStartTimelineSample_.store(blockStartTimelineSample, std::memory_order_relaxed);
        captureStartBlockOffset_.store(0, std::memory_order_relaxed);
        captureStartValid_.store(true, std::memory_order_release);
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    ring_.prepareToWrite(numSamples, start1, size1, start2, size2);
    const int writable = size1 + size2;
    // Full (or nearly): keep what fits and drop the rest rather than block the audio thread.
    if (writable < numSamples)
        overrun_.store(true, std::memory_order_relaxed);

    int sourceFrame = 0;
    const int regionStarts[2] = {start1, start2};
    const int regionSizes[2] = {size1, size2};
    for (int region = 0; region < 2; ++region) {
        for (int i = 0; i < regionSizes[region]; ++i, ++sourceFrame) {
            float* dest = ringStorage_.data() + (std::size_t)(regionStarts[region] + i) * (std::size_t)kMaxChannels;
            for (int channel = 0; channel < kMaxChannels; ++channel) {
                const float* src = channel < availableChannels ? channels[channel] : nullptr;
                dest[channel] = src != nullptr ? src[sourceFrame] : 0.0f;
            }
        }
    }
    if (writable > 0) {
        ring_.finishedWrite(writable);
        capturedFrames_.fetch_add((juce::int64)writable, std::memory_order_relaxed);
    }
}

// A take's WAV header is fixed at start()'s rate, so a block rendered at any other rate cannot honestly go into
// it. The owner calls this on every prepare (a graph node's prepareToPlay, the engine's format change), so the
// push stops before the first new-rate block. capturing_ stays true: the message thread still commits the short
// take through stop(), which would no-op on a disarmed writer.
void TakeCaptureWriter::latchHaltIfRateDiffers(double sampleRate) noexcept {
    const double captureRate = captureSampleRate_.load(std::memory_order_relaxed);
    if (sampleRate > 0.0 && captureRate > 0.0 && sampleRate != captureRate &&
        capturing_.load(std::memory_order_acquire))
        captureHalted_.store(true, std::memory_order_release);
}

bool TakeCaptureWriter::getCaptureStartTimelineSample(juce::int64& timelineSample) const noexcept {
    if (!captureStartValid_.load(std::memory_order_acquire))
        return false;
    timelineSample = captureStartTimelineSample_.load(std::memory_order_relaxed);
    return true;
}

bool TakeCaptureWriter::start(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate,
                              int numChannels) {
    if (isCapturing())
        return false; // a second start would interleave two takes into one file
    if (!(sampleRate > 0.0) || numChannels < 1 || numChannels > kMaxChannels)
        return false;

    wavFile.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(wavFile.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    stream->setPosition(0);
    stream->truncate();

    // bitDepth 32 makes this an IEEE-float WAV, so the take is a bit-exact copy of what was pushed.
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int)numChannels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns the stream from here

    if (ringStorage_.empty()) {
        ringStorage_.assign((std::size_t)ringCapacityFrames_ * (std::size_t)kMaxChannels, 0.0f);
        drainScratch_.setSize(kMaxChannels, kDrainChunkFrames);
    }

    // Every piece of take state is reset BEFORE arming, so the audio thread can only see this take's values.
    ring_.reset();
    overrun_.store(false, std::memory_order_relaxed);
    capturedFrames_.store(0, std::memory_order_relaxed);
    writtenFrames_.store(0, std::memory_order_relaxed);
    captureStartValid_.store(false, std::memory_order_relaxed);
    captureStartTimelineSample_.store(0, std::memory_order_relaxed);
    captureStartBlockOffset_.store(0, std::memory_order_relaxed);
    captureSampleRate_.store(sampleRate, std::memory_order_relaxed);
    captureHalted_.store(false, std::memory_order_relaxed);
    {
        // A fresh accumulator rather than reset(): a mono take after a stereo one needs a different shape.
        const juce::ScopedLock lock(peaksLock_);
        peaksAccumulator_ = PeaksFile::Accumulator(kPeakBucketSize, numChannels);
    }

    peaksFile_ = peaksFile;
    writer_ = std::move(writer);
    captureChannels_.store(numChannels, std::memory_order_relaxed);

    if (!writerThread_.isThreadRunning())
        writerThread_.startThread();
    writerThread_.addTimeSliceClient(&writerClient_);

    capturing_.store(true, std::memory_order_release);
    return true;
}

// Disarm first and wait out any push that saw the take armed (a push is a short copy, never a wait), then detach
// the client: removeTimeSliceClient blocks until any in-flight drain returns, which makes this thread the ring's
// sole reader for the rest of the function. The length is the WRITTEN count, which is exactly the file's.
TakeCaptureWriter::TakeResult TakeCaptureWriter::stop() {
    if (!isCapturing())
        return {};

    capturing_.store(false, std::memory_order_seq_cst);
    while (pushesInFlight_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();
    writerThread_.removeTimeSliceClient(&writerClient_);

    while (drainOnce()) {
    }
    {
        const juce::ScopedLock lock(peaksLock_);
        peaksAccumulator_.flushPartial();
    }

    writer_.reset(); // flushes and closes the stream; the WAV header is written by the destructor

    TakeResult result;
    result.lengthSamples = writtenFrames_.load(std::memory_order_relaxed);
    result.overran = overrun_.load(std::memory_order_relaxed);
    result.captureStartValid = captureStartValid_.load(std::memory_order_acquire);
    result.captureStartTimelineSample = captureStartTimelineSample_.load(std::memory_order_relaxed);
    result.captureStartBlockOffset = captureStartBlockOffset_.load(std::memory_order_relaxed);
    result.ok = writePeaksFile();
    return result;
}

// Straight back for more while there is data; a 10 ms nap when the ring is empty.
int TakeCaptureWriter::WriterClient::useTimeSlice() { return owner.drainOnce() ? 0 : 10; }

// Writer thread, or the message thread inside stop() once the client is detached; never both at once.
bool TakeCaptureWriter::drainOnce() {
    if (writer_ == nullptr)
        return false;

    const int ready = juce::jmin(ring_.getNumReady(), kDrainChunkFrames);
    if (ready <= 0)
        return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    ring_.prepareToRead(ready, start1, size1, start2, size2);
    const int frames = size1 + size2;
    if (frames <= 0)
        return false;

    const int regionStarts[2] = {start1, start2};
    const int regionSizes[2] = {size1, size2};
    int destFrame = 0;
    for (int region = 0; region < 2; ++region) {
        for (int i = 0; i < regionSizes[region]; ++i, ++destFrame) {
            const float* src =
                ringStorage_.data() + (std::size_t)(regionStarts[region] + i) * (std::size_t)kMaxChannels;
            for (int channel = 0; channel < kMaxChannels; ++channel)
                drainScratch_.getWritePointer(channel)[destFrame] = src[channel];
        }
    }

    // Only the capture's channels go to the writer: a mono take must not be fed a stereo array.
    const int channels = juce::jlimit(1, kMaxChannels, captureChannels_.load(std::memory_order_relaxed));
    writer_->writeFromFloatArrays(drainScratch_.getArrayOfReadPointers(), channels, frames);
    {
        const juce::ScopedLock lock(peaksLock_);
        peaksAccumulator_.addSamples(drainScratch_, frames);
    }
    writtenFrames_.fetch_add((juce::int64)frames, std::memory_order_relaxed);

    ring_.finishedRead(frames);
    return true;
}

bool TakeCaptureWriter::writePeaksFile() const {
    if (peaksFile_ == juce::File())
        return false;

    PeaksFile::Data data;
    {
        const juce::ScopedLock lock(peaksLock_);
        data = peaksAccumulator_.getData();
    }
    return PeaksFile::write(peaksFile_, data);
}

void TakeCaptureWriter::copyLivePeaks(std::vector<std::pair<float, float>>& out) const {
    const juce::ScopedLock lock(peaksLock_);
    out = peaksAccumulator_.getData().buckets;
}

} // namespace synth
