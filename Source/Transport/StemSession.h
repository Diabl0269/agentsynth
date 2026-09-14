#pragma once

#include "../Modules/ChannelStripModule.h"
#include "BounceExporter.h"
#include "StemExporter.h"
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

class AudioEngine;

namespace synth {

class OfflineTransportDriver;
class TimelineDoc; // Forward declaration (Source/Timeline/TimelineDoc.h) — see StemSession's
                   // constructor comment for why this layer now looks at it (FRO55).
// Defined in BounceGuards.h; forward-declared here for the same compile-firewall reason
// BounceSession.h does — StemSession.cpp includes the real definitions.
struct MetronomeForceOffGuard;
struct ExternalMidiSuspendGuard;

// One ChannelStrip node found in the graph, paired with the node id that makes the enumeration
// order well-defined (see collectStemStrips). Free function rather than a StemSession member so
// StemExporter::hasChannelStrips can answer "is there anything to export?" without paying for a
// whole session's setup.
struct StemStripEntry {
    juce::AudioProcessorGraph::NodeID nodeId;
    ChannelStripModule* strip = nullptr;
};

// Every ChannelStripModule node in `graph`, ordered by node id (ascending) — the "else node id"
// fallback docs/mixer.md §5.12 allows when no mixer/track order is exposed to this layer (this
// enumeration itself still has no dependency on the timeline/track model — only STEM NAMING, in
// StemSession's constructor below, optionally looks at a TimelineDoc). Node ids are assigned in
// creation order and never reused within a session, so this order is stable across repeated calls
// against the same graph.
std::vector<StemStripEntry> collectStemStrips(juce::AudioProcessorGraph& graph);

// The choreography behind StemExporter::exportStems(), split into resumable steps exactly the way
// BounceSession splits BounceExporter::bounce() (see that class's own comment for why — StemRunner
// is this session's BounceRunner). The two classes are deliberately siblings rather than one
// generalised over "one writer vs N": the render loop's SHAPE (validate -> suspend -> reprepare ->
// stop/unloop/locate/play -> stream blocks -> restore) is identical, but the shared pieces
// (BounceGuards.h, synth::validateBounceOptions) are already factored out, and templating the
// per-block sink over N writers would churn BounceSession/BounceExporterTests for no behavioural
// gain — see docs/architecture.md's bounce/export section.
//
// MESSAGE THREAD, START TO FINISH — same contract as BounceSession.
class StemSession {
public:
    // Validates `options`, suspends the device callback, re-prepares the graph at the render format,
    // enumerates every ChannelStrip node (failing setup immediately if there are none), arms each
    // strip's stem tap and opens one writer/temp file per strip inside `destinationFolder`
    // (destinationFolder is created here if it doesn't exist yet). If anything fails, nothing further
    // is touched and no tap stays armed: the engine is restored immediately and failedDuringSetup()
    // is true.
    //
    // `timelineDoc` (FRO55, docs/mixer.md §5.12): the live document a stem file's name is resolved
    // against — each strip is named after the ONE track (Track In / Track Audio, walked upstream
    // through the instrument/macro chain) that feeds it, falling back to "Channel N" when zero or
    // several tracks do, or when this is null (a caller with no timeline at all — every test rig
    // built before this ticket, and any future headless caller that doesn't care about names).
    // MESSAGE THREAD, read once here — never stored past the constructor.
    StemSession(AudioEngine& engine, const juce::File& destinationFolder, const BounceOptions& options,
                const BounceExporter::ProgressCallback& progress = {}, const TimelineDoc* timelineDoc = nullptr);

    // Disarms every tap and restores the engine if finish() was never called.
    ~StemSession();

    bool failedDuringSetup() const noexcept { return setupFailed_; }

    bool stepRange(int maxBlocks);
    bool isRangeDone() const noexcept { return setupFailed_ || rangeDone_; }

    bool stepTail(int maxBlocks);
    bool isTailDone() const noexcept { return setupFailed_ || tailDone_; }

    // Ends the render: disarms every tap, closes every writer, restores the transport/engine, and —
    // only once EVERY stem finished writing successfully — moves every temp file into place. Any
    // stem write failure fails the whole export and moves nothing; a cancelled or abandoned render
    // finishes as a cancel, same as BounceSession::finish(). Idempotent.
    StemResult finish();

    void requestCancel() noexcept { cancelled_ = true; }

    // 0..1, monotonically non-decreasing, matching the fraction the progress callback would see.
    double getProgress() const noexcept;

private:
    void restoreTransportAndEngine();
    void disarmAllTaps() noexcept;

    struct StemWriter {
        ChannelStripModule* strip = nullptr;
        juce::File finalFile;
        // blockSize x 2, preallocated once in setup — see ChannelStripModule's stem tap contract.
        juce::AudioBuffer<float> tapBuffer;
        std::unique_ptr<juce::TemporaryFile> temporary;
        std::unique_ptr<juce::AudioFormatWriter> writer;
    };

    AudioEngine& engine_;
    juce::File destinationFolder_;
    BounceOptions options_;
    BounceExporter::ProgressCallback progress_;

    std::unique_ptr<MetronomeForceOffGuard> metronomeGuard_;
    std::unique_ptr<ExternalMidiSuspendGuard> externalMidiGuard_;
    std::unique_ptr<OfflineTransportDriver> driver_;
    std::vector<StemWriter> stems_;

    // Read before anything is disturbed, replayed by restoreTransportAndEngine() — same fields
    // BounceSession keeps, for the same reason.
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
    juce::int64 samplesWritten_ = 0; // per stem - every stem writer receives the same block count
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
    StemResult finishedResult_;

    bool setupFailed_ = false;
    StemResult setupResult_;
};

} // namespace synth
