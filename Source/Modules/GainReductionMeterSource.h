#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

/** Live gain-reduction telemetry for a dynamics module's card view.
 *
 *  Same threading contract as ThresholdMeterSource: the audio thread writes, any other thread may
 *  read, and every getter is a lock-free atomic load. The reading is the reduction the module is
 *  applying right now, in positive decibels (0 = untouched, 6 = the signal is 6 dB quieter than it
 *  would be without the module). Modules that report it today: Compressor and Limiter.
 */
class GainReductionMeterSource {
public:
    virtual ~GainReductionMeterSource() = default;

    /** Current gain reduction in decibels, never negative. Peak-held and decayed by the module
     *  (see GainReductionMeter), so a poll at UI rate does not miss a short transient. */
    virtual float getGainReductionDb() const = 0;
};

namespace synth {

/** The audio-thread half of a GainReductionMeterSource: `push` once per block with the deepest
 *  reduction seen in it, `get` from anywhere. A deeper reading is taken at once; a shallower one
 *  falls at `kDecayDbPerSecond`, so the meter hangs on a transient and then settles. */
class GainReductionMeter {
public:
    static constexpr float kDecayDbPerSecond = 40.0f;

    /** Audio thread only. `blockMaxReductionDb` may be any sign; negative counts as 0. */
    void push(float blockMaxReductionDb, int numSamples, double sampleRate) noexcept {
        const float seconds = sampleRate > 0.0 ? (float)((double)numSamples / sampleRate) : 0.0f;
        const float fallen = std::max(0.0f, held_ - kDecayDbPerSecond * seconds);
        held_ = std::max(std::max(0.0f, blockMaxReductionDb), fallen);
        shown_.store(held_, std::memory_order_relaxed);
    }

    /** Audio thread only: the meter reads 0 again (bypass, mute, reset). */
    void clear() noexcept {
        held_ = 0.0f;
        shown_.store(0.0f, std::memory_order_relaxed);
    }

    float get() const noexcept { return shown_.load(std::memory_order_relaxed); }

private:
    float held_ = 0.0f;
    std::atomic<float> shown_{0.0f};
};

} // namespace synth
