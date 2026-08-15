#include "AutomationApplier.h"
#include <cmath>

namespace synth {

void AutomationApplier::applyBlock(const AutomationBindingTable& table, const BlockTimeInfo& info,
                                   const AutomationRecordState* recordState, AutomationUiFeed* uiFeed) noexcept {
    // Knobs stay free while the transport is stopped — see the header for why.
    if (!info.playing)
        return;

    const TimelineSnapshot* snapshot = table.snapshot;
    if (snapshot == nullptr || table.bindings.empty())
        return;

    // Bounds are re-derived here rather than trusted: a table is built on the message thread from a
    // snapshot that is immutable afterwards, so these can never fire — but the cost is two loads
    // outside the loop and it keeps an out-of-range index from becoming an audio-thread read of
    // arbitrary memory.
    const auto numLanes = snapshot->lanes.size();
    const auto numPoints = snapshot->points.size();

    // One relaxed load for the whole block rather than one per binding.
    const bool recordArmed = recordState != nullptr && recordState->globalRecordEnable.load(std::memory_order_relaxed);

    for (const auto& binding : table.bindings) {
        if (binding.param == nullptr || binding.laneIndex < 0)
            continue;

        const auto laneIndex = static_cast<std::size_t>(binding.laneIndex);
        if (laneIndex >= numLanes)
            continue;

        const TimelineSnapshot::LaneInfo& lane = snapshot->lanes[laneIndex];
        if (lane.firstPoint < 0 || lane.numPoints < 0 ||
            static_cast<std::size_t>(lane.firstPoint) + static_cast<std::size_t>(lane.numPoints) > numPoints)
            continue;

        // TL4-4 record modes — the full table is in the header. Everything here is a compare or a
        // scan of eight relaxed atomic loads; no branch reaches memory the message thread can move.
        if (lane.recordMode == static_cast<int>(LaneRecordMode::Off))
            continue;

        if (lane.recordMode == static_cast<int>(LaneRecordMode::Write)) {
            if (recordArmed)
                continue; // the take is overwriting this span — do not play stale data back into it
        } else if (lane.recordMode == static_cast<int>(LaneRecordMode::Touch) ||
                   lane.recordMode == static_cast<int>(LaneRecordMode::Latch)) {
            // Claims only ever exist while global record is armed (the recorder drops them all when
            // it disarms), so this needs no extra gate on recordArmed.
            if (recordState != nullptr && recordState->claims.isClaimed(binding.param))
                continue; // a hand is on this knob: it wins for as long as it is there
        }

        const TimelineSnapshot::Point* points = snapshot->points.data() + lane.firstPoint;

        // Denormalised, in the lane's own units. An empty lane falls back to its range default,
        // which is what the kernel's fallbackValue argument is for.
        const double value = AutomationKernel::evaluate(points, lane.numPoints, info.startPpq,
                                                        static_cast<double>(lane.defaultValue), binding.cursor);

        const auto denormalised = static_cast<float>(value);
        if (!std::isfinite(denormalised))
            continue; // never hand a NaN to NormalisableRange — it jasserts, and it would land in live audio

        // convertTo0to1 clamps into [0, 1] itself, so a lane authored against a wider range pins at
        // the parameter's endpoint. setValue and NOT setValueNotifyingHost: a plain store, no
        // listener notification from the audio thread (see the header).
        const float newNormalized = binding.param->convertTo0to1(denormalised);
        binding.param->setValue(newNormalized);

        // TL4-5: reflect into the UI feed, deduped per binding. NaN != NaN is always true, so a
        // binding's very first write always pushes; every write after that only pushes when the
        // value actually moved.
        if (uiFeed != nullptr && newNormalized != binding.lastPushedNormalized) {
            binding.lastPushedNormalized = newNormalized;
            uiFeed->push({binding.nodeID.uid, binding.param, newNormalized});
        }
    }
}

} // namespace synth
