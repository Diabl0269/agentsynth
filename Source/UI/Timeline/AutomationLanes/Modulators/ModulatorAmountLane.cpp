// Concern: the amount lane's document side -- finding it, the one write that creates, rewrites or removes it,
// and how an amount reads as text.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"

#include <cmath>

namespace synth::ui {

synth::AutomationLane::RangeSnapshot amountLaneRange() noexcept { return {-1.0f, 1.0f, 0.0f}; }

const synth::AutomationLane* amountLaneFor(const synth::TimelineDoc& doc, const juce::String& attenuverterUuid) {
    return attenuverterUuid.isEmpty() ? nullptr : doc.getLaneForParam(attenuverterUuid, kAmountParamId);
}

// Up to three doc mutations (add the lane, read mode, the points) that the caller's single undo record
// covers. A lane is never left empty: no points means no lane.
bool writeAmountLane(synth::TimelineDoc& doc, synth::TrackId track, const juce::String& attenuverterUuid,
                     const std::vector<synth::AutomationLane::Breakpoint>& points) {
    const auto* existing = amountLaneFor(doc, attenuverterUuid);
    if (points.empty())
        return existing != nullptr && doc.removeLane(existing->id);

    const auto id =
        existing != nullptr ? existing->id : doc.addLane(track, attenuverterUuid, kAmountParamId, amountLaneRange());
    const auto* lane = id.isValid() ? doc.getLane(id) : nullptr;
    if (lane == nullptr)
        return false;
    // The applier only plays a Read lane (a stray Touch/Write would hand the amount back to the knob).
    bool changed = doc.setLaneRecordMode(id, static_cast<int>(synth::LaneRecordMode::Read));
    std::vector<double> removeBeats;
    for (const auto& point : doc.getLane(id)->points)
        removeBeats.push_back(point.beat);
    changed = doc.editBreakpoints(id, removeBeats, points) || changed;
    return changed || existing == nullptr;
}

juce::String amountText(double amount) {
    const int percent = juce::roundToInt(amount * 100.0);
    if (percent == 0)
        return "0%";
    const auto sign = percent > 0 ? juce::String("+") : juce::String::fromUTF8("\xE2\x88\x92");
    return sign + juce::String(std::abs(percent)) + "%";
}

} // namespace synth::ui
