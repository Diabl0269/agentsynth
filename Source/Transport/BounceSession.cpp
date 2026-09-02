#include "BounceSession.h"

#include "../AudioEngine.h"
#include "Metronome.h"
#include "OfflineTransportDriver.h"
#include <cmath>

namespace synth {

namespace {

// A safety cap for the range render, sized off the exact block count the tempo implies. Under a
// constant tempo the render lands on the expected count exactly; this only exists so a future
// tempo-map bug fails after a bounded amount of work instead of grinding to the driver's own
// 2^18-block backstop.
constexpr juce::int64 kMaxRangeBlocks = 1 << 20;

// How long one block may wait for AudioClipStreamer's prefetch thread before the block is counted
// as a dropout and rendered anyway. Generous: a ring that has not filled in two seconds means the
// disk is in trouble, and stalling the whole bounce on it would be worse than reporting it.
constexpr int kPrimeTimeoutMs = 2000;

BounceResult failure(juce::String message) {
    BounceResult result;
    result.ok = false;
    result.message = std::move(message);
    return result;
}

// Everything that can be rejected before a single sample is rendered, or a single file touched.
juce::String validateOptions(const BounceOptions& options) {
    if (!(options.sampleRate > 0.0) || !std::isfinite(options.sampleRate))
        return "Sample rate must be a positive number.";
    if (options.blockSize <= 0)
        return "Block size must be at least 1 sample.";
    if (options.numChannels <= 0)
        return "A bounce needs at least one channel.";
    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
        return "Bit depth must be 16, 24 or 32.";
    if (options.format == BounceFormat::Aiff && options.bitDepth == 32)
        return "AIFF has no 32-bit float variant - choose 16 or 24 bit, or export WAV instead.";
    if (!std::isfinite(options.startBeat) || !std::isfinite(options.endBeat))
        return "The bounce range must be finite.";
    if (options.startBeat < 0.0)
        return "The bounce range must start at or after beat 0.";
    if (!(options.endBeat > options.startBeat))
        return "The bounce range must end after it starts.";
    if (!std::isfinite(options.tailSeconds) || options.tailSeconds < 0.0)
        return "Tail length must be zero or more seconds.";
    return {};
}

} // namespace

// The metronome is summed POST-graph, so a bounce — which captures exactly the graph's own output
// buffer — would otherwise pick the click up. Force BOTH the user toggle and the count-in
// forced-on flag off for the render and restore them afterwards, on every exit path.
struct BounceSession::MetronomeGuard {
    explicit MetronomeGuard(Metronome& metronomeIn) noexcept
        : metronome(metronomeIn)
        , savedEnabled(metronomeIn.isEnabled())
        , savedForcedOn(metronomeIn.isForcedOn()) {
        metronome.setEnabled(false);
        metronome.setForcedOn(false);
    }
    ~MetronomeGuard() noexcept {
        metronome.setEnabled(savedEnabled);
        metronome.setForcedOn(savedForcedOn);
    }

    Metronome& metronome;
    bool savedEnabled;
    bool savedForcedOn;

    JUCE_DECLARE_NON_COPYABLE(MetronomeGuard)
};

BounceSession::BounceSession(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                             const BounceExporter::ProgressCallback& progress)
    : engine_(engine)
    , outFile_(outFile)
    , options_(options)
    , progress_(progress)
    , nextBlockBeat_(options.startBeat) {
    if (const auto problem = validateOptions(options_); problem.isNotEmpty()) {
        setupFailed_ = true;
        setupResult_ = failure(problem);
        return;
    }
    if (outFile_ == juce::File()) {
        setupFailed_ = true;
        setupResult_ = failure("No output file was given.");
        return;
    }

    auto& transport = engine_.getTransport();
    auto& graph = engine_.getGraph();

    metronomeGuard_ = std::make_unique<MetronomeGuard>(engine_.getMetronome());

    // ---- Everything that has to go back afterwards, read before anything is disturbed ----
    const auto before = transport.getPositionSnapshot();
    beforeLoopStartPpq_ = before.loopStartPpq;
    beforeLoopEndPpq_ = before.loopEndPpq;
    beforeLooping_ = before.looping;
    beforePpq_ = before.ppq;
    previousSampleRate_ = graph.getSampleRate();
    previousBlockSize_ = graph.getBlockSize();
    previousInputChannels_ = graph.getTotalNumInputChannels();
    previousOutputChannels_ = graph.getTotalNumOutputChannels();

    // Take the graph off the device (Standalone with a live device); a Hosted engine reports false
    // and is used as-is.
    deviceWasAttached_ = engine_.suspendDeviceCallback();

    // Constructing the driver re-prepares the whole graph at the render format.
    driver_ = std::make_unique<OfflineTransportDriver>(engine_, options_.sampleRate, options_.blockSize,
                                                       options_.numChannels);

    // ---- The writer, on a temp file beside the target ----
    temporary_ = std::make_unique<juce::TemporaryFile>(outFile_);
    std::unique_ptr<juce::FileOutputStream> stream(temporary_->getFile().createOutputStream());
    if (stream == nullptr || stream->failedToOpen()) {
        restoreTransportAndEngine();
        setupFailed_ = true;
        setupResult_ = failure("Could not open \"" + outFile_.getFullPathName() + "\" for writing.");
        return;
    }

    juce::WavAudioFormat wavFormat;
    juce::AiffAudioFormat aiffFormat;
    juce::AudioFormat& audioFormat = options_.format == BounceFormat::Aiff ? static_cast<juce::AudioFormat&>(aiffFormat)
                                                                           : static_cast<juce::AudioFormat&>(wavFormat);
    writer_.reset(audioFormat.createWriterFor(stream.get(), options_.sampleRate, (unsigned int)options_.numChannels,
                                              options_.bitDepth, {}, 0));
    if (writer_ == nullptr) {
        restoreTransportAndEngine();
        setupFailed_ = true;
        setupResult_ = failure("Could not create an audio writer for the requested format.");
        return;
    }
    stream.release(); // the writer owns the stream from here

    // ---- How long this is expected to be, for the progress fraction ----
    const double bpm = before.bpm > 0.0 ? before.bpm : 120.0;
    nextBlockBpm_ = bpm;
    const double rangeSamples = (options_.endBeat - options_.startBeat) * 60.0 * options_.sampleRate / bpm;
    const juce::int64 expectedRangeBlocks = (juce::int64)std::ceil(rangeSamples / (double)options_.blockSize);
    const juce::int64 tailBlocksTotal =
        (juce::int64)std::ceil(options_.tailSeconds * options_.sampleRate / (double)options_.blockSize);
    expectedTotalSamples_ = (expectedRangeBlocks + tailBlocksTotal) * (juce::int64)options_.blockSize;

    rangeBlocksRemaining_ = (int)juce::jlimit<juce::int64>(1, kMaxRangeBlocks, expectedRangeBlocks * 2 + 64);
    tailBlocksRemaining_ = (int)juce::jmin<juce::int64>(tailBlocksTotal, kMaxRangeBlocks);

    // ---- Choreography start ----
    // All four commands drain, in this order, at the top of the first tick below: stop whatever
    // was playing, drop the loop for the duration, locate to the range start, play.
    transport.stop();
    transport.setLoop(beforeLoopStartPpq_, beforeLoopEndPpq_, false);
    transport.locateBeat(options_.startBeat);
    transport.play();
}

BounceSession::~BounceSession() {
    if (!finished_)
        finish();
}

void BounceSession::restoreTransportAndEngine() {
    auto& transport = engine_.getTransport();
    transport.stop();
    transport.setLoop(beforeLoopStartPpq_, beforeLoopEndPpq_, beforeLooping_);
    transport.locateBeat(beforePpq_);
    if (driver_ != nullptr)
        driver_->streamBlocks(1, {});

    if (deviceWasAttached_)
        engine_.resumeDeviceCallback(); // re-prepares at the DEVICE's rate; see AudioEngine.h
    else if (previousSampleRate_ > 0.0 && previousBlockSize_ > 0)
        engine_.prepareForHost(previousSampleRate_, previousBlockSize_, previousInputChannels_,
                               previousOutputChannels_);
}

bool BounceSession::stepRange(int maxBlocks) {
    if (isRangeDone())
        return true;

    // The one place audio leaves the render.
    const auto streamToWriter = [this](const juce::AudioBuffer<float>& block, const BlockTimeInfo& info) {
        if (info.bpm > 0.0)
            nextBlockBpm_ = info.bpm;
        nextBlockBeat_ = info.endPpq;

        if (cancelled_ || writeFailed_)
            return;

        if (!writer_->writeFromAudioSampleBuffer(block, 0, block.getNumSamples())) {
            writeFailed_ = true;
            engine_.getTransport().stop();
            return;
        }

        samplesWritten_ += block.getNumSamples();

        if (progress_) {
            const double fraction = expectedTotalSamples_ > 0
                                        ? juce::jmin(1.0, (double)samplesWritten_ / (double)expectedTotalSamples_)
                                        : 1.0;
            if (!progress_(fraction)) {
                cancelled_ = true;
                engine_.getTransport().stop();
            }
        }
    };

    // A bounce outruns the clip streamer's prefetch thread by orders of magnitude, and
    // constructing the driver invalidated every ring, so the first block would otherwise read
    // silence and later ones would drop out at the disk's whim.
    const auto renderRangeBlock = [this]() {
        if (cancelled_ || writeFailed_)
            return false;
        if (!engine_.getAudioClipStreamer().waitUntilPrimed(nextBlockBeat_, nextBlockBpm_, options_.sampleRate,
                                                            options_.blockSize, kPrimeTimeoutMs))
            ++streamDropouts_;
        return true;
    };

    if (!firstBlockRendered_) {
        firstBlockRendered_ = true;
        // One block on its own first, on purpose: streamToBeat decides whether the target is even
        // ahead of the playhead by reading the CROSS-THREAD POSITION SNAPSHOT, which the locate in
        // the constructor has not reached yet — bouncing bars 1-2 while the playhead sits at bar
        // 40 would otherwise bail out instantly with an empty file.
        driver_->streamBlocks(1, streamToWriter, renderRangeBlock);
        maxBlocks = juce::jmax(0, maxBlocks - 1);
    }

    if (!cancelled_ && !writeFailed_ && nextBlockBeat_ < options_.endBeat && maxBlocks > 0 &&
        rangeBlocksRemaining_ > 0) {
        const int thisCall = juce::jmin(maxBlocks, rangeBlocksRemaining_);
        const int rendered = driver_->streamToBeat(options_.endBeat, streamToWriter, thisCall, renderRangeBlock);
        rangeBlocksRemaining_ -= rendered;
    }

    rangeDone_ = cancelled_ || writeFailed_ || nextBlockBeat_ >= options_.endBeat || rangeBlocksRemaining_ <= 0;
    return rangeDone_;
}

bool BounceSession::stepTail(int maxBlocks) {
    if (isTailDone())
        return true;
    if (!isRangeDone())
        return false; // not ready yet

    const auto streamToWriter = [this](const juce::AudioBuffer<float>& block, const BlockTimeInfo& info) {
        if (info.bpm > 0.0)
            nextBlockBpm_ = info.bpm;
        nextBlockBeat_ = info.endPpq;

        if (cancelled_ || writeFailed_)
            return;

        if (!writer_->writeFromAudioSampleBuffer(block, 0, block.getNumSamples())) {
            writeFailed_ = true;
            return;
        }

        samplesWritten_ += block.getNumSamples();

        if (progress_) {
            const double fraction = expectedTotalSamples_ > 0
                                        ? juce::jmin(1.0, (double)samplesWritten_ / (double)expectedTotalSamples_)
                                        : 1.0;
            if (!progress_(fraction))
                cancelled_ = true;
        }
    };
    // Rendered with the transport STOPPED (see below), so clips and sequencers fall silent while
    // the FX ring out — nothing to prime, this gate exists only so a cancelled or failed bounce
    // stops rendering a tail nobody is writing.
    const auto renderTailBlock = [this]() { return !cancelled_ && !writeFailed_; };

    if (!tailSetupDone_) {
        tailSetupDone_ = true;
        if (cancelled_ || writeFailed_)
            tailBlocksRemaining_ = 0;
        else if (tailBlocksRemaining_ > 0)
            engine_.getTransport().stop();
    }

    if (!cancelled_ && !writeFailed_ && tailBlocksRemaining_ > 0 && maxBlocks > 0) {
        const int thisCall = juce::jmin(maxBlocks, tailBlocksRemaining_);
        const int rendered = driver_->streamBlocks(thisCall, streamToWriter, renderTailBlock);
        tailBlocksRemaining_ -= rendered;
        if (rendered < thisCall)
            tailBlocksRemaining_ = 0; // the gate stopped it early (cancel/fail) - nothing more to render
    }

    tailDone_ = tailBlocksRemaining_ <= 0;
    return tailDone_;
}

BounceResult BounceSession::finish() {
    if (finished_)
        return finishedResult_;
    finished_ = true;

    if (setupFailed_) {
        finishedResult_ = setupResult_;
        return finishedResult_;
    }

    // Abandoned mid-render (a caller gave up without cancelling through the progress callback) -
    // treat it as a cancel rather than claim a success that never happened.
    if (!rangeDone_ || !tailDone_)
        cancelled_ = true;

    // Flush and close before the file is moved or inspected.
    writer_.reset();
    restoreTransportAndEngine();

    BounceResult result;
    result.samplesWritten = samplesWritten_;
    result.streamDropouts = streamDropouts_;

    if (cancelled_) {
        // The temp file dies with `temporary_`; the target was never touched.
        result.message = "Bounce cancelled.";
        finishedResult_ = result;
        return finishedResult_;
    }

    if (writeFailed_) {
        result.message = "Failed while writing to \"" + outFile_.getFullPathName() + "\".";
        finishedResult_ = result;
        return finishedResult_;
    }

    if (!temporary_->overwriteTargetFileWithTemporary()) {
        result.message = "Could not move the rendered audio into \"" + outFile_.getFullPathName() + "\".";
        finishedResult_ = result;
        return finishedResult_;
    }

    if (progress_)
        progress_(1.0);

    result.ok = true;
    result.message = "Bounced " + juce::String(samplesWritten_) + " samples to \"" + outFile_.getFileName() + "\".";
    if (streamDropouts_ > 0)
        result.message +=
            " " + juce::String(streamDropouts_) + " block(s) played silence while waiting for audio clips.";
    finishedResult_ = result;
    return finishedResult_;
}

double BounceSession::getProgress() const noexcept {
    return expectedTotalSamples_ > 0 ? juce::jmin(1.0, (double)samplesWritten_ / (double)expectedTotalSamples_) : 0.0;
}

} // namespace synth
