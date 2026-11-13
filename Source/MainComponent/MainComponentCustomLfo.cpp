// MainComponentCustomLfo.cpp -- the TrackHeaderHost side of a lane's "Create custom LFO": the parameter normaliser the
// plan is computed with, and the command that turns a lane range into an LFO, its routing, its amount lane and the
// flattened lane as ONE undo step.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Modules/LFOModule.h"
#include "Modules/LfoRateDivisions.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/CustomLfoFromRange.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"

namespace {
// A parameter written without an undo capture of its own: the surrounding step owns the undo.
void setParameterDirectly(juce::AudioProcessor* processor, const juce::String& paramId, float value) {
    if (auto* param = findParameterByID(processor, paramId))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

void configureLfo(LFOModule& lfo, const synth::ui::CustomLfoPlan& plan) {
    setParameterDirectly(&lfo, "shape", (float)LFOModule::kCustomShapeIndex);
    setParameterDirectly(&lfo, "mode", 1.0f); // Sync
    setParameterDirectly(&lfo, "rateSync", (float)plan.divisionIndex);
    setParameterDirectly(&lfo, "retrig", 0.0f);
    setParameterDirectly(&lfo, "bipolar", 0.0f);
    setParameterDirectly(&lfo, "level", (float)plan.level);
    setParameterDirectly(&lfo, "phase", (float)plan.phaseDegrees);
    lfo.setCustomWave(plan.wave);
}
} // namespace

std::optional<double> MainComponent::normaliseParameterValue(const juce::String& nodeUuid, const juce::String& paramId,
                                                             double value) {
    auto* node = findNodeByUuid(nodeUuid);
    auto* param = node != nullptr ? findParameterByID(node->getProcessor(), paramId) : nullptr;
    if (param == nullptr)
        return std::nullopt;
    return (double)param->convertTo0to1((float)value);
}

// The LFO and its routing, the LFO's settings and wave, the routing's amount lane and the lane's flattened range are
// one recordGraphTimelineAndMacroChange: one Cmd+Z removes the LFO, its row and its amount lane and brings the drawn
// points back, and a redo re-applies all of it (the wave rides the node's saved state). The graph part runs first, so
// the attenuverter exists, with its uuid, before its amount lane is written.
bool MainComponent::createCustomLfoFromRange(synth::LaneId laneId, double startBeat, double endBeat) {
    const auto* lane = timelineDoc.getLane(laneId);
    const auto* track = timelineDoc.getTrackForLane(laneId);
    auto* target = lane != nullptr ? findNodeByUuid(lane->nodeUuid) : nullptr;
    auto* param = target != nullptr ? findParameterByID(target->getProcessor(), lane->paramId) : nullptr;
    if (track == nullptr || param == nullptr || graphEditor.modulationChannelFor(target->nodeID, lane->paramId) < 0)
        return false;

    const auto plan = synth::ui::planCustomLfoFromRange(
        *lane, startBeat, endBeat, [param](double v) { return (double)param->convertTo0to1((float)v); });
    if (!plan.ok())
        return false;

    const auto trackId = track->id;
    const auto targetId = target->nodeID;
    const auto paramId = lane->paramId;
    bool built = false;
    undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
        [this, &plan, &built, trackId, targetId, paramId, laneId] {
            auto& graph = audioEngine.getGraph();
            juce::AudioProcessorGraph::NodeID attenuverterId;
            const auto lfoId =
                graphEditor.addLfoModulator(targetId, paramId, /*recordUndo=*/false, /*depth=*/1.0f, &attenuverterId);
            auto* lfoNode = graph.getNodeForId(lfoId);
            auto* lfo = lfoNode != nullptr ? dynamic_cast<LFOModule*>(lfoNode->getProcessor()) : nullptr;
            auto* attenuverter = graph.getNodeForId(attenuverterId);
            if (lfo == nullptr || attenuverter == nullptr)
                return;
            configureLfo(*lfo, plan);
            const auto attenuverterUuid = synth::AIStateMapper::ensureNodeUuid(attenuverter);
            synth::ui::writeAmountLane(timelineDoc, trackId, attenuverterUuid, plan.amountPoints);
            timelineDoc.editBreakpoints(laneId, plan.removeBeats, plan.addPoints);
            built = true;
        });
    return built;
}
