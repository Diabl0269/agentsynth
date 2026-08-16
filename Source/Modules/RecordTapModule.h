#pragma once

#include "../Timeline/PeaksFile.h"
#include "ModuleBase.h"
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <utility>
#include <vector>

/**
 * @brief "Rec Tap" — the graph-side end of an audio take.
 *
 * A stereo PASS-THROUGH that copies whatever is written into it, verbatim, to its output — while
 * ALSO shipping a copy of those samples to a WAV file on a background thread. The node is
 * transparent whether armed or not, so recording is audibly free.
 *
 * INTERNAL-ONLY, the same way "Track In" is: not in the module library, not offered as a
 * replacement type, and non-authorable by the AI (`kNonAuthorableModuleTypes` — a model that could
 * author one could point a recording at a file path of its choosing).
 *
 * Three threads:
 *   AUDIO THREAD   `processBlock` passes the block through and, when armed, pushes a copy into a
 *                  pre-allocated SPSC ring. No allocation, no locks, no logging. A full ring DROPS
 *                  samples and sets an overrun flag rather than blocking (see TakeResult::overran).
 *   WRITER THREAD  a `juce::TimeSliceThread` this module owns, started on the first startCapture()
 *                  and left running (idle) until destruction. Its one client drains the ring into a
 *                  plain `juce::AudioFormatWriter` and accumulates peaks as it goes.
 *   MESSAGE THREAD `startCapture`/`stopCapture`/`isCapturing`. stopCapture() detaches the writer
 *                  client (blocking until any in-flight drain finishes), drains what is left,
 *                  finalises the WAV and writes the peaks sidecar. The writer thread is never
 *                  joined — it stays alive, idle, for the next take.
 *
 * Ring: one interleaved SPSC ring of `kDefaultRingCapacityFrames` frames (~2.7s at 48kHz), sized so
 * the writer thread can be descheduled for seconds without dropping a sample.
 *
 * Peaks sidecar (".agpk"): format and accumulation live in `synth::PeaksFile`
 * (`Source/Timeline/PeaksFile.h/.cpp`); this module owns a `PeaksFile::Accumulator` (writer thread
 * appends under `peaksLock_`) and calls `PeaksFile::write` at `stopCapture()`.
 * `copyLivePeaks()` is a MESSAGE-THREAD, thread-safe snapshot of the buckets accumulated so far
 * (same `peaksLock_`), for a clip-lane strip that grows while the take is still rolling — never
 * touched by the audio thread.
 */
class RecordTapModule : public ModuleBase {
public:
    /** Channels the tap carries. Fixed for the node's lifetime (JUCE settles the bus layout in the
     *  ModuleBase constructor, and renegotiating it would drop every connection the node has). */
    static constexpr int kNumChannels = 2;

    /** Frames of ring capacity. See "Ring sizing" above for why this number. */
    static constexpr int kDefaultRingCapacityFrames = 1 << 17;

    /** Source samples per peak bucket. Format — a reader takes it from the file header, but every
     *  file this build writes uses this value. */
    static constexpr int kPeakBucketSize = 256;

    /** Peaks-file magic and version. Format; see the class comment. Aliases of
     *  `synth::PeaksFile::kMagic`/`kVersion` — that class is the one place the format is defined
     *  now, but these stay so existing call sites (and `RecordTapTests.cpp`) need no changes. */
    static constexpr std::uint32_t kPeaksMagic = synth::PeaksFile::kMagic;
    static constexpr std::uint32_t kPeaksVersion = synth::PeaksFile::kVersion;

    /** Frames the writer thread moves per time slice. Bounds the scratch buffer and the time spent
     *  in one callback; the client simply comes straight back for more while the ring is non-empty. */
    static constexpr int kDrainChunkFrames = 8192;

    /** @param ringCapacityFrames capacity of the audio->writer ring, in frames. The default is the
     *         only value production uses; tests pass a tiny one to provoke an overrun. */
    explicit RecordTapModule(int ringCapacityFrames = kDefaultRingCapacityFrames);
    ~RecordTapModule() override;

    // ---- juce::AudioProcessor ----
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::RecordTap; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    juce::String getInputPortLabel(int channelIndex) const override { return channelIndex == 0 ? "Left" : "Right"; }
    juce::String getOutputPortLabel(int channelIndex) const override { return channelIndex == 0 ? "Left" : "Right"; }

    LogicalPort mapInputChannel(int rawChannel) const override { return audioPort(rawChannel); }
    LogicalPort mapOutputChannel(int rawChannel) const override { return audioPort(rawChannel); }

    // ---- Message thread ----

    /** What a finished take amounts to.
     *  @param ok            a capture was running and both files were finalised
     *  @param lengthSamples frames actually written per channel — exactly the WAV's length
     *  @param overran       the ring filled at some point, so `lengthSamples` is short of what was
     *                       played. The take is still committed; the caller warns.
     *  @param captureStartValid          whether the two fields below mean anything (see below)
     *  @param captureStartTimelineSample the TRANSPORT sample position at which the take's frame 0
     *                       was captured — `BlockTimeInfo::blockStartSample` of the first block this
     *                       tap pushed, read on the AUDIO thread. This is the sample-honest anchor
     *                       the commit places the clip from; without it a take can only be placed
     *                       at whatever beat the message thread happened to observe.
     *
     *                       Invalid (`captureStartValid == false`) when no block was ever captured,
     *                       or when the tap is not running under a synth::TransportService playhead
     *                       (a bare unit test, a foreign host) — the commit then falls back to the
     *                       punch beat.
     *  @param captureStartBlockOffset frames of that first block that were NOT captured. Always 0
     *                       today: `capturing_` is read once at the top of processBlock, so a take
     *                       always begins at sample 0 of a block. It is reported rather than assumed
     *                       so the commit's arithmetic states its anchor in full. */
    struct TakeResult {
        bool ok = false;
        juce::int64 lengthSamples = 0;
        bool overran = false;
        bool captureStartValid = false;
        juce::int64 captureStartTimelineSample = 0;
        int captureStartBlockOffset = 0;
    };

    /** MESSAGE THREAD. Opens `wavFile` (32-bit IEEE-float WAV at `sampleRate`) and arms the audio
     *  thread. `peaksFile` is remembered and written by stopCapture(), not now. Returns false — and
     *  changes nothing — if a capture is already running, if the format is nonsense, or if the WAV
     *  could not be opened for writing. Parent directories are created if missing. */
    bool startCapture(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate, int numChannels);

    /** MESSAGE THREAD. Disarms, drains what is left of the ring, finalises the WAV and writes the
     *  peaks sidecar. Safe (and a no-op returning `ok == false`) when no capture is running. The
     *  writer thread is NOT joined — it stays alive and idle for the next take. */
    TakeResult stopCapture();

    /** Any thread. True between a successful startCapture() and the matching stopCapture(). */
    bool isCapturing() const noexcept { return capturing_.load(std::memory_order_acquire); }

    /** Any thread. Frames PUSHED INTO THE RING so far this take (dropped samples excluded) — a live
     *  progress figure. It can run a block ahead of what is actually in the file; the authoritative
     *  file length is TakeResult::lengthSamples, which is counted on the writing side. */
    juce::int64 getCapturedSamples() const noexcept { return capturedFrames_.load(std::memory_order_relaxed); }

    /** Any thread. True if the ring has filled at any point since the last startCapture(). */
    bool hadOverrun() const noexcept { return overrun_.load(std::memory_order_relaxed); }

    /** Any thread. The take's frame-0 anchor as described on TakeResult — available WHILE the
     *  take is still rolling (the live recording strip wants it), not only at stopCapture(). Returns
     *  false, leaving `timelineSample` untouched, until the first block has actually been captured. */
    bool getCaptureStartTimelineSample(juce::int64& timelineSample) const noexcept {
        if (!captureStartValid_.load(std::memory_order_acquire))
            return false;
        timelineSample = captureStartTimelineSample_.load(std::memory_order_relaxed);
        return true;
    }

    /** MESSAGE THREAD, after stopCapture(). The peaks exactly as they were written to the sidecar,
     *  flattened to `2 * numChannels` floats per bucket (min, max per channel) — the shape
     *  RecordTapTests.cpp's own byte-level parser expects. Derived from the same accumulator
     *  copyLivePeaks() reads, under the same lock. Cleared by startCapture(). */
    std::vector<float> getPeaksForTest() const;

    /** MESSAGE THREAD. A guarded snapshot of the buckets accumulated so far THIS TAKE — safe to
     *  call while `isCapturing()` is true, i.e. while the writer thread is concurrently appending
     *  to the same accumulator. Channel-interleaved per bucket, exactly `synth::PeaksFile::
     *  Data::buckets`' own layout (this IS that vector, copied). Only ever contains COMPLETE
     *  buckets — the bucket currently being filled is not flushed until it reaches
     *  `kPeakBucketSize` samples or the take stops, so a live strip updates in ~`kPeakBucketSize /
     *  sampleRate` steps, not sample-by-sample. `out` is cleared and replaced (a plain copy, not
     *  merged) whether or not anything has accumulated yet. */
    void copyLivePeaks(std::vector<std::pair<float, float>>& out) const;

private:
    // The TimeSliceThread's one client. A separate object rather than making the module itself a
    // TimeSliceClient, so "the module" and "the thing the writer thread calls" are never confused
    // for each other at a call site.
    struct WriterClient : public juce::TimeSliceClient {
        explicit WriterClient(RecordTapModule& o)
            : owner(o) {}
        int useTimeSlice() override;
        RecordTapModule& owner;
    };

    static LogicalPort audioPort(int rawChannel) noexcept {
        LogicalPort p;
        p.visibleJackIndex = juce::jlimit(0, kNumChannels - 1, rawChannel);
        p.role = PortRole::Audio;
        p.isPolyGroupHead = true;
        p.polyVoiceSpan = 1;
        return p;
    }

    // WRITER THREAD (and, after the client is detached, the message thread inside stopCapture()).
    // Moves up to kDrainChunkFrames frames out of the ring into the writer, accumulating peaks.
    // Returns true if it moved anything.
    bool drainOnce();

    // Peak accumulation over one de-interleaved chunk, and the flush of a partially-filled bucket
    // at end-of-take. Writer/message thread only — never the audio thread. Both just delegate to
    // peaksAccumulator_ under peaksLock_ — see that member's comment for why the lock exists.
    void accumulatePeaks(const juce::AudioBuffer<float>& chunk, int numFrames);
    void flushPartialBucket();

    // MESSAGE THREAD, from stopCapture(). Writes the .agpk sidecar described in the class comment.
    bool writePeaksFile() const;

    const int ringCapacityFrames_;

    // SPSC ring: the audio thread writes (processBlock), the writer thread reads (drainOnce).
    // Interleaved, kNumChannels floats per frame. Modelled on synth::MidiRecorder's ring — a
    // fixed-size juce::AbstractFifo over storage allocated once, in the constructor.
    juce::AbstractFifo ring_;
    std::vector<float> ringStorage_;

    // De-interleaving scratch for the drain, sized once in the constructor.
    juce::AudioBuffer<float> drainScratch_;

    // Armed flag. Release/acquire (not relaxed): it publishes captureChannels_ and the writer's
    // existence to the audio thread, which must not read a half-set-up capture.
    std::atomic<bool> capturing_{false};
    std::atomic<bool> overrun_{false};
    std::atomic<juce::int64> capturedFrames_{0};
    // Frames handed to the writer — the file's true length. Written only from inside drainOnce(),
    // i.e. by the writer thread and (after the client is detached) by stopCapture() on the message
    // thread; never by both at once. Atomic so the hand-off between those two needs no reasoning.
    std::atomic<juce::int64> writtenFrames_{0};
    // Written by startCapture() BEFORE capturing_ goes true and never touched again until the next
    // startCapture(), so the audio thread always sees the value belonging to the take it is in.
    std::atomic<int> captureChannels_{kNumChannels};

    // The take's frame-0 anchor. Cleared by startCapture() BEFORE arming; written exactly
    // once per take by the audio thread, on the first block it pushes, sample-then-flag with a
    // release so a message-thread reader that sees the flag also sees the sample. Read by
    // getCaptureStartTimelineSample() / stopCapture(). See TakeResult for what it means.
    std::atomic<bool> captureStartValid_{false};
    std::atomic<juce::int64> captureStartTimelineSample_{0};
    std::atomic<int> captureStartBlockOffset_{0};

    // Message-thread-only bookkeeping.
    double preparedSampleRate_ = 44100.0;
    juce::File peaksFile_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;

    // Peak accumulation state, carried across drains so a bucket may span two of them. Guards
    // BOTH the writer thread's appends (accumulatePeaks/flushPartialBucket) and the message
    // thread's reads (copyLivePeaks(), mid-take; writePeaksFile(), only after stopCapture() has
    // already detached the writer client, so uncontended there but locked anyway for one uniform
    // rule) — never taken on the audio thread, which never touches either member.
    mutable juce::CriticalSection peaksLock_;
    synth::PeaksFile::Accumulator peaksAccumulator_{kPeakBucketSize, kNumChannels};

    juce::TimeSliceThread writerThread_{"Rec Tap Writer"};
    WriterClient writerClient_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordTapModule)
};
