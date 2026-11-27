#include "AutomationApplier.h"
#include <algorithm>
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
        // Exactly one of these two is populated on a real binding — see the header.
        juce::AudioProcessorParameter* liveParam =
            binding.param != nullptr ? static_cast<juce::AudioProcessorParameter*>(binding.param) : binding.hostedParam;
        if (liveParam == nullptr || binding.laneIndex < 0)
            continue;

        const auto laneIndex = static_cast<std::size_t>(binding.laneIndex);
        if (laneIndex >= numLanes)
            continue;

        const TimelineSnapshot::LaneInfo& lane = snapshot->lanes[laneIndex];
        if (lane.firstPoint < 0 || lane.numPoints < 0 ||
            static_cast<std::size_t>(lane.firstPoint) + static_cast<std::size_t>(lane.numPoints) > numPoints)
            continue;

        // Record modes — the full table is in the header. Everything here is a compare or a
        // scan of eight relaxed atomic loads; no branch reaches memory the message thread can move.
        if (lane.recordMode == static_cast<int>(LaneRecordMode::Off))
            continue;

        if (lane.recordMode == static_cast<int>(LaneRecordMode::Write)) {
            if (recordArmed)
                continue; // the take is overwriting this span — do not play stale data back into it
        }
        // Whatever the mode (Read included, record armed or not), a hand on this knob wins for as long
        // as it is there and a moment after (AutomationRecorder::kHandLingerMs). A Read lane is only
        // suspended, never written: Read does not capture. When the hand lets go the lane eases back
        // from where the hand left the value over kReleaseGlideSec, so neither the take-over nor the
        // hand-back jumps.
        if (recordState != nullptr && recordState->claims.isClaimed(liveParam)) {
            binding.wasHeld = true;
            binding.heldNormalized = liveParam->getValue();
            continue;
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
        // listener notification from the audio thread (see the header). A `hostedParam` binding has
        // no NormalisableRange, so the lane's own range IS the denorm -> 0..1 map (see the header) —
        // the same defensive [0, 1] clamp convertTo0to1 does internally.
        float newNormalized;
        if (binding.param != nullptr) {
            newNormalized = binding.param->convertTo0to1(denormalised);
        } else {
            const double span = static_cast<double>(lane.maxValue) - static_cast<double>(lane.minValue);
            newNormalized =
                span > 0.0
                    ? juce::jlimit(0.0f, 1.0f,
                                   static_cast<float>(
                                       (static_cast<double>(denormalised) - static_cast<double>(lane.minValue)) / span))
                    : 0.0f;
        }

        // The hand just let go: ease from where it left the value to the lane's, so it never jumps.
        if (binding.wasHeld) {
            binding.wasHeld = false;
            binding.glideLeftSec = kReleaseGlideSec;
        }
        if (binding.glideLeftSec > 0.0) {
            const double blockSec = info.sampleRate > 0.0 ? info.numSamples / info.sampleRate : 0.0;
            binding.glideLeftSec = std::max(0.0, binding.glideLeftSec - blockSec);
            const double t = 1.0 - binding.glideLeftSec / kReleaseGlideSec;
            const double eased = t * t * (3.0 - 2.0 * t);
            newNormalized =
                static_cast<float>(binding.heldNormalized + (newNormalized - binding.heldNormalized) * eased);
        }
        liveParam->setValue(newNormalized);

        // Reflect into the UI feed, deduped per binding. NaN != NaN is always true, so a
        // binding's very first write always pushes; every write after that only pushes when the
        // value actually moved.
        if (uiFeed != nullptr && newNormalized != binding.lastPushedNormalized) {
            binding.lastPushedNormalized = newNormalized;
            uiFeed->push({binding.nodeID.uid, liveParam, newNormalized});
        }
    }
}

} // namespace synth
