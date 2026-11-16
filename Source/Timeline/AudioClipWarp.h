#pragma once

// Live time-stretch for an audio clip that follows the song's tempo: one per streamer slot, fed from that slot's ring.

#include <atomic>
#include <cstdint>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace synth {

/** Where a ClipWarp reads its input: the slot's ring, in playback frames. Audio thread. */
struct ClipWarpSource {
    virtual ~ClipWarpSource() = default;
    /** Fills `numFrames` from `frame`, zeroing what is not there; returns the frames that were. */
    virtual int read(std::int64_t frame, float* left, float* right, int numFrames) const noexcept = 0;
};

/**
 * @brief Plays a clip's audio `ratio` times faster at the same pitch, picking up where the last block ended.
 *
 * `prepare` runs on the prefetch thread (it allocates) and waits for a render in progress; `render` runs on the
 * audio thread and never blocks: while `prepare` holds the state it renders silence.
 */
class ClipWarp {
public:
    /** The widest speed-up (and its reciprocal, the widest slow-down) played; beyond it the ratio is clamped. */
    static constexpr double kMaxRatio = 4.0;
    /** Output frames per stretch call: bounds the input scratch. */
    static constexpr int kChunkFrames = 512;
    /** How far (in playback frames) a block may start from where the last one ended and still continue it. */
    static constexpr double kContinueToleranceFrames = 16.0;

    ClipWarp();
    ~ClipWarp();

    /** Prefetch thread. Sets the stretch up for `sampleRate` (no-op when already at it). */
    void prepare(double sampleRate);
    double preparedSampleRate() const noexcept { return preparedRate_.load(std::memory_order_acquire); }

    /** Frames of input the first block after a seek needs from `position` at `ratio`, at the prepared rate. */
    int seekFrames(double ratio) const noexcept;

    /** Audio thread. `numFrames` of `clipId` from source position `position` (playback frames) at `ratio` source
     *  frames per output frame. Returns `numFrames`, or 0 when it wrote silence (not prepared, busy, or the ring
     *  cannot serve the start yet). */
    int render(const ClipWarpSource& source, std::int64_t clipId, double position, double ratio, float* left,
               float* right, int numFrames) noexcept;

private:
    bool seekTo(const ClipWarpSource& source, double position, double ratio) noexcept;

    struct Stretch;
    std::unique_ptr<Stretch> stretch_;
    juce::SpinLock lock_;
    std::atomic<double> preparedRate_{0.0};
    std::vector<float> inLeft_, inRight_;

    // Audio thread only (under lock_).
    bool running_ = false;
    std::int64_t clipId_ = 0;
    double expectedPosition_ = 0.0;
    std::int64_t nextInputFrame_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipWarp)
};

} // namespace synth
