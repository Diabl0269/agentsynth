// Concern: everything that hangs off an automation lane following it through Change parameter and Duplicate -- the
// one carry helper, the per-routing move and copy it is made of (reusable by anything that drags one modulator onto
// another lane), and the two TrackHeaderHost calls that wrap a lane edit and its carry in ONE undo step.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"

namespace {
juce::String uuidOf(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
}
} // namespace

// Everything attached to the `from` lane follows it onto `to` (a Move takes it off `from`, a Copy leaves it there and
// adds its own). Today a lane has one kind of attachment, the modulator routings into its parameter's CV jack with
// their amount lanes; a new kind (anything else that hangs off a lane) adds its branch HERE, so Change parameter and
// Duplicate carry it without a second edit. Records no undo: the caller wraps the doc edit and this in one step.
void MainComponent::carryLaneAttachments(const LaneEnd& from, const LaneEnd& to, synth::TrackId toTrack,
                                         CarryMode mode) {
    if (from == to)
        return;
    for (const auto& modulator : getModulators(from.nodeUuid, from.paramId)) {
        if (mode == CarryMode::Move)
            moveModulatorRouting(modulator, to);
        else
            copyModulatorRouting(modulator, to, toTrack);
    }
}

namespace {
// A routing's depth: its Attenuverter's amount, or 1.0 for a direct cable (which gains an Attenuverter when it is
// cabled again, like any new routing).
float depthOf(juce::AudioProcessorGraph& graph, const AudioEngine::ModulationRouting& routing) {
    if (routing.kind == AudioEngine::RoutingKind::AttenuverterChain)
        if (auto* atten = graph.getNodeForId(routing.attenuverterNodeID))
            if (auto* amount = findParameterByID(atten->getProcessor(), synth::ui::kAmountParamId))
                return amount->convertFrom0to1(amount->getValue());
    return 1.0f;
}
} // namespace

// The same recipe as changeModulatorSource, on the destination side: the source is cabled into `to`'s CV jack at the
// old depth, the amount lane (keyed by the routing's Attenuverter) is re-keyed to the new Attenuverter with its id,
// track and points untouched, and the old routing is cut. False, nothing changed, when `to` has no CV jack or the
// jack already has this source.
bool MainComponent::moveModulatorRouting(const synth::ui::ModulatorInfo& modulator, const LaneEnd& to) {
    auto* target = findNodeByUuid(to.nodeUuid);
    auto* source = findNodeByUuid(modulator.sourceUuid);
    const auto old = findModulatorRouting(modulator);
    if (target == nullptr || source == nullptr || !old.has_value())
        return false;
    const int raw = graphEditor.modulationChannelFor(target->nodeID, to.paramId);
    if (raw < 0)
        return false;
    auto& graph = audioEngine.getGraph();
    const auto created = graphEditor.connectModulationSource(source->nodeID, modulator.sourceChannel, target->nodeID,
                                                             raw, depthOf(graph, *old), /*recordUndo=*/false);
    if (created.uid == 0)
        return false;
    if (const auto* amountLane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid))
        timelineDoc.rebindLane(amountLane->id, uuidOf(graph.getNodeForId(created)));
    graphEditor.removeModulator(*old, /*removeLonelySource=*/false, /*recordUndo=*/false);
    return true;
}

// Like moveModulatorRouting but the old routing stays: the new one has the same source and depth, and a copy of the
// amount lane's points on `amountTrack`.
bool MainComponent::copyModulatorRouting(const synth::ui::ModulatorInfo& modulator, const LaneEnd& to,
                                         synth::TrackId amountTrack) {
    auto* target = findNodeByUuid(to.nodeUuid);
    auto* source = findNodeByUuid(modulator.sourceUuid);
    const auto old = findModulatorRouting(modulator);
    if (target == nullptr || source == nullptr || !old.has_value())
        return false;
    const int raw = graphEditor.modulationChannelFor(target->nodeID, to.paramId);
    if (raw < 0)
        return false;
    auto& graph = audioEngine.getGraph();
    std::vector<synth::AutomationLane::Breakpoint> points; // copied first: the doc edits below move its storage
    if (const auto* amountLane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid))
        points = amountLane->points;
    const auto created = graphEditor.connectModulationSource(source->nodeID, modulator.sourceChannel, target->nodeID,
                                                             raw, depthOf(graph, *old), /*recordUndo=*/false);
    if (created.uid == 0)
        return false;
    if (!points.empty())
        synth::ui::writeAmountLane(timelineDoc, amountTrack, uuidOf(graph.getNodeForId(created)), points);
    return true;
}

// "Change parameter...": the lane's doc edit and the move of its modulators are ONE graph + timeline undo step. The
// old parameter is read first, since the edit rewrites it.
bool MainComponent::retargetLaneWithAttachments(synth::LaneId lane, const synth::ui::LaneTarget& target) {
    const auto* owner = timelineDoc.getTrackForLane(lane);
    const auto* current = timelineDoc.getLane(lane);
    if (owner == nullptr || current == nullptr)
        return false;
    const LaneEnd from{current->nodeUuid, current->paramId};
    const LaneEnd to{target.nodeUuid, target.paramId};
    const auto track = owner->id;
    bool retargeted = false;
    undoManager.recordGraphTimelineAndMacroChange(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [&] {
        retargeted = timelineDoc.retargetLane(lane, target.nodeUuid, target.paramId, target.range, target.paramIndex);
        if (retargeted)
            carryLaneAttachments(from, to, track, CarryMode::Move);
    });
    return retargeted;
}

// "Duplicate": the copy of the lane and the copies of its modulators are ONE graph + timeline undo step; the original
// keeps its own.
synth::LaneId MainComponent::duplicateLaneWithAttachments(synth::LaneId lane, const synth::ui::LaneTarget& target) {
    const auto* current = timelineDoc.getLane(lane);
    if (current == nullptr)
        return {};
    const LaneEnd from{current->nodeUuid, current->paramId};
    const LaneEnd to{target.nodeUuid, target.paramId};
    synth::LaneId created;
    undoManager.recordGraphTimelineAndMacroChange(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [&] {
        created = timelineDoc.duplicateLane(lane, target.nodeUuid, target.paramId, target.range, target.paramIndex);
        if (const auto* owner = created.isValid() ? timelineDoc.getTrackForLane(created) : nullptr)
            carryLaneAttachments(from, to, owner->id, CarryMode::Copy);
    });
    return created;
}
