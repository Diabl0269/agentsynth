#pragma once

#include "Transport/TransportService.h"
#include <algorithm>
#include <cstdint>

namespace synth::ui {

// The transport publishes its position once per audio block, so a display that redraws faster than the blocks arrive
// would show the line standing still for a frame and then jumping two. This estimates where the transport is NOW from
// the last published position, the wall-clock time it was published at and the tempo, so every display frame moves the
// line by about the same amount. Pure and clock-injected: the caller passes `nowMs`, nothing here reads a clock.
//
// The estimate is `ppq + (nowMs - publishedAtMs) * bpm / 60000`, held to three rules:
//   - never behind the published position, and never more than kMaxAheadMs of beats in front of it (the audio block
//     length is not known here; 25 ms is a little over two blocks at 48 kHz / 512 frames);
//   - monotonic: a frame never goes back from the one before it ...
//   - ... unless the snapshot shows a jump (a locate, the position going backwards at a loop wrap, a loop setting that
//     changed, or the transport not playing), which resets to the published position at once;
//   - with a loop on, never past the loop end (the published position wraps there, the estimate must not overshoot).
// A snapshot with no publish time (publishedAtMs <= 0, hand-built) is passed through unchanged.
class PlayheadExtrapolator {
public:
    static constexpr double kMaxAheadMs = 25.0;

    // The position to use for `snapshot` seen at `nowMs`. Feed every snapshot, in order.
    double extrapolate(const TransportService::PositionSnapshot& snapshot, double nowMs) noexcept {
        const bool jump = !havePrevious_ || !snapshot.playing || snapshot.locateCount != locateCount_ ||
                          snapshot.ppq < previousRawPpq_ || snapshot.looping != looping_ ||
                          snapshot.loopStartPpq != loopStartPpq_ || snapshot.loopEndPpq != loopEndPpq_;

        double out = snapshot.ppq;
        if (snapshot.playing && snapshot.publishedAtMs > 0.0) {
            const double elapsedMs = std::clamp(nowMs - snapshot.publishedAtMs, 0.0, kMaxAheadMs);
            out = snapshot.ppq + elapsedMs / 1000.0 * std::max(0.0, snapshot.bpm) / 60.0;
            if (snapshot.looping && snapshot.loopEndPpq > snapshot.loopStartPpq && snapshot.ppq < snapshot.loopEndPpq)
                out = std::min(out, snapshot.loopEndPpq);
            if (!jump)
                out = std::max(out, previousOut_);
            out = std::max(out, snapshot.ppq);
        }

        havePrevious_ = snapshot.playing;
        previousRawPpq_ = snapshot.ppq;
        previousOut_ = out;
        locateCount_ = snapshot.locateCount;
        looping_ = snapshot.looping;
        loopStartPpq_ = snapshot.loopStartPpq;
        loopEndPpq_ = snapshot.loopEndPpq;
        return out;
    }

private:
    bool havePrevious_ = false;
    double previousRawPpq_ = 0.0;
    double previousOut_ = 0.0;
    std::uint32_t locateCount_ = 0;
    bool looping_ = false;
    double loopStartPpq_ = 0.0;
    double loopEndPpq_ = 0.0;
};

} // namespace synth::ui
