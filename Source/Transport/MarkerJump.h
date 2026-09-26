// Concern: FRO277's jump-to-next/previous-marker transport actions (transportJumpToNextMarker/
// transportJumpToPreviousMarker) -- the marker-search arithmetic, and its reuse of TransportNudge.h's
// accumulate-on-last-request state so several presses fired faster than the audio thread applies
// them still step marker-by-marker rather than collapsing onto one stale snapshot. The search
// itself takes a plain list of beats (no TimelineDoc/MainComponent dependency) so it is
// unit-testable headless.
#pragma once

#include "TransportNudge.h"
#include "TransportService.h"
#include <optional>
#include <vector>

namespace synth {

/** The nearest marker beat strictly beyond `referenceBeat` in the requested direction (`forward`
 *  true = next, false = previous), or nullopt if none exists. Sitting exactly on a marker still
 *  steps past it -- `beat > referenceBeat` / `beat < referenceBeat` are both strict -- so repeated
 *  presses always advance and a jump landing exactly on a marker is never a no-op next time.
 *  Never wraps: past the last (or before the first) marker returns nullopt. `markerBeats` need not
 *  be sorted. */
inline std::optional<double> findAdjacentMarkerBeat(const std::vector<double>& markerBeats, double referenceBeat,
                                                    bool forward) {
    std::optional<double> best;
    for (double beat : markerBeats) {
        if (forward ? (beat > referenceBeat) : (beat < referenceBeat)) {
            if (!best || (forward ? beat < *best : beat > *best))
                best = beat;
        }
    }
    return best;
}

/** Locates to the next/previous marker relative to the last-requested position: builds on a still-
 *  unconsumed pending request (see TransportNudge.h's isPendingRequestUnconsumed) rather than the
 *  once-per-block position snapshot, so pressing next/previous twice in quick succession steps two
 *  markers even before the audio thread reports the first jump. A missing marker in that direction
 *  is a no-op -- the transport is never relocated and `state` is left untouched, so it never wraps
 *  and a following press in the OTHER direction still starts from the real position. Always returns
 *  true (command handled), matching jumpToLoopLocator's contract. */
inline bool jumpToAdjacentMarker(TransportService& transport, TransportNudgeState& state,
                                 const std::vector<double>& markerBeats, bool forward) {
    const auto snap = transport.getPositionSnapshot();
    const auto nowMs = juce::Time::getMillisecondCounter();
    const double reference = isPendingRequestUnconsumed(state, snap, nowMs) ? state.target : snap.ppq;
    const auto next = findAdjacentMarkerBeat(markerBeats, reference, forward);
    if (!next)
        return true;
    transport.locateBeat(*next);
    state = {true, snap.ppq, snap.samplePosition, *next, nowMs};
    return true;
}

} // namespace synth
