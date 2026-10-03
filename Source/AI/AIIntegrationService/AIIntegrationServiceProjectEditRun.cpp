// Edit plan reading and running: splits a response root (a patch plus a sibling "timelineOps"
// list) into its three phases, checks the plan-wide rules (one id namespace, the mode rule, how a
// writeLane names its node), and runs the phases in their fixed order against a doc and graph. The
// SAME runPlan serves the preview (on a scratch copy, with stand-in builds) and the apply (on the
// live doc and graph, with the app's builds), which is what keeps a preview from describing an
// apply that then fails. Documented in docs/ai/timeline-ops.md#one-edit-plan. Every literal here
// reaches a chat card, so each is ASCII.
#include "AIIntegrationServiceInternal.h"

#include "AI/PatchEval.h"

#include <cmath>
#include <limits>
#include <map>

namespace synth::projectedit {

namespace {

// A plan id: a whole number in uint32 range, the type a patch node id has (validatePatch's rule).
bool readPlanId(const juce::var& v, juce::uint32& out) {
    double value = 0.0;
    if (v.isInt() || v.isInt64())
        value = static_cast<double>(static_cast<juce::int64>(v));
    else if (v.isDouble())
        value = static_cast<double>(v);
    else
        return false;
    if (!std::isfinite(value) || value < 0.0 || value != std::floor(value) ||
        value > static_cast<double>(std::numeric_limits<juce::uint32>::max()))
        return false;
    out = static_cast<juce::uint32>(value);
    return true;
}

juce::String opLabel(int index, const juce::String& opName) {
    return "timelineOps[" + juce::String(index) + "] (" + opName + "): ";
}

bool isTrackCreatingOp(const juce::String& opName) { return opName == "addTrack" || opName == "addInstrumentTrack"; }

juce::var envelopeOf(const juce::Array<juce::var>& ops) {
    if (ops.isEmpty())
        return {};
    juce::DynamicObject::Ptr envelope = new juce::DynamicObject();
    envelope->setProperty("timelineOps", ops);
    return juce::var(envelope.get());
}

// Every id the response introduces shares ONE namespace: patch node ids, each instrumentId and each
// insert id. A model reusing one would leave a connection, modulation or writeLane silently
// pointing at whichever node the resolver happened to pick, so a repeat rejects the whole plan.
class IdNamespace {
public:
    juce::String claim(juce::uint32 id, const juce::String& what) {
        if (auto it = owners_.find(id); it != owners_.end())
            return "Id " + juce::String(id) + " is used twice: as " + it->second + " and as " + what +
                   ". Every id in the response (patch node ids, instrumentId and insert ids) must be distinct.";
        owners_[id] = what;
        return {};
    }

private:
    std::map<juce::uint32, juce::String> owners_;
};

// Reads one addInstrumentTrack op's instrumentId and insert ids into the namespace. TimelineOps
// checks every other field of the op; this only adds what the plan needs on top: each id is a
// usable node id, distinct from every other one, and not already the uid of a live node (a merge
// patch addresses live nodes by uid, so such an id would name two nodes at once).
juce::String reserveBuildIds(int opIndex, juce::DynamicObject& op, int buildIndex,
                             const std::set<juce::uint32>& liveIds, IdNamespace& ids, Plan& out) {
    const juce::String where = opLabel(opIndex, "addInstrumentTrack");
    auto reserve = [&](const juce::var& idVar, int insertIndex, const juce::String& what) -> juce::String {
        if (idVar.isVoid())
            return {};
        juce::uint32 id = 0;
        if (!readPlanId(idVar, id))
            return where + "\"" + what + "\" must be a non-negative integer id.";
        if (liveIds.count(id) > 0)
            return where + "\"" + what + "\" " + juce::String(id) +
                   " is already the id of a node in the current patch. Pick an id no existing node uses.";
        if (const auto clash = ids.claim(id, what + " of timelineOps[" + juce::String(opIndex) + "]");
            clash.isNotEmpty())
            return clash;
        out.reservations.push_back({id, buildIndex, insertIndex});
        return {};
    };

    if (const auto error = reserve(op.getProperty("instrumentId"), -1, "instrumentId"); error.isNotEmpty())
        return error;
    if (auto* inserts = op.getProperty("inserts").getArray())
        for (int i = 0; i < inserts->size(); ++i)
            if (auto* insert = inserts->getReference(i).getDynamicObject())
                if (const auto error = reserve(insert->getProperty("id"), i, "insert " + juce::String(i) + " id");
                    error.isNotEmpty())
                    return error;
    return {};
}

// A writeLane op names its node by exactly one of "nodeUuid" (a node that already exists) or
// "nodeId" (a node this response creates). Checked here, before any phase runs, so a plan that
// gets this wrong is rejected before its track ops would build anything.
juce::String checkLaneAddress(int opIndex, juce::DynamicObject& op, const Plan& plan) {
    const juce::String where = opLabel(opIndex, "writeLane");
    const bool hasUuid = op.hasProperty("nodeUuid");
    const bool hasId = op.hasProperty("nodeId");
    if (hasUuid == hasId)
        return where + "give exactly one of \"nodeUuid\" (a node that already exists) or \"nodeId\" (a node this "
                       "response creates).";
    if (!hasId)
        return {};
    juce::uint32 id = 0;
    if (!readPlanId(op.getProperty("nodeId"), id))
        return where + "\"nodeId\" must be a non-negative integer id.";
    bool created = plan.patchNodeIds.count(id) > 0;
    for (const auto& reservation : plan.reservations)
        created = created || reservation.id == id;
    if (!created)
        return where + "\"nodeId\" " + juce::String(id) +
               " is not a node this response creates. Use a patch node id, an instrumentId or an insert id, or "
               "address an existing node by \"nodeUuid\".";
    return {};
}

juce::String readMode(const juce::var& root, Plan& out) {
    const juce::String mode = root.getProperty("mode", {}).toString();
    out.modeStated = mode.isNotEmpty();
    if (mode.isNotEmpty() && mode != "merge" && mode != "replace")
        return "\"mode\" is \"" + mode + "\". Use \"merge\" or \"replace\".";
    // Track ops build instruments in step 1; a replace in step 2 would delete them again.
    if (out.hasTrackOps && mode == "replace")
        return "The response adds tracks but asks for \"mode\": \"replace\", which would delete the instruments "
               "those tracks just built. Use \"mode\": \"merge\".";
    out.merge = mode == "merge" || (mode.isEmpty() && out.hasTrackOps);
    return {};
}

} // namespace

juce::String readPlan(const juce::var& root, const std::set<juce::uint32>& liveIds, Plan& out) {
    auto* rootObj = root.getDynamicObject();
    if (rootObj == nullptr)
        return "The response is not a JSON object.";

    for (const char* key : {"nodes", "connections", "remove", "modulations", "removeModulations"})
        out.hasPatch = out.hasPatch || rootObj->hasProperty(key);

    juce::Array<juce::var> trackOps, otherOps;
    const juce::var opsVar = rootObj->getProperty("timelineOps");
    if (!opsVar.isVoid() && !opsVar.isArray())
        return "\"timelineOps\" must be an array of operations.";
    if (auto* ops = opsVar.getArray()) {
        if (ops->size() > TimelineOps::kMaxOps)
            return "There are " + juce::String(ops->size()) + " timeline operations, exceeding the limit of " +
                   juce::String(TimelineOps::kMaxOps) + " in one response.";
        for (int i = 0; i < ops->size(); ++i) {
            const juce::String opName = ops->getReference(i).getProperty("op", {}).toString();
            // Anything malformed rides in phase 3, where TimelineOps names the problem.
            const bool trackOp = isTrackCreatingOp(opName);
            (trackOp ? trackOps : otherOps).add(ops->getReference(i));
            (trackOp ? out.trackOpIndex : out.otherOpIndex).push_back(i);
            out.buildsInstrumentTracks = out.buildsInstrumentTracks || opName == "addInstrumentTrack";
        }
    }
    out.hasTrackOps = !trackOps.isEmpty();
    out.trackOps = envelopeOf(trackOps);
    out.otherOps = envelopeOf(otherOps);
    if (!out.hasPatch && trackOps.isEmpty() && otherOps.isEmpty())
        return "The response carries neither a patch nor timeline operations, so there is nothing to apply.";

    if (const auto error = readMode(root, out); error.isNotEmpty())
        return error;

    IdNamespace ids;
    if (auto* nodes = rootObj->getProperty("nodes").getArray()) {
        for (const auto& node : *nodes) {
            juce::uint32 id = 0;
            if (!readPlanId(node.getProperty("id", {}), id))
                continue; // validatePatch names a bad node id
            if (const auto clash = ids.claim(id, "a patch node id"); clash.isNotEmpty())
                return clash;
            out.patchNodeIds.insert(id);
        }
    }

    int buildIndex = 0;
    for (size_t k = 0; k < out.trackOpIndex.size(); ++k) {
        auto* op = trackOps.getReference((int)k).getDynamicObject();
        if (op == nullptr || op->getProperty("op").toString() != "addInstrumentTrack")
            continue;
        if (const auto error = reserveBuildIds(out.trackOpIndex[k], *op, buildIndex++, liveIds, ids, out);
            error.isNotEmpty())
            return error;
    }

    for (size_t k = 0; k < out.otherOpIndex.size(); ++k)
        if (auto* op = otherOps.getReference((int)k).getDynamicObject();
            op != nullptr && op->getProperty("op").toString() == "writeLane")
            if (const auto error = checkLaneAddress(out.otherOpIndex[k], *op, out); error.isNotEmpty())
                return error;
    return {};
}

// -- running -----------------------------------------------------------------------------------

namespace {

// TimelineOps names an op by its index in the envelope it was handed; a phase envelope is a slice
// of the response's list, so the index is mapped back before the message reaches the model.
juce::String renumber(const juce::String& message, const std::vector<int>& originalIndex) {
    if (!message.startsWith("timelineOps["))
        return message;
    const int close = message.indexOfChar(']');
    const int local = message.substring(12, close).getIntValue();
    if (close < 0 || local < 0 || local >= (int)originalIndex.size())
        return message;
    return "timelineOps[" + juce::String(originalIndex[(size_t)local]) + message.substring(close);
}

juce::AudioProcessorGraph::Node* nodeForUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

std::set<juce::AudioProcessorGraph::NodeID> nodeIdsOf(const juce::AudioProcessorGraph& graph) {
    std::set<juce::AudioProcessorGraph::NodeID> ids;
    for (auto* node : graph.getNodes())
        ids.insert(node->nodeID);
    return ids;
}

// Forwards every call to the host doing the real (or stand-in) build and keeps each build's result,
// in op order, so a reserved id can be bound to the node it names without TimelineOps having to
// hand the results back itself.
class RecordingHost final : public TimelineOpsHost {
public:
    explicit RecordingHost(TimelineOpsHost& inner)
        : inner_(inner) {}
    std::optional<InstrumentTrackBuildResult>
    addInstrumentTrack(const juce::String& name, const juce::String& type, bool poly,
                       const std::vector<InstrumentTrackInsert>& inserts) override {
        auto result = inner_.addInstrumentTrack(name, type, poly, inserts);
        if (result.has_value())
            results.push_back(*result);
        return result;
    }
    bool recordBatch(const std::function<void()>& mutation) override { return inner_.recordBatch(mutation); }
    TimelineDoc* editableTimelineDoc() override { return inner_.editableTimelineDoc(); }
    void placeNewModules(const std::vector<juce::AudioProcessorGraph::NodeID>& created) override {
        inner_.placeNewModules(created);
    }

    std::vector<InstrumentTrackBuildResult> results;

private:
    TimelineOpsHost& inner_;
};

// Validate, then apply inside the caller's transaction: the order TimelineOps::applyInsideTransaction
// requires. Validating right before is cheap (validate() works on a copy) and means a phase never
// runs on state it was not checked against.
juce::String runOps(const juce::var& envelope, const std::vector<int>& originalIndex, TimelineDoc& doc,
                    juce::AudioProcessorGraph& graph, TimelineOpsHost& host, juce::StringArray& preview) {
    const auto checked = TimelineOps::validate(envelope, doc, graph, &host);
    if (!checked.ok)
        return renumber(checked.message, originalIndex);
    const auto applied = TimelineOps::applyInsideTransaction(envelope, doc, graph, &host);
    if (!applied.ok)
        return renumber(applied.message, originalIndex);
    if (applied.previewText.isNotEmpty())
        preview.add(applied.previewText);
    return {};
}

// Phase 1's bookkeeping: every node the builds created is hidden from the patch's raw-uid
// namespace, and each reserved id is bound to the node its build result names.
juce::String bindReservations(const Plan& plan, const RecordingHost& recorder, juce::AudioProcessorGraph& graph,
                              const std::set<juce::AudioProcessorGraph::NodeID>& before, PatchIdScope& scope) {
    for (auto* node : graph.getNodes())
        if (before.count(node->nodeID) == 0)
            scope.hiddenNodes.insert(node->nodeID);
    for (const auto& reservation : plan.reservations) {
        if (reservation.buildIndex >= (int)recorder.results.size())
            return "An instrument track was not built, so id " + juce::String(reservation.id) + " names nothing.";
        const auto& built = recorder.results[(size_t)reservation.buildIndex];
        const juce::String uuid = reservation.insertIndex < 0 ? built.instrumentUuid
                                  : reservation.insertIndex < (int)built.insertUuids.size()
                                      ? built.insertUuids[(size_t)reservation.insertIndex]
                                      : juce::String();
        auto* node = nodeForUuid(graph, uuid);
        if (node == nullptr)
            return "The node for id " + juce::String(reservation.id) + " could not be found after its track was built.";
        scope.boundIds[reservation.id] = node->nodeID;
    }
    return {};
}

// Phase 2. Mode: the plan's, or - with no "mode" and no track ops - applyPatch's one-directional
// repair (a replace that is rejected but validates as a merge runs as a merge). The structural gate
// is applyPatch's too (a merge may not take a sounding patch silent; a replace must sound), run only
// when `checkStructure` (the preview), because the apply runs exactly what the preview proved.
juce::String runPatch(const Plan& plan, const juce::var& root, juce::AudioProcessorGraph& graph,
                      const PatchIdScope& scope, bool checkStructure,
                      std::map<int, juce::AudioProcessorGraph::NodeID>& idMap, RunResult& out) {
    out.merge = plan.merge;
    auto validation = AIStateMapper::validatePatch(root, graph, !out.merge, false, false, &scope);
    if (!validation.ok && !out.merge && !plan.modeStated) {
        const auto asMerge = AIStateMapper::validatePatch(root, graph, false, false, false, &scope);
        if (asMerge.ok) {
            out.merge = true;
            validation = asMerge;
        }
    }
    if (!validation.ok)
        return "Patch: " + (validation.message.isNotEmpty() ? validation.message : juce::String("failed validation."));

    bool beforeOk = true; // replace mode has no "before" to regress from
    if (checkStructure && out.merge) {
        const auto beforeEval = evaluatePatch(graph);
        beforeOk = beforeEval.hasAudioOutput && beforeEval.sourceReachesOutput;
    }
    out.patchBefore = AIStateMapper::graphToJSON(graph);
    if (!AIStateMapper::applyJSONToGraph(root, graph, !out.merge, false, true, &idMap, &scope))
        return "Patch: it could not be applied to the graph.";
    out.patchAfter = AIStateMapper::graphToJSON(graph);

    if (checkStructure) {
        const auto afterEval = evaluatePatch(graph);
        if (beforeOk && !(afterEval.hasAudioOutput && afterEval.sourceReachesOutput))
            return "Patch: " +
                   (afterEval.detail.isNotEmpty() ? afterEval.detail : juce::String("produces no usable signal path"));
    }
    return {};
}

// Phase 3's one rewrite: a writeLane "nodeId" becomes the "nodeUuid" of the node that id now
// denotes, so TimelineOps only ever sees uuids. Done on a fresh copy in every run - the preview's
// uuids are the scratch graph's, never the live one's.
juce::String rewriteLaneNodeIds(juce::var& envelope, const Plan& plan, juce::AudioProcessorGraph& graph,
                                const PatchIdScope& scope,
                                const std::map<int, juce::AudioProcessorGraph::NodeID>& idMap) {
    auto* ops = envelope.getProperty("timelineOps", {}).getArray();
    for (int i = 0; ops != nullptr && i < ops->size(); ++i) {
        auto* op = ops->getReference(i).getDynamicObject();
        if (op == nullptr || !op->hasProperty("nodeId"))
            continue;
        juce::uint32 id = 0;
        readPlanId(op->getProperty("nodeId"), id); // checked by readPlan
        std::optional<juce::AudioProcessorGraph::NodeID> nodeId;
        if (auto bound = scope.boundIds.find(id); bound != scope.boundIds.end())
            nodeId = bound->second;
        else if (auto mapped = idMap.find((int)id); mapped != idMap.end() && plan.patchNodeIds.count(id) > 0)
            nodeId = mapped->second;
        auto* node = nodeId.has_value() ? graph.getNodeForId(*nodeId) : nullptr;
        if (node == nullptr)
            return opLabel(plan.otherOpIndex[(size_t)i], "writeLane") + "\"nodeId\" " + juce::String(id) +
                   " names no node after the patch applied (did the patch remove it?).";
        op->setProperty("nodeUuid", AIStateMapper::ensureNodeUuid(node));
        op->removeProperty("nodeId");
    }
    return {};
}

} // namespace

RunResult runPlan(const Plan& plan, const juce::var& root, TimelineDoc& doc, juce::AudioProcessorGraph& graph,
                  TimelineOpsHost& buildHost, bool checkStructure) {
    RunResult out;
    auto fail = [&out](const juce::String& message) {
        out.ok = false;
        out.message = message;
        return out;
    };
    juce::StringArray preview;

    // 1. Track-creating ops, in list order.
    PatchIdScope scope;
    if (!plan.trackOps.isVoid()) {
        const auto before = nodeIdsOf(graph);
        RecordingHost recorder(buildHost);
        if (const auto error = runOps(plan.trackOps, plan.trackOpIndex, doc, graph, recorder, preview);
            error.isNotEmpty())
            return fail(error);
        if (const auto error = bindReservations(plan, recorder, graph, before, scope); error.isNotEmpty())
            return fail(error);
    }

    // 2. The patch, its namespace widened by the ids phase 1 bound.
    std::map<int, juce::AudioProcessorGraph::NodeID> idMap;
    if (plan.hasPatch)
        if (const auto error = runPatch(plan, root, graph, scope, checkStructure, idMap, out); error.isNotEmpty())
            return fail(error);

    // 3. Every other op, in list order, with nodeId references resolved to uuids.
    if (!plan.otherOps.isVoid()) {
        juce::var ops = juce::JSON::parse(juce::JSON::toString(plan.otherOps));
        if (const auto error = rewriteLaneNodeIds(ops, plan, graph, scope, idMap); error.isNotEmpty())
            return fail(error);
        if (const auto error = runOps(ops, plan.otherOpIndex, doc, graph, buildHost, preview); error.isNotEmpty())
            return fail(error);
    }

    for (int i = 1; i < preview.size(); ++i)
        preview.set(i, preview[i].substring(0, 1).toLowerCase() + preview[i].substring(1));
    out.opsPreview = preview.joinIntoString("; ");
    return out;
}

// -- the preview's stand-in build ---------------------------------------------------------------

// The graph side of an instrument track cannot be dry-run (docs/ai/timeline-ops.md#addinstrumenttrack):
// the real build needs the app's macros, card placement and channel flow. What a preview needs is
// narrower - nodes of the right TYPES, with the inserts' params applied, carrying uuids - so that a
// patch's "destParam" onto an insert and a writeLane's range check resolve against real
// processors. These stand-ins are exactly that, on the scratch graph only: unwired, no Track In,
// no envelope or channel strip, no macro. The doc side is the real thing (the MIDI track the build
// would add), so later ops in the plan can address the track by name.
std::optional<InstrumentTrackBuildResult>
StandInHost::addInstrumentTrack(const juce::String& name, const juce::String& instrumentType, bool poly,
                                const std::vector<InstrumentTrackInsert>& inserts) {
    juce::ignoreUnused(poly);
    if (!doc_.addTrack(TrackKind::Midi, name).isValid())
        return std::nullopt;
    auto addStandIn = [this](const juce::String& type, const juce::var& params) -> juce::String {
        auto processor = AIStateMapper::createModule(type);
        if (processor == nullptr)
            return {};
        if (auto* paramsObj = params.getDynamicObject())
            AIStateMapper::applyUntrustedParams(processor.get(), paramsObj);
        auto node = graph_.addNode(std::move(processor));
        return node != nullptr ? AIStateMapper::ensureNodeUuid(node.get()) : juce::String();
    };
    InstrumentTrackBuildResult result;
    result.instrumentUuid = addStandIn(instrumentType, {});
    for (const auto& insert : inserts)
        result.insertUuids.push_back(addStandIn(insert.type, insert.params));
    return result;
}

bool StandInHost::recordBatch(const std::function<void()>& mutation) {
    mutation();
    return true;
}

} // namespace synth::projectedit
