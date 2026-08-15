#include "AutomationApplier.h"
#include <cmath>

namespace synth {

void AutomationApplier::applyBlock(const AutomationBindingTable& table, const BlockTimeInfo& info) noexcept {
    // Knobs stay free while the transport is stopped — see the header for why, and for what TL4-4
    // has to change here when record modes (latch/touch) arrive.
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
        binding.param->setValue(binding.param->convertTo0to1(denormalised));
    }
}

} // namespace synth
