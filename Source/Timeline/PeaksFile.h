#pragma once

#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace synth {

/**
 * @brief TL6-5: the ONE place that knows the ".agpk" waveform-peaks sidecar format.
 *
 * `RecordTapModule` (`Source/Modules/RecordTapModule.h/.cpp`) originated this format and wrote it
 * inline; this class is that logic factored out so a second consumer — `TimelineClipLaneArea`'s
 * clip-waveform painter — can read it without depending on the recorder, and so a future writer
 * (an offline bounce, an imported-file peaks cache) has the same single implementation to call
 * rather than a second copy of the bucket math.
 *
 * ### On-disk format (binary, LITTLE-ENDIAN throughout — unchanged from RecordTapModule's original)
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
 * no buckets. `read()` cannot tell a short final bucket from a full one (that information is the
 * WAV's sample count, not this file's) — it only enforces the file's own STRUCTURE: every
 * recorded (min,max) pair belongs to some bucket, and every bucket has exactly `numChannels`
 * pairs. A short final bucket is therefore not something a reader ever "rejects"; it is simply a
 * bucket like any other, covering fewer source samples than the rest.
 */
class PeaksFile {
public:
    /** One fully-parsed (or about-to-be-written) .agpk file, in memory. `buckets` is
     *  CHANNEL-INTERLEAVED PER BUCKET, exactly the file's own byte order: for a stereo file,
     *  `buckets[2*i]` is bucket `i`'s channel-0 (min,max) and `buckets[2*i+1]` is its channel-1
     *  (min,max). `buckets.size()` is always a whole multiple of `numChannels`. */
    struct Data {
        int bucketSize = 0;
        int numChannels = 0;
        std::vector<std::pair<float, float>> buckets;
    };

    /** Peaks-file magic and version — format, not implementation detail. Bit-identical to the
     *  values `RecordTapModule` originally defined (`kPeaksMagic`/`kPeaksVersion`), which now
     *  alias these so every file this build has ever written still parses. */
    static constexpr std::uint32_t kMagic = 0x4b475041u; // 'A','G','P','K' little-endian
    static constexpr std::uint32_t kVersion = 1;

    /** Parses an .agpk file into `out`. Returns false — and leaves `out` untouched — on: a
     *  missing/unopenable file, a bad magic or an unsupported version, a zero bucketSize or
     *  numChannels, or a payload whose byte count isn't a whole number of (min,max) pairs forming
     *  whole buckets (garbage/truncated). Tolerant of a short FINAL bucket in the musical sense
     *  (see the class comment) — that is not a structural defect, so it is never a rejection
     *  reason. */
    static bool read(const juce::File& file, Data& out);

    /** Writes a complete .agpk file in one shot: the header, then `data.buckets` verbatim in
     *  file order. Creates parent directories if missing; truncates/overwrites an existing file.
     *  Returns false on any I/O failure or if `data.bucketSize`/`data.numChannels` is <= 0. */
    static bool write(const juce::File& file, const Data& data);

    /**
     * @brief Incremental bucket accumulation — the math `RecordTapModule`'s drain step used to do
     *        inline (`accumulatePeaks`/`flushPartialBucket`), factored out so there is exactly one
     *        implementation of "turn a stream of sample chunks into (min,max) buckets".
     *
     * NOT thread-safe by itself: an owner that reads `getData()` from one thread while another
     * calls `addSamples()`/`flushPartial()` must supply its own lock (see
     * `RecordTapModule::copyLivePeaks()`, which is exactly that owner).
     */
    class Accumulator {
    public:
        /** `bucketSize`/`numChannels` are clamped to >= 1 — matching RecordTapModule's own
         *  `jlimit` clamps on the values it used to store directly. */
        Accumulator(int bucketSize, int numChannels);

        /** Consumes one chunk of already-de-interleaved audio (channel 0..numChannels-1, `chunk`
         *  may carry MORE channels than this accumulator was built with — only the leading
         *  `numChannels` are read, mirroring RecordTapModule's own capture-channel clamp).
         *  Updates the in-progress bucket's per-channel min/max and flushes it to `data_.buckets`
         *  every time `bucketSize` samples have been seen. */
        void addSamples(const juce::AudioBuffer<float>& chunk, int numFrames);

        /** Flushes whatever has accumulated into the in-progress bucket, even if it is short of
         *  `bucketSize` samples — the short-final-bucket rule the class comment documents. A
         *  no-op if nothing has been seen since construction or the last flush (never emits an
         *  empty bucket). */
        void flushPartial();

        /** Empties `data_.buckets` and the in-progress bucket, keeping the same bucketSize/
         *  numChannels — what a fresh take needs without reconstructing the accumulator (and
         *  without losing whatever numChannels this take actually needs, which can differ from
         *  the previous one). */
        void reset();

        const Data& getData() const noexcept { return data_; }

    private:
        int bucketSize_;
        int numChannels_;
        Data data_;
        int bucketFill_ = 0;
        std::vector<float> bucketMin_;
        std::vector<float> bucketMax_;
    };

private:
    PeaksFile() = delete;
};

} // namespace synth
