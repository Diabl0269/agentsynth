// Concern: FRO271's cursor-move and loop-locator-jump transport actions (transportNudge*Beat/Bar,
// transportJumpToLoopStart/End) -- the target arithmetic and the message-thread accumulation state
// they share, kept free of MainComponent so it is unit-testable against a bare TransportService.
#pragma once

#include "TransportService.h"
#include <algorithm>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace synth {

/** Message-thread-only memory of the last position a cursor action asked for.
 *
 *  TransportService::locateBeat() only POSTS a command; the audio thread applies it on its next
 *  tick(), and getPositionSnapshot().ppq is refreshed once per audio block. A jog wheel or key
 *  repeat can fire several nudges inside one block, so each one must build on the previous
 *  REQUEST rather than on the stale snapshot -- otherwise all but the last are lost. */
struct TransportNudgeState {
    bool pending = false;
    double basePpq = 0.0;            // snapshot ppq the pending request was computed from
    std::int64_t baseSample = 0;     // snapshot sample position it was computed from
    double target = 0.0;             // the beat that request asked the transport to locate to
    std::uint32_t requestedAtMs = 0; // juce millisecond counter when it was posted
};

/** How long an unconsumed request stays authoritative. It has to outlast one audio block (a few
 *  ms to ~100 ms) yet expire before an unrelated relocate could leave the snapshot bit-identical
 *  to the one the request was based on. */
inline constexpr std::uint32_t kNudgeAccumulateWindowMs = 250;

/** Beats (quarter notes) in one bar of the snapshot's time signature. */
inline double beatsPerBarOf(const TransportService::PositionSnapshot& snap) noexcept {
    const double v = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
    return v > 0.0 ? v : 4.0;
}

/** The beat a nudge of `deltaBeats` should locate to, clamped at 0. Builds on the previous
 *  request while the snapshot is still the one it was based on (the audio thread has not applied
 *  it yet), else on the snapshot's own position. Records the new request in `state`. */
inline double computeNudgeTarget(TransportNudgeState& state, const TransportService::PositionSnapshot& snap,
                                 double deltaBeats, std::uint32_t nowMs) noexcept {
    const bool unconsumed = state.pending && state.baseSample == snap.samplePosition && state.basePpq == snap.ppq &&
                            (std::uint32_t)(nowMs - state.requestedAtMs) <= kNudgeAccumulateWindowMs;
    const double from = unconsumed ? state.target : snap.ppq;
    const double target = std::max(0.0, from + deltaBeats);
    state = {true, snap.ppq, snap.samplePosition, target, nowMs};
    return target;
}

/** Moves the transport cursor by `deltaBeats` (playing or stopped). Always returns true. */
inline bool nudgeTransportCursor(TransportService& transport, TransportNudgeState& state, double deltaBeats) {
    transport.locateBeat(
        computeNudgeTarget(state, transport.getPositionSnapshot(), deltaBeats, juce::Time::getMillisecondCounter()));
    return true;
}

/** As above, but `bars` (negative = back) whole bars in the snapshot's current time signature. */
inline bool nudgeTransportCursorBars(TransportService& transport, TransportNudgeState& state, double bars) {
    const auto snap = transport.getPositionSnapshot();
    transport.locateBeat(
        computeNudgeTarget(state, snap, bars * beatsPerBarOf(snap), juce::Time::getMillisecondCounter()));
    return true;
}

/** Locates to a loop locator. A missing or degenerate range (end <= start) is a no-op. Always
 *  returns true. The jump is recorded so a nudge fired before the audio thread applies it builds
 *  on the jump target. */
inline bool jumpToLoopLocator(TransportService& transport, TransportNudgeState& state, bool toEnd) {
    const auto snap = transport.getPositionSnapshot();
    if (!(snap.loopEndPpq > snap.loopStartPpq))
        return true;
    const double target = toEnd ? snap.loopEndPpq : snap.loopStartPpq;
    transport.locateBeat(target);
    state = {true, snap.ppq, snap.samplePosition, target, juce::Time::getMillisecondCounter()};
    return true;
}

/** Locates to an absolute beat (clamped at 0) and records it, so a nudge fired before the audio
 *  thread applies it builds on this target. Always returns true. */
inline bool locateTransportTracked(TransportService& transport, TransportNudgeState& state, double beat) {
    const auto snap = transport.getPositionSnapshot();
    const double target = std::max(0.0, beat);
    transport.locateBeat(target);
    state = {true, snap.ppq, snap.samplePosition, target, juce::Time::getMillisecondCounter()};
    return true;
}

} // namespace synth
