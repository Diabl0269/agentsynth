#include "RecordTapModule.h"

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
    {
        // Replaces the accumulator wholesale rather than reset()-ing the old one in place: a mono
        // take following a stereo one (or vice versa) needs a different numChannels, which reset()
        // deliberately does not change (see its own comment) — a fresh Accumulator is the simplest
        // way to get both "cleared" and "right shape" in one step. Locked because the writer thread
        // from a PREVIOUS take may still be idling with the client attached until the loop below
        // runs; in practice it is always detached by stopCapture() before this point, but the lock
        // costs nothing and removes the need to reason about it.
        const juce::ScopedLock lock(peaksLock_);
        peaksAccumulator_ = synth::PeaksFile::Accumulator(kPeakBucketSize, numChannels);
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
    const juce::ScopedLock lock(peaksLock_);
    peaksAccumulator_.addSamples(chunk, numFrames);
}

void RecordTapModule::flushPartialBucket() {
    const juce::ScopedLock lock(peaksLock_);
    peaksAccumulator_.flushPartial();
}

bool RecordTapModule::writePeaksFile() const {
    if (peaksFile_ == juce::File())
        return false;

    synth::PeaksFile::Data data;
    {
        const juce::ScopedLock lock(peaksLock_);
        data = peaksAccumulator_.getData();
    }
    return synth::PeaksFile::write(peaksFile_, data);
}

void RecordTapModule::copyLivePeaks(std::vector<std::pair<float, float>>& out) const {
    const juce::ScopedLock lock(peaksLock_);
    out = peaksAccumulator_.getData().buckets;
}

std::vector<float> RecordTapModule::getPeaksForTest() const {
    std::vector<std::pair<float, float>> pairs;
    copyLivePeaks(pairs);
    std::vector<float> flat;
    flat.reserve(pairs.size() * 2);
    for (const auto& pair : pairs) {
        flat.push_back(pair.first);
        flat.push_back(pair.second);
    }
    return flat;
}
