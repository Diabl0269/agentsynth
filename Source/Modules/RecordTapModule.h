#pragma once

#include "../Timeline/PeaksFile.h"
#include "../Timeline/TakeCaptureWriter.h"
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
 * A take never spans a sample-rate change: the WAV header is written at startCapture()'s rate, so
 * `prepareToPlay` at any other rate halts the push at that boundary (see `captureHalted_`) and the
 * message thread commits the short take through its ordinary path.
 *
 * The ring, writer client, WAV and peaks machinery is `synth::TakeCaptureWriter`, shared with the engine's
 * input recorder; this module owns one writer and the thread it drains on.
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
    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::RecordTap; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    juce::String getInputPortLabel(int channelIndex) const override { return channelIndex == 0 ? "Left" : "Right"; }
    juce::String getOutputPortLabel(int channelIndex) const override { return channelIndex == 0 ? "Left" : "Right"; }

    LogicalPort mapInputChannel(int rawChannel) const override { return audioPort(rawChannel); }
    LogicalPort mapOutputChannel(int rawChannel) const override { return audioPort(rawChannel); }

    // ---- Message thread ----

    /** What a finished take amounts to; see synth::TakeCaptureWriter::TakeResult. `captureStartTimelineSample`
     *  is `BlockTimeInfo::blockStartSample` of the first captured block, invalid without a
     *  synth::TransportService playhead (the commit then falls back to the punch beat). */
    using TakeResult = synth::TakeCaptureWriter::TakeResult;

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
    bool isCapturing() const noexcept { return capture_.isCapturing(); }

    /** Any thread. Frames PUSHED INTO THE RING so far this take (dropped samples excluded) — a live
     *  progress figure. It can run a block ahead of what is actually in the file; the authoritative
     *  file length is TakeResult::lengthSamples, which is counted on the writing side. */
    juce::int64 getCapturedSamples() const noexcept { return capture_.getCapturedSamples(); }

    /** Any thread. True if the ring has filled at any point since the last startCapture(). */
    bool hadOverrun() const noexcept { return capture_.hadOverrun(); }

    /** Any thread. True once the prepared sample rate stopped matching the rate the take was armed
     *  at: the audio thread stopped pushing at that exact boundary, so the file holds only samples
     *  recorded at its own header rate. `isCapturing()` stays true — the take is still the message
     *  thread's to commit (see prepareToPlay). Cleared by the next startCapture(). */
    bool wasCaptureHaltedByFormatChange() const noexcept { return capture_.wasHaltedByFormatChange(); }

    /** Any thread. The take's frame-0 anchor as described on TakeResult — available WHILE the
     *  take is still rolling (the live recording strip wants it), not only at stopCapture(). Returns
     *  false, leaving `timelineSample` untouched, until the first block has actually been captured. */
    bool getCaptureStartTimelineSample(juce::int64& timelineSample) const noexcept {
        return capture_.getCaptureStartTimelineSample(timelineSample);
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
    void copyLivePeaks(std::vector<std::pair<float, float>>& out) const { capture_.copyLivePeaks(out); }

private:
    static LogicalPort audioPort(int rawChannel) noexcept {
        LogicalPort p;
        p.visibleJackIndex = juce::jlimit(0, kNumChannels - 1, rawChannel);
        p.role = PortRole::Audio;
        p.isPolyGroupHead = true;
        p.polyVoiceSpan = 1;
        return p;
    }

    // Declared before capture_ so the writer (which detaches its client on destruction) dies first.
    juce::TimeSliceThread writerThread_{"Rec Tap Writer"};
    synth::TakeCaptureWriter capture_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordTapModule)
};
