// AIStateMapper — undo/redo snapshot restore.
//
// applySnapshotPreservingNodes plans and applies a snapshot restore while keeping as many live
// nodes (and their listeners/UI state) in place as possible, instead of tearing down and rebuilding
// the whole graph on every Cmd+Z. The class itself is declared in AIStateMapper.h.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace synth {

namespace {

/**
 * One node of a snapshot, resolved far enough that the whole restore can be planned before the
 * graph is touched at all. After planning, either `kept`/`liveId` names the live node this entry
 * updates in place, or `created` holds the processor waiting to be added.
 */
struct SnapshotTargetNode {
    const juce::DynamicObject* obj = nullptr;
    juce::uint32 id = 0;
    juce::String uuid;
    juce::String type;
    bool kept = false;
    // Node IDENTITY, never a cached Node*: applying a parameter to a node that is already in the
    // graph runs its listeners synchronously, and one of them re-anchors the module's cables — so
    // any Node* held across that call can dangle. Everything below re-resolves through the graph.
    bool hasLiveId = false;
    juce::AudioProcessorGraph::NodeID liveId;
    std::unique_ptr<juce::AudioProcessor> created;
};

/** A snapshot connection with both endpoints resolved to indices into the target-node vector. */
struct SnapshotTargetConnection {
    size_t srcIndex = 0;
    size_t dstIndex = 0;
    int srcPort = 0;
    int dstPort = 0;
};

// Re-applies a node's non-parameter state ONLY when it differs from what the module already holds.
// setExtraState is not a cheap setter — SamplerModule and WavetableOscillatorModule read a file off
// disk from it — so a restore that re-applied it unconditionally would reload every sample and every
// wavetable on every single Cmd+Z.
void applyExtraStateIfChanged(juce::AudioProcessor* processor, const juce::DynamicObject* nObj) {
    if (!nObj->hasProperty("state"))
        return;
    auto* mb = dynamic_cast<ModuleBase*>(processor);
    if (mb == nullptr)
        return;

    const juce::var target = nObj->getProperty("state");
    if (juce::JSON::toString(mb->getExtraState()) == juce::JSON::toString(target))
        return;

    mb->setExtraState(target);
}

// Copies the snapshot's stored canvas position onto a live node, if it carries one.
void applyPositionToNode(juce::AudioProcessorGraph::Node* node, const juce::DynamicObject* nObj) {
    if (auto* posObj = nObj->getProperty("position").getDynamicObject()) {
        node->properties.set("x", posObj->getProperty("x"));
        node->properties.set("y", posObj->getProperty("y"));
    }
}

} // namespace

bool AIStateMapper::applySnapshotPreservingNodes(const juce::var& snapshot, juce::AudioProcessorGraph& graph,
                                                 std::function<void()> beforeNodeRemoval) {
    auto* rootObj = snapshot.isObject() ? snapshot.getDynamicObject() : nullptr;
    if (rootObj == nullptr)
        return false;

    // A merge delta describes a CHANGE, not a target state: there is nothing to diff a live graph
    // against, so it is not something this entry point can restore.
    if (rootObj->hasProperty("remove") || rootObj->hasProperty("removeModulations"))
        return false;

    const auto* nodesList = rootObj->hasProperty("nodes") ? rootObj->getProperty("nodes").getArray() : nullptr;
    const auto* connList =
        rootObj->hasProperty("connections") ? rootObj->getProperty("connections").getArray() : nullptr;
    if (nodesList == nullptr || connList == nullptr)
        return false;

    // ---------------------------------------------------------------------------------------
    // Plan. Nothing below this block mutates the graph, so every rejection here leaves the graph
    // exactly as it was and the caller's full-rebuild fallback starts from a clean slate.
    // ---------------------------------------------------------------------------------------

    // Live nodes, indexed by identity. A node with no uuid, or a uuid two nodes share, means
    // identity cannot be decided — give up rather than guess which node the snapshot meant.
    std::map<juce::String, juce::AudioProcessorGraph::Node*> liveByUuid;
    for (auto* node : graph.getNodes()) {
        if (node->getProcessor() == nullptr)
            return false;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isEmpty() || !liveByUuid.emplace(uuid, node).second)
            return false;
    }

    std::vector<SnapshotTargetNode> targets;
    targets.reserve(static_cast<size_t>(nodesList->size()));
    std::set<juce::String> targetUuids;
    std::set<juce::uint32> targetIds;
    std::map<juce::uint32, size_t> targetIndexById;

    for (const auto& nVar : *nodesList) {
        const auto* nObj = nVar.getDynamicObject();
        if (nObj == nullptr)
            return false;

        SnapshotTargetNode t;
        t.obj = nObj;
        if (!detail::extractUnsignedInt(nObj->getProperty("id"), t.id))
            return false;

        t.uuid = nObj->getProperty("uuid").toString();
        t.type = nObj->getProperty("type").toString();
        if (t.uuid.isEmpty() || t.type.isEmpty())
            return false;
        if (!targetUuids.insert(t.uuid).second || !targetIds.insert(t.id).second)
            return false;

        const auto liveEntry = liveByUuid.find(t.uuid);
        if (liveEntry != liveByUuid.end()) {
            // The same identity must still name the same module. A uuid whose type changed is a
            // corrupt snapshot, not an update-in-place — and patchTypeMatchesProcessor is what
            // knows that an "Amp Env" processor legitimately serializes as type "ADSR".
            if (!detail::patchTypeMatchesProcessor(liveEntry->second->getProcessor(), t.type))
                return false;
            t.kept = true;
            t.hasLiveId = true;
            t.liveId = liveEntry->second->nodeID;
        } else {
            t.created = createModule(t.type);
            if (t.created == nullptr)
                return false;
        }

        targetIndexById[t.id] = targets.size();
        targets.push_back(std::move(t));
    }

    std::vector<SnapshotTargetConnection> targetConnections;
    targetConnections.reserve(static_cast<size_t>(connList->size()));

    for (const auto& cVar : *connList) {
        const auto* cObj = cVar.getDynamicObject();
        if (cObj == nullptr)
            return false;
        if (!cObj->hasProperty("srcPort") || !cObj->hasProperty("dstPort"))
            return false;

        juce::uint32 srcId = 0, dstId = 0;
        if (!detail::extractUnsignedInt(cObj->getProperty("src"), srcId) ||
            !detail::extractUnsignedInt(cObj->getProperty("dst"), dstId))
            return false;

        const auto srcEntry = targetIndexById.find(srcId);
        const auto dstEntry = targetIndexById.find(dstId);
        if (srcEntry == targetIndexById.end() || dstEntry == targetIndexById.end())
            return false;

        SnapshotTargetConnection c;
        c.srcIndex = srcEntry->second;
        c.dstIndex = dstEntry->second;
        c.srcPort = static_cast<int>(cObj->getProperty("srcPort"));
        c.dstPort = static_cast<int>(cObj->getProperty("dstPort"));
        if (c.srcPort == -1)
            c.srcPort = juce::AudioProcessorGraph::midiChannelIndex;
        if (c.dstPort == -1)
            c.dstPort = juce::AudioProcessorGraph::midiChannelIndex;
        targetConnections.push_back(c);
    }

    // ---------------------------------------------------------------------------------------
    // Execute. Every topology op is issued with UpdateKind::none and the render sequence is
    // rebuilt exactly once at the end, so a mixed restore pays for the rebuild once instead of
    // once per node and per wire — and a restore that changes no topology pays nothing at all.
    // No graph callback lock is taken: parameter stores are atomic, and JUCE's graph publishes
    // topology to the audio thread through its own render-sequence exchange.
    //
    // Parameters go FIRST and all structure is reconciled after them, because a parameter write
    // can re-enter and change the graph (ModuleComponent re-anchors a module's cables when "poly"
    // flips). Reconciling afterwards means such a rewire is just more state for the diff to
    // correct, instead of a hazard the plan has to anticipate.
    // ---------------------------------------------------------------------------------------
    bool topologyChanged = false;
    bool teardownNotified = false;
    auto notifyBeforeNodeRemoval = [&beforeNodeRemoval, &teardownNotified] {
        if (teardownNotified)
            return;
        teardownNotified = true;
        if (beforeNodeRemoval)
            beforeNodeRemoval();
    };
    auto resolve = [&graph](const SnapshotTargetNode& t) {
        return t.hasLiveId ? graph.getNodeForId(t.liveId) : nullptr;
    };

    // 1. Kept nodes: parameters, extra state and position updated in place. No lock is held across
    //    this loop, so the audio callback keeps running through it.
    for (auto& t : targets) {
        if (!t.kept)
            continue;

        if (auto* live = resolve(t))
            if (auto* pObj = t.obj->getProperty("params").getDynamicObject())
                applyParamsToProcessor(live->getProcessor(), pObj, /*trusted=*/true, /*skipUnchanged=*/true);
        if (auto* live = resolve(t))
            applyExtraStateIfChanged(live->getProcessor(), t.obj);
        if (auto* live = resolve(t)) {
            applyPositionToNode(live, t.obj);
            detail::applyDisplayNameToNode(live, t.obj);
        }
    }

    // 2. Drop every live node the snapshot does not contain. Their processors — and every UI object
    //    reading them — die here, which is why the caller gets its one chance to detach first. The
    //    set is recomputed from the live graph rather than taken from the plan, so a node that a
    //    re-entrant rewire created above (it has no uuid) is swept up too.
    std::vector<juce::AudioProcessorGraph::NodeID> doomed;
    for (auto* node : graph.getNodes())
        if (targetUuids.count(node->properties["uuid"].toString()) == 0)
            doomed.push_back(node->nodeID);

    if (!doomed.empty()) {
        notifyBeforeNodeRemoval();
        for (auto nodeId : doomed)
            graph.removeNode(nodeId, juce::AudioProcessorGraph::UpdateKind::none);
        topologyChanged = true;
    }

    // 3. Create every snapshot node the graph now lacks, re-adopting the snapshot's node id when it
    //    is free so ids stay stable across the restore (a merge-mode patch card addresses existing
    //    nodes by uid). Removals ran first precisely so those ids are available. Identity is
    //    re-derived from the live graph, so a node destroyed by a re-entrant rewire is rebuilt here.
    std::map<juce::String, juce::AudioProcessorGraph::NodeID> liveIdByUuid;
    for (auto* node : graph.getNodes())
        liveIdByUuid[node->properties["uuid"].toString()] = node->nodeID;

    for (auto& t : targets) {
        const auto liveEntry = liveIdByUuid.find(t.uuid);
        if (liveEntry != liveIdByUuid.end()) {
            t.hasLiveId = true;
            t.liveId = liveEntry->second;
            continue;
        }

        t.hasLiveId = false;
        auto processor = t.created != nullptr ? std::move(t.created) : createModule(t.type);
        if (processor == nullptr)
            continue;

        if (auto* pObj = t.obj->getProperty("params").getDynamicObject())
            applyParamsToProcessor(processor.get(), pObj, /*trusted=*/true);
        applyExtraStateToProcessor(processor.get(), t.obj, /*trusted=*/true);

        std::optional<juce::AudioProcessorGraph::NodeID> preservedId;
        const juce::AudioProcessorGraph::NodeID wantedId(t.id);
        if (t.id > 0 && graph.getNodeForId(wantedId) == nullptr)
            preservedId = wantedId;

        auto node = graph.addNode(std::move(processor), preservedId, juce::AudioProcessorGraph::UpdateKind::none);
        topologyChanged = true;
        if (node == nullptr)
            continue; // Unreachable in practice (the id was checked free); wires to it are skipped below.

        t.hasLiveId = true;
        t.liveId = node->nodeID;
        node->properties.set("uuid", t.uuid);
        detail::mirrorUuidIntoProcessor(node.get(), t.uuid);
        applyPositionToNode(node.get(), t.obj);
        detail::applyDisplayNameToNode(node.get(), t.obj);
    }

    // 4. Connections: apply the delta only. A restore that changed no wiring issues no graph
    //    topology call here, which is what keeps a parameter-only undo free of any rebuild.
    std::set<juce::AudioProcessorGraph::Connection> wanted;
    for (const auto& c : targetConnections) {
        auto* src = resolve(targets[c.srcIndex]);
        auto* dst = resolve(targets[c.dstIndex]);
        if (src == nullptr || dst == nullptr)
            continue;
        wanted.insert({{src->nodeID, c.srcPort}, {dst->nodeID, c.dstPort}});
    }

    for (const auto& conn : graph.getConnections()) {
        if (wanted.count(conn) == 0) {
            graph.removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
            topologyChanged = true;
        }
    }

    for (const auto& conn : wanted) {
        if (!graph.isConnected(conn) && graph.addConnection(conn, juce::AudioProcessorGraph::UpdateKind::none))
            topologyChanged = true;
    }

    if (topologyChanged) {
        graph.rebuild();
        graph.sendChangeMessage(); // Suppressed per-op above; announced once here instead.
    }

    return true;
}

} // namespace synth
