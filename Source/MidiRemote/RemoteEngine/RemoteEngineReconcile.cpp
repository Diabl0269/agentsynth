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

// FRO236 (docs/control/midi-remote.md#continuous-targets): resolves a masterVolume continuous
// target through the injected ContinuousParameterLookup -- otherwise identical to
// resolveParameterTarget above (a setter's graph==nullptr rebuild keeps the previous resolution).
// bpm/playhead never call this: addSlot leaves them param==nullptr, orphaned==false unconditionally
// (there is nothing to resolve against a graph -- see RemoteMappingSnapshot::Slot::continuous).
void resolveContinuousParameterTarget(const Assignment& assignment, juce::AudioProcessorGraph* graph,
                                      const ContinuousParameterLookup& continuousLookup,
                                      const PreviousResolution& previousResolution, RemoteMappingSnapshot::Slot& slot) {
    if (graph != nullptr) {
        slot.param = continuousLookup ? continuousLookup(*graph, assignment.target.continuous.kind) : nullptr;
        slot.orphaned = slot.param == nullptr;
        return;
    }
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

// FRO140: a paired-CC slot at CC n is ALSO reached by its LSB partner CC n+32, so the LSB key routes
// to the same slot. Appended after every primary entry (see the stable_sort below): an explicit
// assignment on CC n+32 keeps that message. FRO142: `lookupEligible[i]` mirrors addSlot's own
// "on the active page, or not a page-scoped assignment at all" test -- an alias must never reach a
// slot that inactive-page filtering itself would refuse to route to.
void addPairedAliasEntries(const std::vector<ControllerProfile>& profiles,
                           const std::vector<RemoteMappingSnapshot::SourceEntry>& sources,
                           const RemoteMappingSnapshot& fresh, const std::vector<bool>& lookupEligible,
                           std::vector<std::pair<std::uint32_t, std::int32_t>>& pending) {
    for (std::size_t i = 0; i < fresh.slots.size(); ++i) {
        if (!lookupEligible[i])
            continue;
        const auto& slot = fresh.slots[i];
        if (!isPairedEncoding(slot.encoding) || slot.spec.type != MessageType::cc ||
            slot.spec.number + kPairedLsbOffset > 63)
            continue;
        const int sourceIndex = findSourceIndexForProfile(profiles, sources, slot.profileId);
        if (sourceIndex < 0)
            continue;
        pending.emplace_back(
            packLookupKey(sourceIndex, MessageType::cc, slot.spec.channel, slot.spec.number + kPairedLsbOffset),
            static_cast<std::int32_t>(i));
    }
}

// One parameter-, action- or nodeCommand-target assignment -> one Slot, appended to `fresh`, plus
// its pending lookup-table entry -- unless `includeInLookup` is false (FRO142: an inactive-page
// project assignment). It still gets a real, resolved Slot either way -- see rebuildAndPublish's
// own comment on why an excluded page must still be resolved -- just no way for the MIDI path to
// ever reach it.
void addSlot(const Assignment& assignment, juce::AudioProcessorGraph* graph, const ProcessorByUuid& processorByUuid,
             const NodeIdByUuid& nodeIdByUuid, const PreviousResolution& previousResolution,
             const std::vector<ControllerProfile>& profiles, const ActionCommandLookup& actionLookup,
             const ContinuousParameterLookup& continuousLookup, RemoteMappingSnapshot& fresh,
             std::vector<std::pair<std::uint32_t, std::int32_t>>& lookupPending, std::vector<bool>& lookupEligible,
             bool includeInLookup) {
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
    slot.spec = assignment.spec;
    slot.messageNumber = assignment.spec.number;
    slot.profileId = assignment.control.profileId;

    if (assignment.target.isParameter())
        resolveParameterTarget(assignment, graph, processorByUuid, previousResolution, slot);
    else if (assignment.target.isAction() && actionLookup)
        slot.commandId = actionLookup(assignment.target.action.actionId);
    else if (assignment.target.isNodeCommand())
        resolveNodeCommandTarget(assignment, graph, nodeIdByUuid, previousResolution, slot);
    else if (assignment.target.isContinuous()) {
        slot.continuous = assignment.target.continuous.kind;
        if (slot.continuous == ContinuousTargetKind::masterVolume)
            resolveContinuousParameterTarget(assignment, graph, continuousLookup, previousResolution, slot);
        // bpm/playhead: no graph resolution -- param stays nullptr, orphaned stays false (see
        // RemoteMappingSnapshot::Slot::continuous's own comment).
    }

    const Control* control = findControl(profiles, assignment.control.profileId, assignment.control.controlId);
    const bool controlIsButtonLike =
        control != nullptr && (control->kind == ControlKind::button || control->kind == ControlKind::pad);
    slot.buttonLike = assignment.target.isAction() || assignment.target.isNodeCommand() || assignment.target.isPage() ||
                      controlIsButtonLike || dynamic_cast<juce::AudioParameterBool*>(slot.param) != nullptr;

    slot.onActivePage = includeInLookup;
    const int slotIndex = static_cast<int>(fresh.slots.size());
    fresh.slots.push_back(slot);
    lookupEligible.push_back(includeInLookup);
    if (includeInLookup)
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
    // FRO142: parallel to fresh->slots -- whether the MIDI path may ever reach that slot at all
    // (addPairedAliasEntries' own guard). See addSlot's comment for why a page-excluded assignment
    // still gets a Slot pushed here, just with this false.
    std::vector<bool> lookupEligible;

    // FRO142 (docs/control/midi-remote.md#pages): a PROJECT assignment is resolved every real
    // reconcile regardless of page (a setActivePage()-only rebuild has graph == nullptr and can only
    // carry a resolution FORWARD from the last real one -- RemoteEngineReconcile.cpp's file header
    // comment -- so an assignment that never had a Slot before its page became active would stay
    // unresolved until some unrelated graph change happened to reach reconcile()). Only its own
    // page's assignment gets a LOOKUP entry, though -- that's what makes the MIDI-path table already
    // page-filtered, with no page awareness needed in the apply path itself. A GLOBAL profile action
    // (the loop below) carries no page and is always lookup-eligible -- that's what keeps a page
    // Target's own button reachable no matter which page is active.
    for (const auto& assignment : assignments_) {
        const bool onActivePage = assignment.page == getActivePage(assignment.control.profileId);
        addSlot(assignment, graph, processorByUuid, nodeIdByUuid, previousResolution, profiles_, actionLookup_,
                continuousLookup_, *fresh, lookupPending, lookupEligible, onActivePage);
    }
    for (const auto& profile : profiles_)
        for (const auto& assignment : profile.actions)
            addSlot(assignment, graph, processorByUuid, nodeIdByUuid, previousResolution, profiles_, actionLookup_,
                    continuousLookup_, *fresh, lookupPending, lookupEligible, /*includeInLookup=*/true);

    addPairedAliasEntries(profiles_, fresh->sources, *fresh, lookupEligible, lookupPending);

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

    // Same cleanup for feedback_ (FRO139): an assignment that no longer exists needs no cooldown
    // state kept around for it. Unlike gestures_ this never republishes on setProfiles/setAssignments
    // alone -- see setProfiles()'s own comment for why THAT case clears feedback_ wholesale instead.
    for (auto it = feedback_.begin(); it != feedback_.end();) {
        const bool stillPresent =
            std::any_of(fresh->slots.begin(), fresh->slots.end(),
                        [&](const RemoteMappingSnapshot::Slot& slot) { return slot.assignmentId == it->first; });
        if (!stillPresent)
            it = feedback_.erase(it);
        else
            ++it;
    }

    // FRO236: same cleanup for continuousGestures_ as gestures_/feedback_ above.
    for (auto it = continuousGestures_.begin(); it != continuousGestures_.end();) {
        const bool stillPresent =
            std::any_of(fresh->slots.begin(), fresh->slots.end(),
                        [&](const RemoteMappingSnapshot::Slot& slot) { return slot.assignmentId == it->first; });
        if (!stillPresent)
            it = continuousGestures_.erase(it);
        else
            ++it;
    }

    fresh->generation = ++snapshotGeneration_;
    publisher_.publish(std::move(fresh));
    updateTimerState();
}

} // namespace synth::midi
