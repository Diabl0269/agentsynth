#include "RecordTapModule.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

// Little-endian writers for the .agpk sidecar. juce::FileOutputStream's writeInt/writeFloat are
// already little-endian on every platform JUCE supports, but the format is specified as LE (see the
// class comment), so the intent is spelled out here rather than inherited from a default.
void writeLittleEndianUInt32(juce::OutputStream& stream, std::uint32_t value) {
    stream.writeByte((char)(value & 0xffu));
    stream.writeByte((char)((value >> 8) & 0xffu));
    stream.writeByte((char)((value >> 16) & 0xffu));
    stream.writeByte((char)((value >> 24) & 0xffu));
}

void writeLittleEndianFloat(juce::OutputStream& stream, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float is not 32 bits");
    std::memcpy(&bits, &value, sizeof(bits));
    writeLittleEndianUInt32(stream, bits);
}

} // namespace

RecordTapModule::RecordTapModule(int ringCapacityFrames)
    : ModuleBase("Rec Tap", kNumChannels, kNumChannels)
    , ringCapacityFrames_(juce::jmax(1, ringCapacityFrames))
    , ring_(juce::jmax(1, ringCapacityFrames)) {
    // Everything the audio thread touches is allocated HERE, once. processBlock never sizes
    // anything, whatever the block size or the capture format turns out to be.
    ringStorage_.assign((std::size_t)ringCapacityFrames_ * (std::size_t)kNumChannels, 0.0f);
    drainScratch_.setSize(kNumChannels, kDrainChunkFrames);
    enableVisualBuffer(true);
}

RecordTapModule::~RecordTapModule() {
    // Finalises an in-flight take rather than abandoning a half-written WAV, and — crucially —
    // detaches the writer client before any member it touches starts being destroyed.
    if (isCapturing())
        stopCapture();
    writerThread_.stopThread(2000);
}

void RecordTapModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    if (sampleRate > 0.0)
        preparedSampleRate_ = sampleRate;
}

void RecordTapModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);

    const int numSamples = buffer.getNumSamples();

    // Bypass: this module HAS a dry audio path (it is a pass-through), so the bypassed branch
    // returns without touching the audio channels — see the bypass/mute contract in
    // docs/architecture.md. There are no CV channels to clear. Capture stops for the duration:
    // a bypassed tap is not part of the signal chain, so recording what flows past it would put
    // audio in the take that the user could not hear.
    if (isBypassed())
        return;

    // Pass-through is literally nothing: the graph hands us the input in the same buffer it wants
    // the output in, so the samples are already where they belong. Everything below only READS
    // them.

    if (numSamples > 0 && capturing_.load(std::memory_order_acquire)) {
        const int captureChannels = juce::jlimit(1, kNumChannels, captureChannels_.load(std::memory_order_relaxed));
        const int availableChannels = juce::jmin(captureChannels, buffer.getNumChannels());

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        ring_.prepareToWrite(numSamples, start1, size1, start2, size2);
        const int writable = size1 + size2;
        if (writable < numSamples) {
            // Full (or nearly): keep what fits and drop the rest rather than blocking the audio
            // thread. Surfaced later through hadOverrun() / TakeResult::overran.
            overrun_.store(true, std::memory_order_relaxed);
        }

        int sourceFrame = 0;
        const int regionStarts[2] = {start1, start2};
        const int regionSizes[2] = {size1, size2};
        for (int region = 0; region < 2; ++region) {
            for (int i = 0; i < regionSizes[region]; ++i, ++sourceFrame) {
                float* dest = ringStorage_.data() + (std::size_t)(regionStarts[region] + i) * (std::size_t)kNumChannels;
                for (int channel = 0; channel < kNumChannels; ++channel)
                    dest[channel] = channel < availableChannels ? buffer.getReadPointer(channel)[sourceFrame] : 0.0f;
            }
        }
        if (writable > 0) {
            ring_.finishedWrite(writable);
            capturedFrames_.fetch_add((juce::int64)writable, std::memory_order_relaxed);
        }
    }

    // Activity LED: the block's peak, same shape as every other audio module's meter feed.
    if (auto* vb = getVisualBuffer())
        for (int i = 0; i < numSamples; ++i)
            vb->pushSample(buffer.getNumChannels() > 0 ? buffer.getReadPointer(0)[i] : 0.0f);
}

// ------------------------------------------------------------------ message thread --

bool RecordTapModule::startCapture(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate,
                                   int numChannels) {
    if (isCapturing())
        return false; // a second start would interleave two takes into one file
    if (!(sampleRate > 0.0) || numChannels < 1 || numChannels > kNumChannels)
        return false;

    wavFile.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(wavFile.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    stream->setPosition(0);
    stream->truncate();

    juce::WavAudioFormat wavFormat;
    // bitDepth 32 makes this an IEEE-float WAV (juce::WavAudioFormatWriter sets
    // usesFloatingPointData for 32-bit), so the take is a bit-exact copy of what the graph
    // produced — no clipping, no dither decision, and a read-back that compares equal.
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int)numChannels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns the stream from here

    // Reset every piece of take state BEFORE arming, so the audio thread cannot push into a ring
    // that is about to be reset out from under it.
    ring_.reset();
    overrun_.store(false, std::memory_order_relaxed);
    capturedFrames_.store(0, std::memory_order_relaxed);
    writtenFrames_.store(0, std::memory_order_relaxed);
    peaks_.clear();
    bucketFill_ = 0;
    for (int channel = 0; channel < kNumChannels; ++channel) {
        bucketMin_[channel] = std::numeric_limits<float>::max();
        bucketMax_[channel] = std::numeric_limits<float>::lowest();
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

RecordTapModule::TakeResult RecordTapModule::stopCapture() {
    if (!isCapturing())
        return {}; // never started, or already stopped: ok == false, nothing written

    // Disarm first: processBlock checks this before touching the ring at all.
    capturing_.store(false, std::memory_order_release);

    // Blocks until any in-flight useTimeSlice() has returned (see juce::TimeSliceThread::
    // removeTimeSliceClient), which is what makes the message thread the ring's sole reader for
    // the rest of this function — no lock needed, and no torn drain.
    writerThread_.removeTimeSliceClient(&writerClient_);

    // Whatever the writer thread did not get to. A processBlock already in flight may still land a
    // few frames after this loop; they are simply not in the take, which is the same "the last
    // moment of a take is fuzzy" the punch-in start already has.
    while (drainOnce()) {
    }
    flushPartialBucket();

    writer_.reset(); // flushes and closes the stream: the WAV header is written by the destructor

    TakeResult result;
    // The WRITTEN count, not the pushed one: a processBlock that landed frames after the drain
    // loop above would make the pushed counter one block longer than the file, and the caller
    // turns this number straight into a clip length.
    result.lengthSamples = writtenFrames_.load(std::memory_order_relaxed);
    result.overran = overrun_.load(std::memory_order_relaxed);
    result.ok = writePeaksFile();
    return result;
}

// ------------------------------------------------------------------- writer thread --

int RecordTapModule::WriterClient::useTimeSlice() {
    // Straight back for more while there is data; a 10 ms nap when the ring is empty. Both numbers
    // are latency-of-drain, not latency-of-audio — the audio thread never waits on this.
    return owner.drainOnce() ? 0 : 10;
}

bool RecordTapModule::drainOnce() {
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

    // De-interleave into the pre-sized scratch: the writer wants per-channel arrays, and the peak
    // accumulation below reads the same layout.
    const int regionStarts[2] = {start1, start2};
    const int regionSizes[2] = {size1, size2};
    int destFrame = 0;
    for (int region = 0; region < 2; ++region) {
        for (int i = 0; i < regionSizes[region]; ++i, ++destFrame) {
            const float* src =
                ringStorage_.data() + (std::size_t)(regionStarts[region] + i) * (std::size_t)kNumChannels;
            for (int channel = 0; channel < kNumChannels; ++channel)
                drainScratch_.getWritePointer(channel)[destFrame] = src[channel];
        }
    }

    // Only the capture's channels are handed over: drainScratch_ is always kNumChannels wide, but
    // a mono take's writer must not be fed a stereo array's worth of "channels".
    const int channels = juce::jlimit(1, kNumChannels, captureChannels_.load(std::memory_order_relaxed));
    writer_->writeFromFloatArrays(drainScratch_.getArrayOfReadPointers(), channels, frames);
    accumulatePeaks(drainScratch_, frames);
    writtenFrames_.fetch_add((juce::int64)frames, std::memory_order_relaxed);

    ring_.finishedRead(frames);
    return true;
}

void RecordTapModule::accumulatePeaks(const juce::AudioBuffer<float>& chunk, int numFrames) {
    const int channels = juce::jlimit(1, kNumChannels, captureChannels_.load(std::memory_order_relaxed));

    for (int frame = 0; frame < numFrames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const float sample = chunk.getReadPointer(channel)[frame];
            bucketMin_[channel] = std::min(bucketMin_[channel], sample);
            bucketMax_[channel] = std::max(bucketMax_[channel], sample);
        }
        if (++bucketFill_ >= kPeakBucketSize)
            flushPartialBucket();
    }
}

void RecordTapModule::flushPartialBucket() {
    if (bucketFill_ <= 0)
        return; // nothing accumulated: never emit an empty bucket, the count is ceil(len/bucket)

    const int channels = juce::jlimit(1, kNumChannels, captureChannels_.load(std::memory_order_relaxed));
    for (int channel = 0; channel < channels; ++channel) {
        peaks_.push_back(bucketMin_[channel]);
        peaks_.push_back(bucketMax_[channel]);
        bucketMin_[channel] = std::numeric_limits<float>::max();
        bucketMax_[channel] = std::numeric_limits<float>::lowest();
    }
    bucketFill_ = 0;
}

bool RecordTapModule::writePeaksFile() const {
    if (peaksFile_ == juce::File())
        return false;

    peaksFile_.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(peaksFile_.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    stream->setPosition(0);
    stream->truncate();

    const std::uint32_t channels =
        (std::uint32_t)juce::jlimit(1, kNumChannels, captureChannels_.load(std::memory_order_relaxed));
    writeLittleEndianUInt32(*stream, kPeaksMagic);
    writeLittleEndianUInt32(*stream, kPeaksVersion);
    writeLittleEndianUInt32(*stream, (std::uint32_t)kPeakBucketSize);
    writeLittleEndianUInt32(*stream, channels);
    for (float value : peaks_)
        writeLittleEndianFloat(*stream, value);

    return stream->getStatus().wasOk();
}
