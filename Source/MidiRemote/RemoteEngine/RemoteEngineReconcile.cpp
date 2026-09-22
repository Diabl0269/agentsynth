// Message thread. Builds a fresh RemoteMappingSnapshot from profiles_/assignments_/sourceKeys_ and
// publishes it. reconcile(graph) is MainComponent's post-graph-change funnel; the setters
// (setSources/setProfiles/setAssignments/setDefaultTakeover) also land here with graph == nullptr,
// which must NOT re-resolve parameters -- it keeps whatever the last real reconcile resolved
// (docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step: "a setter is
// not a graph change").

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Timeline/AutomationBinding.h"

#include <algorithm>
#include <map>
#include <utility>

namespace synth::midi {

namespace {

using ProcessorByUuid = std::map<juce::String, juce::AudioProcessor*>;
using NodeIdByUuid = std::map<juce::String, juce::AudioProcessorGraph::NodeID>;
// assignmentId -> the last real reconcile's resolution, for a parameter OR a nodeCommand target
// (a setter's graph==nullptr rebuild keeps whichever of the two applies -- see resolveParameterTarget
// / resolveNodeCommandTarget below).
struct PreviousResolutionEntry {
    juce::AudioProcessorParameter* param = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId;
    bool orphaned = false;
};
using PreviousResolution = std::map<juce::String, PreviousResolutionEntry>;

// Same uuid -> processor lookup TimelineReconciler::reconcile uses (Source/Timeline/
// TimelineReconciler.cpp): read once per rebuild, never cached across calls.
ProcessorByUuid buildProcessorByUuid(juce::AudioProcessorGraph* graph) {
    ProcessorByUuid result;
    if (graph == nullptr)
        return result;
    for (auto* node : graph->getNodes()) {
        if (node == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            result.emplace(uuid, node->getProcessor());
    }
    return result;
}

// FRO253: uuid -> NodeID, alongside buildProcessorByUuid above -- a nodeCommand target resolves to
// the node itself (there is no processor-level lookup for it, unlike resolveLaneParameter).
NodeIdByUuid buildNodeIdByUuid(juce::AudioProcessorGraph* graph) {
    NodeIdByUuid result;
    if (graph == nullptr)
        return result;
    for (auto* node : graph->getNodes()) {
        if (node == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            result.emplace(uuid, node->nodeID);
    }
    return result;
}

PreviousResolution buildPreviousResolution(const RemoteMappingSnapshot* previous) {
    PreviousResolution result;
    if (previous == nullptr)
        return result;
    for (const auto& slot : previous->slots)
        result.emplace(slot.assignmentId, PreviousResolutionEntry{slot.param, slot.nodeId, slot.orphaned});
    return result;
}

const Control* findControl(const std::vector<ControllerProfile>& profiles, const juce::String& profileId,
                           const juce::String& controlId) {
    for (const auto& profile : profiles) {
        if (profile.id != profileId)
            continue;
        for (const auto& control : profile.controls)
            if (control.id == controlId)
                return &control;
        return nullptr;
    }
    return nullptr;
}

// The index of `profileId`'s input device among the sources this rebuild just resolved, or -1 if
// the profile doesn't exist or its device isn't currently open -- the slot is still kept, it just
// gets no lookup entry (docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project's "orphan
// controller" case).
int findSourceIndexForProfile(const std::vector<ControllerProfile>& profiles,
                              const std::vector<RemoteMappingSnapshot::SourceEntry>& sources,
                              const juce::String& profileId) {
    for (const auto& profile : profiles) {
        if (profile.id != profileId)
            continue;
        for (std::size_t i = 0; i < sources.size(); ++i)
            if (sources[i].key == profile.input.identifier)
                return static_cast<int>(i);
        return -1;
    }
    return -1;
}

void resolveParameterTarget(const Assignment& assignment, juce::AudioProcessorGraph* graph,
                            const ProcessorByUuid& processorByUuid, const PreviousResolution& previousResolution,
                            RemoteMappingSnapshot::Slot& slot) {
    if (graph != nullptr) {
        juce::AudioProcessor* processor = nullptr;
        const auto found = processorByUuid.find(assignment.target.parameter.nodeUuid);
        if (found != processorByUuid.end())
            processor = found->second;
        const auto resolution = resolveLaneParameter(processor, assignment.target.parameter.paramId,
                                                     assignment.target.parameter.paramIndexHint);
        slot.param = resolution.liveParameter();
        slot.orphaned = resolution.orphaned;
        return;
    }

    // A setter, not a graph change: keep whatever the last real reconcile resolved for this
    // assignment id; a brand new assignment simply stays unresolved until the next real reconcile.
    const auto found = previousResolution.find(assignment.id);
    if (found != previousResolution.end()) {
        slot.param = found->second.param;
        slot.orphaned = found->second.orphaned;
    }
}

// FRO253 (docs/control/midi-remote.md#node-command-targets): mirrors resolveParameterTarget above,
// but resolves to a NodeID rather than a juce::AudioProcessorParameter* -- a node command has no
// parameter to point at (ChannelStripModule::soloed_ is engine state, not a
// juce::RangedAudioParameter). Missing from `nodeIdByUuid` means orphaned, same as an unresolved
// parameter.
void resolveNodeCommandTarget(const Assignment& assignment, juce::AudioProcessorGraph* graph,
                              const NodeIdByUuid& nodeIdByUuid, const PreviousResolution& previousResolution,
                              RemoteMappingSnapshot::Slot& slot) {
    if (graph != nullptr) {
        const auto found = nodeIdByUuid.find(assignment.target.nodeCommand.nodeUuid);
        if (found != nodeIdByUuid.end()) {
            slot.nodeId = found->second;
            slot.orphaned = false;
        } else {
            slot.nodeId = {};
            slot.orphaned = true;
        }
        return;
    }

    // A setter, not a graph change: same "keep the last real reconcile's resolution" rule as
    // resolveParameterTarget.
    const auto found = previousResolution.find(assignment.id);
    if (found != previousResolution.end()) {
        slot.nodeId = found->second.nodeId;
        slot.orphaned = found->second.orphaned;
    }
}

void addLookupEntry(const std::vector<ControllerProfile>& profiles,
                    const std::vector<RemoteMappingSnapshot::SourceEntry>& sources, const Assignment& assignment,
                    int slotIndex, std::vector<std::pair<std::uint32_t, std::int32_t>>& pending) {
    const int sourceIndex = findSourceIndexForProfile(profiles, sources, assignment.control.profileId);
    if (sourceIndex < 0)
        return;
    const auto key = packLookupKey(sourceIndex, assignment.spec.type, assignment.spec.channel, assignment.spec.number);
    pending.emplace_back(key, slotIndex);
}

// One parameter-, action- or nodeCommand-target assignment -> one Slot, appended to `fresh`, plus
// its pending lookup-table entry (if its profile's device is currently open).
void addSlot(const Assignment& assignment, juce::AudioProcessorGraph* graph, const ProcessorByUuid& processorByUuid,
             const NodeIdByUuid& nodeIdByUuid, const PreviousResolution& previousResolution,
             const std::vector<ControllerProfile>& profiles, const ActionCommandLookup& actionLookup,
             RemoteMappingSnapshot& fresh, std::vector<std::pair<std::uint32_t, std::int32_t>>& lookupPending) {
    if (!assignment.enabled)
        return;

    RemoteMappingSnapshot::Slot slot;
    slot.assignmentId = assignment.id;
    slot.encoding = assignment.specEncoding;
    slot.buttonMode = assignment.specButtonMode;
    slot.takeover = assignment.takeover;
    slot.rangeMin = assignment.range.min;
    slot.rangeMax = assignment.range.max;
    slot.target = assignment.target;

    if (assignment.target.isParameter())
        resolveParameterTarget(assignment, graph, processorByUuid, previousResolution, slot);
    else if (assignment.target.isAction() && actionLookup)
        slot.commandId = actionLookup(assignment.target.action.actionId);
    else if (assignment.target.isNodeCommand())
        resolveNodeCommandTarget(assignment, graph, nodeIdByUuid, previousResolution, slot);

    const Control* control = findControl(profiles, assignment.control.profileId, assignment.control.controlId);
    const bool controlIsButtonLike =
        control != nullptr && (control->kind == ControlKind::button || control->kind == ControlKind::pad);
    slot.buttonLike = assignment.target.isAction() || assignment.target.isNodeCommand() || controlIsButtonLike ||
                      dynamic_cast<juce::AudioParameterBool*>(slot.param) != nullptr;

    const int slotIndex = static_cast<int>(fresh.slots.size());
    fresh.slots.push_back(slot);
    addLookupEntry(profiles, fresh.sources, assignment, slotIndex, lookupPending);
}

} // namespace

void RemoteEngine::reconcile(juce::AudioProcessorGraph& graph) { rebuildAndPublish(&graph); }

void RemoteEngine::rebuildAndPublish(juce::AudioProcessorGraph* graph) {
    const ProcessorByUuid processorByUuid = buildProcessorByUuid(graph);
    const NodeIdByUuid nodeIdByUuid = buildNodeIdByUuid(graph);
    const PreviousResolution previousResolution = buildPreviousResolution(publisher_.live());

    auto fresh = std::make_unique<RemoteMappingSnapshot>();

    for (const auto& key : sourceKeys_) {
        const int laneIndex = laneIndexFor(key);
        if (laneIndex < 0)
            continue; // beyond kMaxRemoteSources -- not routed (RemoteEvent.h)

        RemoteMappingSnapshot::SourceEntry entry;
        entry.key = key;
        entry.laneIndex = laneIndex;
        for (const auto& profile : profiles_) {
            if (profile.input.identifier == key) {
                entry.passMapped = profile.passMapped;
                break;
            }
        }
        fresh->sources.push_back(std::move(entry));
    }

    std::vector<std::pair<std::uint32_t, std::int32_t>> lookupPending;

    for (const auto& assignment : assignments_)
        addSlot(assignment, graph, processorByUuid, nodeIdByUuid, previousResolution, profiles_, actionLookup_, *fresh,
                lookupPending);
    for (const auto& profile : profiles_)
        for (const auto& assignment : profile.actions)
            addSlot(assignment, graph, processorByUuid, nodeIdByUuid, previousResolution, profiles_, actionLookup_,
                    *fresh, lookupPending);

    // Sorted lookup table, first-inserted wins on a duplicate key (stable_sort keeps ties in
    // insertion order).
    std::stable_sort(lookupPending.begin(), lookupPending.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::uint32_t lastKey = 0;
    bool haveLast = false;
    for (const auto& [key, slotIndex] : lookupPending) {
        if (haveLast && key == lastKey)
            continue;
        fresh->lookupKeys.push_back(key);
        fresh->lookupSlots.push_back(slotIndex);
        lastKey = key;
        haveLast = true;
    }

    // A republished table that no longer carries an assignment with an active gesture must end
    // that gesture -- the mouse-equivalent of releasing a knob that just vanished.
    for (auto it = gestures_.begin(); it != gestures_.end();) {
        const bool stillPresent =
            std::any_of(fresh->slots.begin(), fresh->slots.end(),
                        [&](const RemoteMappingSnapshot::Slot& slot) { return slot.assignmentId == it->first; });
        if (!stillPresent) {
            if (it->second.gestureActive && it->second.param != nullptr)
                it->second.param->endChangeGesture();
            it = gestures_.erase(it);
        } else {
            ++it;
        }
    }

    fresh->generation = ++snapshotGeneration_;
    publisher_.publish(std::move(fresh));
    updateTimerState();
}

} // namespace synth::midi
