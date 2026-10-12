#include "Timeline/TimelineOpsLaneRepair.h"

#include "Timeline/AutomationBinding.h"

#include <algorithm>
#include <cmath>

namespace synth {

LaneRepair repairWriteLane(juce::DynamicObject& op, const TimelineDoc& doc, const juce::AudioProcessorGraph& graph) {
    LaneRepair repair;
    const juce::var nodeUuidVar = op.getProperty("nodeUuid");
    const juce::var paramIdVar = op.getProperty("paramId");
    if (!nodeUuidVar.isString() || !paramIdVar.isString() || nodeUuidVar.toString().isEmpty() ||
        paramIdVar.toString().isEmpty())
        return repair;
    const juce::String nodeUuid = nodeUuidVar.toString();
    const juce::String paramId = paramIdVar.toString();

    juce::AudioProcessor* processor = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == nodeUuid) {
            processor = node->getProcessor();
            break;
        }
    if (processor == nullptr) {
        repair.outcome = LaneRepair::Outcome::UnknownNode;
        return repair;
    }
    repair.moduleName = processor->getName();

    const AutomationLane* existing = doc.getLaneForParam(nodeUuid, paramId);
    const auto resolved = resolveLaneParameter(processor, paramId, existing != nullptr ? existing->paramIndexHint : -1);
    if (!resolved.resolved()) {
        repair.outcome = LaneRepair::Outcome::UnknownParam;
        return repair;
    }
    repair.parameterName = resolved.liveParameter()->getName(32);

    const auto liveBounds = laneValueBoundsFor(resolved);
    repair.minAllowed = liveBounds.minValue;
    repair.maxAllowed = liveBounds.maxValue;
    if (existing != nullptr) {
        repair.minAllowed = std::max(repair.minAllowed, static_cast<double>(existing->range.minValue));
        repair.maxAllowed = std::min(repair.maxAllowed, static_cast<double>(existing->range.maxValue));
    }
    if (!(repair.minAllowed <= repair.maxAllowed))
        return repair; // disjoint ranges: nothing sensible to clamp to, TimelineOps rejects the values

    if (auto* points = op.getProperty("points").getArray())
        for (auto& pointVar : *points)
            if (auto* point = pointVar.getDynamicObject()) {
                const juce::var valueVar = point->getProperty("value");
                if (!(valueVar.isInt() || valueVar.isInt64() || valueVar.isDouble()))
                    continue;
                const double value = static_cast<double>(valueVar);
                if (!std::isfinite(value))
                    continue;
                const double limited = juce::jlimit(repair.minAllowed, repair.maxAllowed, value);
                if (limited != value) {
                    point->setProperty("value", limited);
                    ++repair.clamped;
                }
            }
    return repair;
}

} // namespace synth
