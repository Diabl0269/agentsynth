#pragma once

#include <array>
#include <atomic>

// PeakMeterLatch.h -- FRO146 (docs/mixer.md meters section): a lock-free "peak since I last
// looked" latch, one per output leg of a metered module (ChannelStripModule/MasterModule).
//
// THE MISSED-OVERS BUG THIS REPLACES. The old scheme stored exactly one float per leg, overwritten
// every block (ChannelStripModule/MasterModule's own `meterPeakL_`/`meterPeakR_`). A hot block
// landing between two 10 Hz UI polls was silently gone by the next poll -- read back as whatever
// the LAST block happened to be, which is very often quiet. A latch fixes this by keeping the
// loudest block the audio thread has produced SINCE EACH READER'S OWN LAST READ, not since the
// last block.
//
// PER-READER, NOT SHARED. The mixer column and a track header's channel chip both read the same
// strip's meter, at their own independent cadence -- a single shared "peak since last read" would
// let one reader's read silently clear the peak out from under the other. Each MeterReader gets
// its own slot per leg (see the enum below), so `takePeak(Mixer, ...)` and
// `takePeak(TrackHeader, ...)` never interact. NEVER read-and-reset another reader's slot -- add a
// new enumerator for a new consumer rather than repurposing an existing one.
//
// REAL-TIME SAFE. `storeBlockPeak()` is a CAS loop with no lock and no allocation; the only
// contention is the one audio thread against itself (nothing else ever writes), so it always
// terminates promptly.

namespace synth {

/** One enumerator per independent consumer of a metered module's peak. Add a new one for a new
 *  consumer -- never share an existing reader's slot across two logically different pollers. */
enum class MeterReader : int { Mixer = 0, TrackHeader = 1, Count = 2 };

inline constexpr int kMeterReaderCount = static_cast<int>(MeterReader::Count);

/** One leg's (Left or Right) worth of per-reader latches. A metered module owns one of these per
 *  leg -- e.g. `std::array<PeakMeterLatch, 2> meterLatches_;` indexed 0 = Left, 1 = Right. */
class PeakMeterLatch {
public:
    PeakMeterLatch() noexcept { reset(); }

    /** Audio thread, once per block per leg: raises every reader's latched peak to at least
     *  `blockPeak`, never lowers it (a reader that hasn't looked yet must still see the max).
     *  NaN-safe: `blockPeak > current` is false for a NaN input, so a NaN block is dropped
     *  entirely rather than latching NaN into a slot forever. Non-positive input (silence, or a
     *  negative magnitude that should never occur) is likewise a no-op -- there is nothing to
     *  raise a slot to below its resting 0. */
    void storeBlockPeak(float blockPeak) noexcept {
        if (!(blockPeak > 0.0f))
            return;
        for (auto& slot : slots_) {
            float current = slot.load(std::memory_order_relaxed);
            while (blockPeak > current && !slot.compare_exchange_weak(current, blockPeak, std::memory_order_relaxed)) {
                // compare_exchange_weak reloads `current` with the latest value on a failed
                // attempt (either real contention, which never happens here since only the audio
                // thread ever writes, or a spurious failure) -- the loop condition re-checks it.
            }
        }
    }

    /** Reader thread: returns the max latched since THIS reader's last call, and resets only its
     *  own slot to 0 -- read-and-reset, so two readers never see (or clear) each other's peaks. */
    float takePeak(MeterReader reader) noexcept {
        return slots_[static_cast<size_t>(reader)].exchange(0.0f, std::memory_order_relaxed);
    }

    /** Resets every reader's slot to 0 -- call from prepareToPlay() so a re-prepared module never
     *  hands a reader a peak latched before the transport last stopped. */
    void reset() noexcept {
        for (auto& slot : slots_)
            slot.store(0.0f, std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<float>, kMeterReaderCount> slots_;
};

} // namespace synth
