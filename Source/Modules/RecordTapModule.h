#pragma once

#include "ModuleBase.h"
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

/**
 * @brief "Rec Tap" — the graph-side end of an audio take (TL6-3).
 *
 * A stereo PASS-THROUGH that copies whatever is written into it, verbatim, to its output — while
 * ALSO shipping a copy of those samples to a WAV file on a background thread. Recording a take is
 * therefore audibly free: the node is transparent whether it is armed or not, and a patch with a
 * tap in it sounds exactly like the same patch without one.
 *
 * The module is INTERNAL-ONLY, the same way "Track In" is: not in the module library, not offered
 * as a replacement type, and explicitly non-authorable by the AI (`kNonAuthorableModuleTypes` —
 * a model that could author one could point a recording at a file path of its choosing).
 *
 * -- The three threads --------------------------------------------------------------------------
 *
 *   AUDIO THREAD   `processBlock` — passes the block through, and (when armed) pushes a copy of it
 *                  into a pre-allocated SPSC ring. No allocation, no locks, no logging, ever. A
 *                  full ring DROPS samples and sets an overrun flag rather than blocking; the take
 *                  still commits, just short (see TakeResult::overran).
 *
 *   WRITER THREAD  a `juce::TimeSliceThread` this module owns, started on the first startCapture()
 *                  and left running (idle) until the module is destroyed. Its one client drains the
 *                  ring straight into a plain `juce::AudioFormatWriter` and accumulates peaks as it
 *                  goes.
 *
 *                  Deliberately NOT `AudioFormatWriter::ThreadedWriter`: that class is its own ring
 *                  plus its own time-slice client, so stacking it on top of ours would mean two
 *                  buffers, two overrun policies and two answers to "how many samples are in the
 *                  file so far". One ring, one writer, one counter.
 *
 *   MESSAGE THREAD `startCapture` / `stopCapture` / `isCapturing`. stopCapture() detaches the
 *                  writer client (which blocks until any in-flight drain has finished), drains
 *                  whatever is left itself, finalises the WAV and writes the peaks sidecar. The
 *                  thread itself is never joined there — it stays alive, idle, ready for the next
 *                  take.
 *
 * -- Ring sizing --------------------------------------------------------------------------------
 *
 * One INTERLEAVED ring of `kDefaultRingCapacityFrames` frames (a frame = one sample per channel),
 * so a block is one contiguous copy per FIFO region rather than one per channel, and a "frame" is
 * the same unit the sample counter and the WAV length are measured in.
 *
 * 1 << 17 = 131072 frames is 2.7 s at 48 kHz and 1 MiB of float storage for the stereo case — well
 * over the ~1 s of slack the brief asks for, and a power of two so the FIFO's wrap arithmetic stays
 * trivial. The cost is fixed and paid once at construction; the benefit is that the writer thread
 * can be descheduled for seconds (a spinning disk, a busy machine) without dropping a sample.
 *
 * -- Peaks sidecar: the ".agpk" format ----------------------------------------------------------
 *
 * Accumulated during the drain, so the file is ready the moment the take stops and nothing has to
 * re-read the WAV to draw it (TL6-5 renders the clip waveform straight from this).
 *
 * Binary, LITTLE-ENDIAN throughout:
 *
 *     offset  size  field
 *     0       4     magic, the ASCII bytes 'A','G','P','K'
 *     4       4     uint32  version           (currently 1)
 *     8       4     uint32  bucketSize        (source samples per bucket, currently 256)
 *     12      4     uint32  numChannels
 *     16      ...   per bucket, per channel: float32 min, float32 max
 *
 * A bucket covers `bucketSize` SOURCE SAMPLES PER CHANNEL. The bucket count is
 * `ceil(lengthSamples / bucketSize)` — the final bucket is short rather than padded, and its
 * min/max cover only the samples that actually exist. A take of zero samples writes a header and
 * no buckets.
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

    /** Peaks-file magic and version. Format; see the class comment. */
    static constexpr std::uint32_t kPeaksMagic = 0x4b475041u; // 'A','G','P','K' little-endian
    static constexpr std::uint32_t kPeaksVersion = 1;

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
     *                       played. The take is still committed; the caller warns. */
    struct TakeResult {
        bool ok = false;
        juce::int64 lengthSamples = 0;
        bool overran = false;
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

    /** MESSAGE THREAD, after stopCapture(). The peaks exactly as they were written to the sidecar:
     *  `2 * numChannels` floats per bucket, ordered min, max per channel. Cleared by startCapture(). */
    const std::vector<float>& getPeaksForTest() const noexcept { return peaks_; }

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
    // at end-of-take. Writer/message thread only — never the audio thread.
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

    // Message-thread-only bookkeeping.
    double preparedSampleRate_ = 44100.0;
    juce::File peaksFile_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;

    // Peak accumulation state, carried across drains so a bucket may span two of them.
    std::vector<float> peaks_;
    int bucketFill_ = 0;
    float bucketMin_[kNumChannels]{};
    float bucketMax_[kNumChannels]{};

    juce::TimeSliceThread writerThread_{"Rec Tap Writer"};
    WriterClient writerClient_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordTapModule)
};
