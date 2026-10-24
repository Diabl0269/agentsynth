#pragma once

#include "Transport/TransportService.h"
#include <algorithm>

namespace synth::ui {

// Keeps the latency-shifted playhead from jumping back early when a loop wraps. The transport position wraps as
// soon as the block being rendered crosses the loop end, but what is heard is `latency` behind it, so a plain
// shift would turn the line around while the sound is still `latency` short of the loop end.
class PlayheadWrapCompensation {
public:
    // Feed every transport snapshot, in order, with the output latency in beats.
    void observe(const TransportService::PositionSnapshot& snapshot, double latencyBeats) noexcept;

    // The beat to draw for `snapshot`: the heard beat, kept at the end of the loop until the sound itself wraps.
    double drawnBeat(const TransportService::PositionSnapshot& snapshot, double latencyBeats) const noexcept;

    bool isWrapped() const noexcept { return wrapped_; }

private:
    bool havePrevious_ = false;
    bool wrapped_ = false;
    double previousPpq_ = 0.0;
    double loopStartPpq_ = 0.0;
    double loopEndPpq_ = 0.0;
};

inline void PlayheadWrapCompensation::observe(const TransportService::PositionSnapshot& snapshot,
                                              double latencyBeats) noexcept {
    // Snapshots arrive from a 10 Hz poll and a 30 Hz frame timer, so a wrap can be seen up to one poll late.
    const double slackBeats = std::max(0.0, snapshot.bpm) / 60.0 * 0.15;
    const double window = std::max(0.0, latencyBeats) + slackBeats;

    const bool loopChanged = snapshot.loopStartPpq != loopStartPpq_ || snapshot.loopEndPpq != loopEndPpq_;
    const bool active = snapshot.playing && snapshot.looping && snapshot.loopEndPpq > snapshot.loopStartPpq;

    if (!active || loopChanged) {
        wrapped_ = false;
    } else if (havePrevious_) {
        const bool movedBack = snapshot.ppq < previousPpq_;
        const bool jumpedAhead = snapshot.ppq - previousPpq_ > window;
        const bool fellAcrossLoopEnd =
            movedBack && previousPpq_ >= snapshot.loopEndPpq - window && snapshot.ppq - snapshot.loopStartPpq < window;
        if (fellAcrossLoopEnd)
            wrapped_ = true;
        else if (movedBack || jumpedAhead)
            wrapped_ = false; // a locate
        if (snapshot.ppq - latencyBeats >= snapshot.loopStartPpq)
            wrapped_ = false; // the sound has caught up with the new pass
    }

    havePrevious_ = active;
    previousPpq_ = snapshot.ppq;
    loopStartPpq_ = snapshot.loopStartPpq;
    loopEndPpq_ = snapshot.loopEndPpq;
}

inline double PlayheadWrapCompensation::drawnBeat(const TransportService::PositionSnapshot& snapshot,
                                                  double latencyBeats) const noexcept {
    if (!snapshot.playing)
        return std::max(0.0, snapshot.ppq);
    const double heard = snapshot.ppq - latencyBeats;
    if (wrapped_ && heard < snapshot.loopStartPpq)
        return heard + (snapshot.loopEndPpq - snapshot.loopStartPpq);
    return std::max(0.0, heard);
}

} // namespace synth::ui
