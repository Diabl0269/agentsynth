#pragma once

#include "BounceExporter.h"
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

class AudioEngine;

namespace synth {

class OfflineTransportDriver;

// The choreography behind BounceExporter::bounce(), split into resumable steps so a caller can
// interleave "render a handful of blocks" with something else (a UI progress tick) instead of
// blocking for the whole take. BounceExporter::bounce() itself is just this class driven to
// completion in one go, so there is exactly one copy of the render loop.
//
// MESSAGE THREAD, START TO FINISH. Nothing here is any safer to call off the message thread than
// BounceExporter::bounce() was — see OfflineTransportDriver's own precondition and
// AudioEngine::suspendDeviceCallback/resumeDeviceCallback's message-thread-only contract. A caller
// that wants the render to not block the UI must instead call stepRange()/stepTail() in small
// pieces from a juce::Timer (see BounceRunner), never from a second thread.
class BounceSession {
public:
    // Validates `options`, suspends the device callback, re-prepares the graph at the render
    // format and opens the destination writer/temp file. If anything here fails (bad options, an
    // unwritable destination, ...) nothing further is touched: the engine is restored immediately
    // and failedDuringSetup() is true, ready for finish() to report the reason. `progress` is
    // called exactly like BounceExporter::bounce()'s own parameter — once per rendered block,
    // returning false cancels.
    BounceSession(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                  const BounceExporter::ProgressCallback& progress = {});

    // Restores the engine if finish() was never called - a session abandoned mid-render must never
    // leave the engine suspended or the transport mid-bounce.
    ~BounceSession();

    bool failedDuringSetup() const noexcept { return setupFailed_; }

    // Renders up to maxBlocks blocks of [startBeat, endBeat]. Returns true once the range phase is
    // over (finished, cancelled, or failed) - safe to call again after that, a no-op returning
    // true. A no-op returning true immediately if failedDuringSetup().
    bool stepRange(int maxBlocks);
    bool isRangeDone() const noexcept { return setupFailed_ || rangeDone_; }

    // Same shape, for the tail. Only meaningful once isRangeDone().
    bool stepTail(int maxBlocks);
    bool isTailDone() const noexcept { return setupFailed_ || tailDone_; }

    // Ends the render: closes the writer, restores the transport/engine, and moves the temp file
    // into place on success. Call once isRangeDone() && isTailDone() (or after failedDuringSetup()
    // - it just reports that failure). Idempotent: a second call returns the same result. If the
    // render was not actually finished yet (a caller gave up early), it is completed as a cancel.
    BounceResult finish();

    void requestCancel() noexcept { cancelled_ = true; }

    // 0..1, monotonically non-decreasing, matching the fraction the progress callback would see.
    double getProgress() const noexcept;

private:
    void restoreTransportAndEngine();

    AudioEngine& engine_;
    juce::File outFile_;
    BounceOptions options_;
    BounceExporter::ProgressCallback progress_;

    struct MetronomeGuard;
    std::unique_ptr<MetronomeGuard> metronomeGuard_;
    std::unique_ptr<OfflineTransportDriver> driver_;
    std::unique_ptr<juce::TemporaryFile> temporary_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;

    // Read before anything is disturbed, replayed by restoreTransportAndEngine().
    double beforeLoopStartPpq_ = 0.0;
    double beforeLoopEndPpq_ = 0.0;
    bool beforeLooping_ = false;
    double beforePpq_ = 0.0;
    double previousSampleRate_ = 0.0;
    int previousBlockSize_ = 0;
    int previousInputChannels_ = 0;
    int previousOutputChannels_ = 0;
    bool deviceWasAttached_ = false;

    juce::int64 expectedTotalSamples_ = 0;
    juce::int64 samplesWritten_ = 0;
    int streamDropouts_ = 0;
    bool cancelled_ = false;
    bool writeFailed_ = false;

    double nextBlockBeat_ = 0.0;
    double nextBlockBpm_ = 120.0;
    bool firstBlockRendered_ = false;
    int rangeBlocksRemaining_ = 0;

    bool tailSetupDone_ = false;
    int tailBlocksRemaining_ = 0;

    bool rangeDone_ = false;
    bool tailDone_ = false;
    bool finished_ = false;
    BounceResult finishedResult_;

    bool setupFailed_ = false;
    BounceResult setupResult_;
};

} // namespace synth
