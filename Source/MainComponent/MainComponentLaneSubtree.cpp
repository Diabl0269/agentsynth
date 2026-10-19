// Concern: everything that hangs off an automation lane following it through Change parameter and Duplicate -- the
// one carry helper, the per-routing move and copy it is made of (reusable by anything that drags one modulator onto
// another lane), and the two TrackHeaderHost calls that wrap a lane edit and its carry in ONE undo step.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include <algorithm>
#include <map>

namespace {
juce::String uuidOf(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
}

// `order` (a lane's rows as they showed before a carry, as modulatorOrder keys) with every key a carried routing had
// swapped for its new routing's key, positions kept, written as `lane`'s stored order (the order lives with the lane,
// but its keys are the routings' Attenuverter uuids, which a carry replaces). A key that was not carried (a refused
// move, a stale key) stays as it is. Nothing is written when nothing was carried.
void remapModulatorOrder(synth::TimelineDoc& doc, synth::LaneId lane, std::vector<juce::String> order,
                         const std::map<juce::String, juce::String>& newKeys) {
    if (doc.getLane(lane) == nullptr || newKeys.empty())
        return;
    for (auto& key : order)
        if (const auto it = newKeys.find(key); it != newKeys.end())
            key = it->second;
    doc.setModulatorOrder(lane, std::move(order));
}
} // namespace

// The order `lane`'s modulator rows show in now, as modulatorOrder keys: its stored order completed by graph order, so
// an empty (never reordered) order is written out whole before a carry replaces the keys it would fall back on.
std::vector<juce::String> MainComponent::shownModulatorOrder(synth::LaneId lane) {
    std::vector<juce::String> order;
    if (const auto* current = timelineDoc.getLane(lane))
        for (const auto& info : synth::ui::sortedByModulatorOrder(getModulators(current->nodeUuid, current->paramId),
                                                                  current->modulatorOrder))
            order.push_back(synth::ui::modulatorOrderKey(info));
    return order;
}

// Everything attached to the `from` lane follows it onto `to` (a Move takes it off `from`, a Copy leaves it there and
// adds its own). Today a lane has one kind of attachment, the modulator routings into its parameter's CV jack with
// their amount lanes; a new kind (anything else that hangs off a lane) adds its branch HERE, so Change parameter and
// Duplicate carry it without a second edit. Records no undo: the caller wraps the doc edit and this in one step.
// `newKeys`, when given, receives each carried routing's old modulatorOrderKey -> its new routing's key, so the caller
// can carry the lane's modulator order along with the routings.
void MainComponent::carryLaneAttachments(const LaneEnd& from, const LaneEnd& to, synth::TrackId toTrack, CarryMode mode,
                                         std::map<juce::String, juce::String>* newKeys) {
    if (from == to)
        return;
    for (const auto& modulator : getModulators(from.nodeUuid, from.paramId)) {
        juce::String newKey;
        const bool carried = mode == CarryMode::Move ? moveModulatorRouting(modulator, to, &newKey)
                                                     : copyModulatorRouting(modulator, to, toTrack, &newKey);
        if (carried && newKeys != nullptr)
            (*newKeys)[synth::ui::modulatorOrderKey(modulator)] = newKey;
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
// `newOrderKey`, when given, receives the new routing's modulatorOrderKey.
bool MainComponent::moveModulatorRouting(const synth::ui::ModulatorInfo& modulator, const LaneEnd& to,
                                         juce::String* newOrderKey) {
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
    const auto newAtten = uuidOf(graph.getNodeForId(created));
    if (const auto* amountLane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid))
        timelineDoc.rebindLane(amountLane->id, newAtten);
    graphEditor.removeModulator(*old, /*removeLonelySource=*/false, /*recordUndo=*/false);
    if (newOrderKey != nullptr)
        *newOrderKey = newAtten;
    return true;
}

// Like moveModulatorRouting but the old routing stays: the new one has the same source and depth, and a copy of the
// amount lane's points on `amountTrack`.
bool MainComponent::copyModulatorRouting(const synth::ui::ModulatorInfo& modulator, const LaneEnd& to,
                                         synth::TrackId amountTrack, juce::String* newOrderKey) {
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
    const auto newAtten = uuidOf(graph.getNodeForId(created));
    if (!points.empty())
        synth::ui::writeAmountLane(timelineDoc, amountTrack, newAtten, points);
    if (newOrderKey != nullptr)
        *newOrderKey = newAtten;
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
    auto order = shownModulatorOrder(lane); // read before the edit: the rows keep their places on the new parameter
    // The rows see the retarget and the carry as one change: each re-binds in place, keeping its fold and height.
    const synth::ui::TimelineAutomationLanes::ModulatorHold hold(timelinePanel.getAutomationLanes());
    bool retargeted = false;
    undoManager.recordGraphTimelineAndMacroChange(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [&] {
        retargeted = timelineDoc.retargetLane(lane, target.nodeUuid, target.paramId, target.range, target.paramIndex);
        if (!retargeted)
            return;
        std::map<juce::String, juce::String> newKeys;
        carryLaneAttachments(from, to, track, CarryMode::Move, &newKeys);
        remapModulatorOrder(timelineDoc, lane, std::move(order), newKeys);
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
    auto order = shownModulatorOrder(lane); // the copy shows its rows in the original's order
    synth::LaneId created;
    undoManager.recordGraphTimelineAndMacroChange(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [&] {
        created = timelineDoc.duplicateLane(lane, target.nodeUuid, target.paramId, target.range, target.paramIndex);
        const auto* owner = created.isValid() ? timelineDoc.getTrackForLane(created) : nullptr;
        if (owner == nullptr)
            return;
        std::map<juce::String, juce::String> newKeys;
        carryLaneAttachments(from, to, owner->id, CarryMode::Copy, &newKeys);
        remapModulatorOrder(timelineDoc, created, std::move(order), newKeys);
    });
    return created;
}

// Dragging a modulator row onto another lane: true when that lane's parameter has a CV jack that does not already carry
// this source. The drop indicator asks this on every drag event, so it only reads.
bool MainComponent::canMoveModulatorToLane(const synth::ui::ModulatorInfo& modulator, synth::LaneId destLane) {
    const auto* dest = timelineDoc.getLane(destLane);
    auto* target = dest != nullptr ? findNodeByUuid(dest->nodeUuid) : nullptr;
    if (target == nullptr || (dest->nodeUuid == modulator.targetUuid && dest->paramId == modulator.paramId))
        return false;
    if (graphEditor.modulationChannelFor(target->nodeID, dest->paramId) < 0 || !findModulatorRouting(modulator))
        return false;
    for (const auto& existing : getModulators(dest->nodeUuid, dest->paramId))
        if (existing.sourceUuid == modulator.sourceUuid && existing.sourceChannel == modulator.sourceChannel)
            return false;
    return true;
}

// The row's drop on another lane: the routing moves to that lane's parameter (moveModulatorRouting: the amount lane
// keeps its id and points), the destination lane's order gets the moved routing's new key at `destIndex`, and the old
// key leaves the order of every lane on the old jack -- ONE graph + timeline undo step.
bool MainComponent::moveModulatorToLane(const synth::ui::ModulatorInfo& modulator, synth::LaneId destLane,
                                        int destIndex) {
    if (!canMoveModulatorToLane(modulator, destLane))
        return false;
    const auto* dest = timelineDoc.getLane(destLane);
    const LaneEnd to{dest->nodeUuid, dest->paramId};
    std::vector<juce::String> order; // the destination's rows as they show now, before the newcomer
    for (const auto& info :
         synth::ui::sortedByModulatorOrder(getModulators(to.nodeUuid, to.paramId), dest->modulatorOrder))
        order.push_back(synth::ui::modulatorOrderKey(info));
    std::vector<synth::LaneId> oldLanes;
    for (const auto& track : timelineDoc.getTracks())
        for (const auto& lane : track.lanes)
            if (lane.nodeUuid == modulator.targetUuid && lane.paramId == modulator.paramId)
                oldLanes.push_back(lane.id);
    const auto oldKey = synth::ui::modulatorOrderKey(modulator);
    bool moved = false;
    undoManager.recordGraphTimelineAndMacroChange(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [&] {
        juce::String newKey;
        if (!moveModulatorRouting(modulator, to, &newKey))
            return;
        moved = true;
        order.insert(order.begin() + juce::jlimit(0, (int)order.size(), destIndex), newKey);
        timelineDoc.setModulatorOrder(destLane, order);
        for (const auto lane : oldLanes) {
            const auto* current = timelineDoc.getLane(lane);
            if (current == nullptr || current->modulatorOrder.empty())
                continue;
            auto kept = current->modulatorOrder;
            kept.erase(std::remove(kept.begin(), kept.end(), oldKey), kept.end());
            timelineDoc.setModulatorOrder(lane, std::move(kept));
        }
    });
    return moved;
}
