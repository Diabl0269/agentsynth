#include "StemSession.h"

#include "../AudioEngine.h"
#include "../Modules/AttenuverterModule.h"
#include "../Timeline/TimelineDoc.h"
#include "BounceGuards.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "OfflineTransportDriver.h"
#include <algorithm>
#include <cmath>

namespace synth {

std::vector<StemStripEntry> collectStemStrips(juce::AudioProcessorGraph& graph) {
    std::vector<StemStripEntry> result;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor()))
            result.push_back({node->nodeID, strip});
    }
    // "else node id" (docs/mixer.md §5.12) - this ENUMERATION ORDER still has no dependency on the
    // timeline/track model, so node id ascending is the stable order available at this layer. That
    // stability is only WITHIN one export: node ids are reassigned whenever the graph is rebuilt from
    // JSON (e.g. an undo/redo that crosses a rebuild - MixerSoloTests.UndoRedoAcrossAGraphRebuildSettlesTheGate
    // is the proof such rebuilds happen), so "NN" numbering a given strip is not itself a
    // cross-session guarantee, only a per-export one. STEM NAMING (the "<name>" half of
    // "NN - <name>.<ext>") is a separate concern, resolved in StemSession's constructor below, and
    // that part DOES optionally look at a synth::TimelineDoc (FRO55) - see its own comment.
    std::sort(result.begin(), result.end(),
              [](const StemStripEntry& a, const StemStripEntry& b) { return a.nodeId.uid < b.nodeId.uid; });
    return result;
}

namespace {

// Same safety cap and prime timeout as BounceSession - see that file's own comments.
constexpr juce::int64 kMaxRangeBlocks = 1 << 20;
constexpr int kPrimeTimeoutMs = 2000;

StemResult failure(juce::String message) {
    StemResult result;
    result.ok = false;
    result.message = std::move(message);
    return result;
}

// ---- FRO55 (docs/mixer.md §5.12): stem file names, off the TRACK that feeds each strip ----------

// True when `conn` carries signal (audio or MIDI) rather than a hidden modulation leg — the same
// two exclusions ChannelFlows.cpp's own isSignalEdge rule uses (that function is private to this
// codebase's "Make channel" implementation, so this mirrors just the parts a backward walk needs
// rather than exposing it): never cross an AttenuverterModule on either end (AudioEngine::
// addModRouting always wraps a hidden mod leg in one of these), and never treat an audio edge
// landing on a PortRole::ModCV input as part of the signal path (a plain CV cable into some other
// track's cutoff must not make that track "feed" this strip). Unlike ChannelFlows' full rule, this
// does not resolve through macro ports first (resolveThroughPorts) - a CV cable that enters a strip's
// macro through an auto-ported Mono jack is a rare enough patch shape that treating it as signal
// here is an acceptable simplification for a stem FILE NAME, not a routing decision.
bool isStemNamingSignalEdge(juce::AudioProcessorGraph& graph, const juce::AudioProcessorGraph::Connection& conn) {
    auto* srcNode = graph.getNodeForId(conn.source.nodeID);
    auto* dstNode = graph.getNodeForId(conn.destination.nodeID);
    auto* srcProcessor = srcNode != nullptr ? srcNode->getProcessor() : nullptr;
    auto* dstProcessor = dstNode != nullptr ? dstNode->getProcessor() : nullptr;
    if (srcProcessor == nullptr || dstProcessor == nullptr)
        return false;
    if (dynamic_cast<AttenuverterModule*>(srcProcessor) != nullptr ||
        dynamic_cast<AttenuverterModule*>(dstProcessor) != nullptr)
        return false;
    if (conn.source.isMIDI())
        return true;
    if (auto* module = dynamic_cast<ModuleBase*>(dstProcessor))
        return module->mapInputChannel(conn.destination.channelIndex).role != PortRole::ModCV;
    return true;
}

// BFS upstream from `stripId`, following every incoming SIGNAL edge (isStemNamingSignalEdge above),
// transitively through the instrument/macro chain, collecting every distinct TimelineMidiSource /
// TimelineAudioSource ("Track In" / "Track Audio") node reached — the track(s) whose signal feeds
// this strip however many hops away. Mirrors ChannelFlows.cpp's findUnchanneledOutputFeeds in
// reverse: never expands PAST a ChannelStripModule reached upstream (that strip is already another
// channel's own terminus - whatever feeds IT is not this strip's track to claim), and a track-source
// node is a pure source (no inputs of its own), so it is recorded but not enqueued either. Cycle-safe
// via the visited set; a plain BFS terminates on any finite graph regardless.
std::vector<juce::AudioProcessorGraph::NodeID> upstreamTrackSources(juce::AudioProcessorGraph& graph,
                                                                    juce::AudioProcessorGraph::NodeID stripId) {
    std::vector<juce::AudioProcessorGraph::NodeID> tracks;
    std::vector<juce::AudioProcessorGraph::NodeID> visited{stripId};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{stripId};
    const auto connections = graph.getConnections();

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        for (const auto& conn : connections) {
            if (conn.destination.nodeID != nodeId || !isStemNamingSignalEdge(graph, conn))
                continue;
            const auto sourceId = conn.source.nodeID;
            if (std::find(visited.begin(), visited.end(), sourceId) != visited.end())
                continue;
            visited.push_back(sourceId);

            auto* sourceNode = graph.getNodeForId(sourceId);
            auto* sourceProcessor = sourceNode != nullptr ? sourceNode->getProcessor() : nullptr;
            if (isTrackSourceNode(sourceProcessor)) {
                if (std::find(tracks.begin(), tracks.end(), sourceId) == tracks.end())
                    tracks.push_back(sourceId);
                continue; // a track source has no inputs of its own - nothing to enqueue
            }
            if (dynamic_cast<ChannelStripModule*>(sourceProcessor) != nullptr)
                continue; // never expand past another strip - see the function comment

            queue.push_back(sourceId);
        }
    }
    return tracks;
}

// `timelineDoc`'s track whose bindingUuid equals `nodeUuid`, or an empty string when there is no
// doc, no uuid, or no such track (an unbound/untracked node - e.g. a graph built directly by a test
// with no TimelineDoc behind it at all).
juce::String trackNameForNodeUuid(const TimelineDoc* timelineDoc, const juce::String& nodeUuid) {
    if (timelineDoc == nullptr || nodeUuid.isEmpty())
        return {};
    for (const auto& track : timelineDoc->getTracks())
        if (track.bindingUuid == nodeUuid)
            return track.name;
    return {};
}

// The stem NAME for the strip at `stripId` (the part between "NN - " and the extension) - FRO55,
// docs/mixer.md §5.12. `number` is the strip's own 1-based export position, reused verbatim for the
// "Channel N" fallback so it always agrees with the file's own "NN" prefix. Exactly one upstream
// track source resolving to a non-empty TimelineDoc track name wins; zero, several, or an
// unresolvable/untracked source all fall back, which is also what keeps every name legal and
// unique on its own (the "NN - " prefix already makes the full FILE name unique regardless - see
// docs/mixer.md §5.12 - so no separate de-duplication pass is needed here even when two different
// tracks share a user-given name).
//
// ChannelStripModule has no user-given name field of its own yet (checked at FRO55 time - its
// getExtraState() carries only "shape"/"solo", docs/mixer.md §5.4-5.10 never added one either) - the
// mixer-UI ticket that might add one is expected to make THIS function prefer it, ahead of the track
// walk below, whenever it lands.
juce::String stemStripName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId, int number,
                           const TimelineDoc* timelineDoc) {
    const juce::String fallback = "Channel " + juce::String(number);
    const auto tracks = upstreamTrackSources(graph, stripId);
    if (tracks.size() != 1)
        return fallback;

    auto* node = graph.getNodeForId(tracks.front());
    if (node == nullptr)
        return fallback;
    // The message-thread-canonical uuid (ChannelFlows.cpp's own `inMacro` reads the same property
    // for the same reason) - never ModuleBase::getNodeUuid(), which is the audio-thread-safe mirror
    // and returns a raw const char* rather than a juce::String.
    const juce::String uuid = node->properties["uuid"].toString();
    const juce::String trackName = trackNameForNodeUuid(timelineDoc, uuid);
    return trackName.isNotEmpty() ? trackName : fallback;
}

} // namespace

StemSession::StemSession(AudioEngine& engine, const juce::File& destinationFolder, const BounceOptions& options,
                         const BounceExporter::ProgressCallback& progress, const TimelineDoc* timelineDoc)
    : engine_(engine)
    , destinationFolder_(destinationFolder)
    , options_(options)
    , progress_(progress)
    , nextBlockBeat_(options.startBeat) {
    if (const auto problem = validateBounceOptions(options_); problem.isNotEmpty()) {
        setupFailed_ = true;
        setupResult_ = failure(problem);
        return;
    }
    if (destinationFolder_ == juce::File()) {
        setupFailed_ = true;
        setupResult_ = failure("No destination folder was given.");
        return;
    }
    if (destinationFolder_.exists() && !destinationFolder_.isDirectory()) {
        setupFailed_ = true;
        setupResult_ = failure("\"" + destinationFolder_.getFullPathName() + "\" is not a folder.");
        return;
    }

    auto& transport = engine_.getTransport();
    auto& graph = engine_.getGraph();

    metronomeGuard_ = std::make_unique<MetronomeForceOffGuard>(engine_.getMetronome());

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

    deviceWasAttached_ = engine_.suspendDeviceCallback();
    externalMidiGuard_ = std::make_unique<ExternalMidiSuspendGuard>(engine_);

    // Constructing the driver re-prepares the whole graph at the render format - strips' pointers
    // stay valid across this (the graph itself is not cleared), so enumerating and sizing tap
    // buffers afterward, against the settled options_.blockSize, is correct either way.
    driver_ = std::make_unique<OfflineTransportDriver>(engine_, options_.sampleRate, options_.blockSize,
                                                       options_.numChannels);

    // ---- No strips means nothing to export - fail before touching the destination folder ----
    const auto entries = collectStemStrips(graph);
    if (entries.empty()) {
        restoreTransportAndEngine();
        setupFailed_ = true;
        setupResult_ = failure(StemExporter::kNoChannelsMessage);
        return;
    }

    if (!destinationFolder_.isDirectory()) {
        const auto folderResult = destinationFolder_.createDirectory();
        if (folderResult.failed()) {
            restoreTransportAndEngine();
            setupFailed_ = true;
            setupResult_ = failure("Could not create \"" + destinationFolder_.getFullPathName() + "\".");
            return;
        }
    }

    // ---- One writer per strip, on a sibling temp file each, inside the destination folder ----
    juce::WavAudioFormat wavFormat;
    juce::AiffAudioFormat aiffFormat;
    juce::AudioFormat& audioFormat = options_.format == BounceFormat::Aiff ? static_cast<juce::AudioFormat&>(aiffFormat)
                                                                           : static_cast<juce::AudioFormat&>(wavFormat);
    const juce::String extension = options_.format == BounceFormat::Aiff ? "aiff" : "wav";
    // Wide enough that "07" doesn't need to become "007" once an 8th strip exists, but never
    // narrower than 2 digits even for a 1-strip export - see docs/mixer.md §5.12.
    const int nameWidth = juce::jmax(2, juce::String((int)entries.size()).length());

    stems_.reserve(entries.size()); // pointers into stems_[i].tapBuffer are armed below and must
                                    // never move once a tap holds one - see the class comment.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        StemWriter sw;
        sw.strip = entries[i].strip;
        const auto number = juce::String((int)i + 1).paddedLeft('0', nameWidth);
        // FRO55: named after the track that feeds this strip, not the strip's own graph-node
        // instance name (that used to be "Channel Strip N" - distinct across strips, but useless) -
        // see stemStripName's own comment.
        const auto legalName =
            juce::File::createLegalFileName(stemStripName(graph, entries[i].nodeId, (int)i + 1, timelineDoc));
        sw.finalFile = destinationFolder_.getChildFile(number + " - " + legalName + "." + extension);
        sw.tapBuffer.setSize(2, options_.blockSize);
        sw.tapBuffer.clear();

        sw.temporary = std::make_unique<juce::TemporaryFile>(sw.finalFile);
        std::unique_ptr<juce::FileOutputStream> stream(sw.temporary->getFile().createOutputStream());
        if (stream == nullptr || stream->failedToOpen()) {
            restoreTransportAndEngine();
            setupFailed_ = true;
            setupResult_ = failure("Could not open \"" + sw.finalFile.getFullPathName() + "\" for writing.");
            return;
        }

        sw.writer.reset(audioFormat.createWriterFor(stream.get(), options_.sampleRate, 2u, options_.bitDepth, {}, 0));
        if (sw.writer == nullptr) {
            restoreTransportAndEngine();
            setupFailed_ = true;
            setupResult_ = failure("Could not create an audio writer for the requested format.");
            return;
        }
        stream.release(); // the writer owns the stream from here

        stems_.push_back(std::move(sw));
    }

    // Every writer opened cleanly - only now arm the taps. A partial failure above must never leave
    // any strip's tap pointing at a buffer this session is about to destroy.
    for (auto& stem : stems_)
        stem.strip->setStemTapBuffer(&stem.tapBuffer);

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

    // ---- Choreography start - identical to BounceSession's ----
    transport.stop();
    transport.setLoop(beforeLoopStartPpq_, beforeLoopEndPpq_, false);
    transport.locateBeat(options_.startBeat);
    transport.play();
}

StemSession::~StemSession() {
    if (!finished_)
        finish();
}

void StemSession::disarmAllTaps() noexcept {
    for (auto& stem : stems_)
        if (stem.strip != nullptr)
            stem.strip->setStemTapBuffer(nullptr);
}

void StemSession::restoreTransportAndEngine() {
    // Disarmed BEFORE the flush-block below, so that block (and anything rendered after this
    // session is gone) never writes into a buffer this session is about to destroy.
    disarmAllTaps();

    auto& transport = engine_.getTransport();
    transport.stop();
    transport.setLoop(beforeLoopStartPpq_, beforeLoopEndPpq_, beforeLooping_);
    transport.locateBeat(beforePpq_);
    if (driver_ != nullptr)
        driver_->streamBlocks(1, {});

    externalMidiGuard_.reset();

    if (deviceWasAttached_)
        engine_.resumeDeviceCallback();
    else if (previousSampleRate_ > 0.0 && previousBlockSize_ > 0)
        engine_.prepareForHost(previousSampleRate_, previousBlockSize_, previousInputChannels_,
                               previousOutputChannels_);
}

bool StemSession::stepRange(int maxBlocks) {
    if (isRangeDone())
        return true;

    // The one place audio leaves the render - every stem writer gets a slice of the SAME block, all
    // already sitting in each strip's tapBuffer by the time this fires (OfflineTransportDriver hands
    // the callback the block only after the whole graph pass, taps included, has run).
    const auto streamToWriter = [this](const juce::AudioBuffer<float>& block, const BlockTimeInfo& info) {
        if (info.bpm > 0.0)
            nextBlockBpm_ = info.bpm;
        nextBlockBeat_ = info.endPpq;

        if (cancelled_ || writeFailed_)
            return;

        for (auto& stem : stems_) {
            if (!stem.writer->writeFromAudioSampleBuffer(stem.tapBuffer, 0, block.getNumSamples())) {
                writeFailed_ = true;
                engine_.getTransport().stop();
                return;
            }
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
        // Same reason BounceSession does this: streamToBeat reads the cross-thread position
        // snapshot, which the constructor's locate has not reached yet on the very first tick.
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

bool StemSession::stepTail(int maxBlocks) {
    if (isTailDone())
        return true;
    if (!isRangeDone())
        return false;

    const auto streamToWriter = [this](const juce::AudioBuffer<float>& block, const BlockTimeInfo& info) {
        if (info.bpm > 0.0)
            nextBlockBpm_ = info.bpm;
        nextBlockBeat_ = info.endPpq;

        if (cancelled_ || writeFailed_)
            return;

        for (auto& stem : stems_) {
            if (!stem.writer->writeFromAudioSampleBuffer(stem.tapBuffer, 0, block.getNumSamples())) {
                writeFailed_ = true;
                return;
            }
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
            tailBlocksRemaining_ = 0;
    }

    tailDone_ = tailBlocksRemaining_ <= 0;
    return tailDone_;
}

StemResult StemSession::finish() {
    if (finished_)
        return finishedResult_;
    finished_ = true;

    if (setupFailed_) {
        finishedResult_ = setupResult_;
        return finishedResult_;
    }

    // Abandoned mid-render, same as BounceSession::finish().
    if (!rangeDone_ || !tailDone_)
        cancelled_ = true;

    // Flush and close every writer before any file is moved or inspected.
    for (auto& stem : stems_)
        stem.writer.reset();
    restoreTransportAndEngine();

    StemResult result;
    result.samplesWritten = samplesWritten_;
    result.streamDropouts = streamDropouts_;

    if (cancelled_) {
        // Every temp file dies with its `stems_` entry; no target was ever touched.
        result.message = "Stem export cancelled.";
        finishedResult_ = result;
        return finishedResult_;
    }

    if (writeFailed_) {
        result.message = "Failed while writing stems to \"" + destinationFolder_.getFullPathName() + "\".";
        finishedResult_ = result;
        return finishedResult_;
    }

    // Move every stem into place only once every one of them rendered successfully - see the header
    // comment for why this cannot be perfectly atomic across N independent filesystem renames, and
    // why that residual gap does not matter for the property this export exists to guarantee.
    for (auto& stem : stems_) {
        if (!stem.temporary->overwriteTargetFileWithTemporary()) {
            result.message = "Could not move the rendered stems into \"" + destinationFolder_.getFullPathName() + "\".";
            finishedResult_ = result;
            return finishedResult_;
        }
        result.stemFiles.add(stem.finalFile);
    }

    if (progress_)
        progress_(1.0);

    result.ok = true;
    result.message = "Exported " + juce::String(stems_.size()) + " stem(s) (" + juce::String(samplesWritten_) +
                     " samples each) to \"" + destinationFolder_.getFileName() + "\".";
    if (streamDropouts_ > 0)
        result.message +=
            " " + juce::String(streamDropouts_) + " block(s) played silence while waiting for audio clips.";
    finishedResult_ = result;
    return finishedResult_;
}

double StemSession::getProgress() const noexcept {
    return expectedTotalSamples_ > 0 ? juce::jmin(1.0, (double)samplesWritten_ / (double)expectedTotalSamples_) : 0.0;
}

} // namespace synth
