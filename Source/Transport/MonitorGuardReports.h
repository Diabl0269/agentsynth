#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace synth {

// Where a track's monitoring feedback guard says it tripped: the audio thread files the track id, the message thread
// collects it once to tell the user. A fixed set of slots, lock-free and allocation-free on both sides.
class MonitorGuardReports {
public:
    static constexpr int kSlots = 16;

    // AUDIO THREAD. `trackId` must not be 0. A track already waiting is not filed twice; with every slot taken the
    // report is dropped (the track still mutes itself; only the message is lost).
    void report(std::int64_t trackId) noexcept {
        for (auto& slot : slots_) {
            std::int64_t expected = 0;
            if (slot.compare_exchange_strong(expected, trackId, std::memory_order_relaxed) || expected == trackId)
                return;
        }
    }

    // MESSAGE THREAD. Calls `onTrip(trackId)` for every report filed since the last call, and forgets them.
    template <typename OnTrip>
    void consume(OnTrip&& onTrip) {
        for (auto& slot : slots_)
            if (const std::int64_t id = slot.exchange(0, std::memory_order_relaxed); id != 0)
                onTrip(id);
    }

private:
    std::array<std::atomic<std::int64_t>, kSlots> slots_{};
};

} // namespace synth
