// Edit plan reading and running: splits a response root (a patch plus a sibling "timelineOps"
// list) into its three phases, checks the plan-wide rules (one id namespace, the mode rule, how a
// writeLane names its node), and runs the phases in their fixed order against a doc and graph. The
// SAME runPlan serves the preview (on a scratch copy, with stand-in builds) and the apply (on the
// live doc and graph, with the app's builds), which is what keeps a preview from describing an
// apply that then fails. Documented in docs/ai/timeline-ops.md#one-edit-plan. Every literal here
// reaches a chat card, so each is ASCII.
#include "AIIntegrationServiceInternal.h"

#include "AI/PatchEval.h"
#include "Modules/ModuleBase.h"
#include "SoundPacks/CoreSounds.h"
#include "Timeline/TimelineOps.h"
#include "Timeline/TimelineOpsLaneRepair.h"

#include <cmath>
#include <limits>
#include <map>

namespace synth::projectedit {

namespace {
// A clamp or a repair is the app mending something, not the model's mistake.
bool isBadDetail(const PlanNote& note) {
    return note.kind != PlanNote::Kind::Clamped && note.kind != PlanNote::Kind::Repaired;
}
} // namespace

bool exceedsBadDetailLimit(const std::vector<PlanNote>& notes) {
    int bad = 0;
    for (const auto& note : notes)
        bad += isBadDetail(note) ? 1 : 0;
    return bad > kMaxBadDetails;
}

juce::String firstBadDetailError(const std::vector<PlanNote>& notes) {
    for (const auto& note : notes)
        if (isBadDetail(note))
            return note.error;
    return {};
}

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

// Every id the response introduces shares ONE namespace: patch node ids, each instrumentId, each
// envelope id and each insert id. A model reusing one would leave a connection, modulation or writeLane silently
// pointing at whichever node the resolver happened to pick, so a repeat rejects the whole plan.
class IdNamespace {
public:
    juce::String claim(juce::uint32 id, const juce::String& what) {
        if (auto it = owners_.find(id); it != owners_.end())
            return "Id " + juce::String(id) + " is used twice: as " + it->second + " and as " + what +
                   ". Every id in the response (patch node ids, instrumentId, insert ids and envelope ids) must be "
                   "distinct.";
        owners_[id] = what;
        return {};
    }

private:
    std::map<juce::uint32, juce::String> owners_;
};

// Reads one addInstrumentTrack op's instrumentId, envelope id and insert ids into the namespace. TimelineOps
// checks every other field of the op; this only adds what the plan needs on top: each id is a
// usable node id, distinct from every other one, and not already the uid of a live node (a merge
// patch addresses live nodes by uid, so such an id would name two nodes at once). A bad instrumentId
// refuses the plan (the track's other ids hang off it); a bad envelope or insert id is left out and
// noted instead, and a writeLane that names it is skipped (see Plan::unusableIds).
juce::String reserveBuildIds(int opIndex, juce::DynamicObject& op, int buildIndex,
                             const std::set<juce::uint32>& liveIds, IdNamespace& ids, Plan& out) {
    const juce::String where = opLabel(opIndex, "addInstrumentTrack");
    auto reserve = [&](const juce::var& idVar, int insertIndex, const juce::String& what) -> juce::String {
        if (idVar.isVoid())
            return {};
        juce::uint32 id = 0;
        juce::String problem;
        if (!readPlanId(idVar, id))
            problem = where + "\"" + what + "\" must be a non-negative integer id.";
        else if (liveIds.count(id) > 0)
            problem = where + "\"" + what + "\" " + juce::String(id) +
                      " is already the id of a node in the current patch. Pick an id no existing node uses.";
        else
            problem = ids.claim(id, what + " of timelineOps[" + juce::String(opIndex) + "]");
        if (problem.isEmpty()) {
            out.reservations.push_back({id, buildIndex, insertIndex});
            return {};
        }
        if (insertIndex == Reservation::kInstrument)
            return problem;
        out.notes.push_back(
            {PlanNote::Kind::Ignored, problem,
             "an id on " + juce::String(insertIndex == Reservation::kEnvelope ? "a track's envelope" : "an effect") +
                 " was left out because another module already uses it"});
        if (readPlanId(idVar, id))
            out.unusableIds.insert(id);
        return {};
    };

    if (const auto error = reserve(op.getProperty("instrumentId"), Reservation::kInstrument, "instrumentId");
        error.isNotEmpty())
        return error;
    if (const auto error =
            reserve(op.getProperty("envelope").getProperty("id", {}), Reservation::kEnvelope, "envelope id");
        error.isNotEmpty())
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
// "nodeId" (a node this response creates). Checked here, before any phase runs. A lane that gets this
// wrong is skipped (with its error kept, for the case the plan is refused after all) rather than
// refusing a plan whose tracks and notes are fine.
struct LaneAddressProblem {
    juce::String error, note;
};

LaneAddressProblem checkLaneAddress(int opIndex, juce::DynamicObject& op, const Plan& plan) {
    const juce::String where = opLabel(opIndex, "writeLane");
    const bool hasUuid = op.hasProperty("nodeUuid");
    const bool hasId = op.hasProperty("nodeId");
    if (hasUuid == hasId)
        return {where + "give exactly one of \"nodeUuid\" (a node that already exists) or \"nodeId\" (a node this "
                        "response creates).",
                "an automation lane that did not say clearly which module it belongs to"};
    if (!hasId)
        return {};
    juce::uint32 id = 0;
    if (!readPlanId(op.getProperty("nodeId"), id))
        return {where + "\"nodeId\" must be a non-negative integer id.",
                "an automation lane that did not say clearly which module it belongs to"};
    if (plan.unusableIds.count(id) > 0)
        return {where + "\"nodeId\" " + juce::String(id) +
                    " is an id the response used for more than one module, so it names none of them.",
                "an automation lane on a module whose id was used twice"};
    bool created = plan.patchNodeIds.count(id) > 0;
    for (const auto& reservation : plan.reservations)
        created = created || reservation.id == id;
    if (!created)
        return {where + "\"nodeId\" " + juce::String(id) +
                    " is not a node this response creates. Use a patch node id, an instrumentId, an envelope id or an "
                    "insert id, or "
                    "address an existing node by \"nodeUuid\".",
                "an automation lane on a module the answer never created"};
    return {};
}

// True if the op list places notes: a placeClips clip with a non-empty "notes" array, or a placeMidiClip.
bool placesNotes(const juce::Array<juce::var>& ops) {
    for (const auto& op : ops) {
        const juce::String name = op.getProperty("op", {}).toString();
        if (name == "placeMidiClip")
            return true;
        if (name != "placeClips")
            continue;
        if (auto* clips = op.getProperty("clips", {}).getArray())
            for (const auto& clip : *clips)
                if (auto* notes = clip.getProperty("notes", {}).getArray(); notes != nullptr && !notes->isEmpty())
                    return true;
    }
    return false;
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

    // A writeLane that names its node wrongly is left out of the plan; everything else about the plan stands.
    juce::Array<juce::var> keptOps;
    std::vector<int> keptIndex;
    for (size_t k = 0; k < out.otherOpIndex.size(); ++k) {
        if (auto* op = otherOps.getReference((int)k).getDynamicObject();
            op != nullptr && op->getProperty("op").toString() == "writeLane")
            if (const auto problem = checkLaneAddress(out.otherOpIndex[k], *op, out); problem.error.isNotEmpty()) {
                out.notes.push_back({PlanNote::Kind::Skipped, problem.error, problem.note});
                continue;
            }
        keptOps.add(otherOps.getReference((int)k));
        keptIndex.push_back(out.otherOpIndex[k]);
    }
    out.otherOps = envelopeOf(keptOps);
    out.otherOpIndex = keptIndex;

    // The refusals a repaired plan still gets: too many bad details, or nothing left that does anything - for a
    // plan that builds tracks, no notes (a song of empty tracks is not the song that was asked for).
    if (!out.notes.empty()) {
        const auto first = firstBadDetailError(out.notes);
        if (first.isNotEmpty()) {
            const bool nothingLeft = !out.hasPatch && !out.hasTrackOps && keptOps.isEmpty();
            if (exceedsBadDetailLimit(out.notes) || nothingLeft || (out.hasTrackOps && !placesNotes(keptOps)))
                return first;
        }
    }
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
    std::optional<InstrumentTrackBuildResult> addInstrumentTrack(const juce::String& name, const juce::String& type,
                                                                 bool poly,
                                                                 const std::vector<InstrumentTrackInsert>& inserts,
                                                                 const juce::var& envelopeParams,
                                                                 const juce::var& instrumentParams) override {
        auto result = inner_.addInstrumentTrack(name, type, poly, inserts, envelopeParams, instrumentParams);
        if (result.has_value())
            results.push_back(*result);
        return result;
    }
    std::optional<InstrumentTrackBuildResult>
    addInstrumentTrackWithSound(const juce::String& name, const juce::String& type, bool poly,
                                const std::vector<InstrumentTrackInsert>& inserts, const juce::var& envelopeParams,
                                const juce::var& instrumentParams, const juce::String& sound) override {
        auto result =
            inner_.addInstrumentTrackWithSound(name, type, poly, inserts, envelopeParams, instrumentParams, sound);
        if (result.has_value())
            results.push_back(*result);
        return result;
    }
    bool recordBatch(const std::function<void()>& mutation) override { return inner_.recordBatch(mutation); }
    TimelineDoc* editableTimelineDoc() override { return inner_.editableTimelineDoc(); }
    void placeNewModules(const std::vector<juce::AudioProcessorGraph::NodeID>& created) override {
        inner_.placeNewModules(created);
    }
    bool canSetTempo() const override { return inner_.canSetTempo(); }
    bool setTempo(double bpm) override { return inner_.setTempo(bpm); }

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
        if (reservation.insertIndex == Reservation::kEnvelope && built.envelopeUuid.isEmpty())
            return "The instrument track built for envelope id " + juce::String(reservation.id) +
                   " has no envelope, so the id names nothing.";
        const juce::String uuid = reservation.insertIndex == Reservation::kInstrument ? built.instrumentUuid
                                  : reservation.insertIndex == Reservation::kEnvelope ? built.envelopeUuid
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

// True when `module` has a modulation jack on channel `port`.
bool hasModulationChannel(const ModuleBase& module, int port) {
    for (const auto& target : module.getModulationTargets())
        if (target.channelIndex == port)
            return true;
    return false;
}

// True when the modulation entry names a destination the real node cannot take: a "destParam" that resolves to no
// jack, or - with no "destParam" - a "destPort" no jack sits on. An entry validatePatch is to judge (a malformed
// one, or one whose param and port disagree) is not touched.
bool modulationMissesNode(const juce::DynamicObject& modulation, const ModuleBase& module) {
    if (modulation.hasProperty("destParam")) {
        const auto param = modulation.getProperty("destParam");
        return param.isString() && param.toString().isNotEmpty() && module.modulationChannelForParam(param) < 0;
    }
    return modulation.hasProperty("destPort") && (int)modulation.getProperty("destPort") >= 0 &&
           !hasModulationChannel(module, (int)modulation.getProperty("destPort"));
}

// The nodes a build made as a pack Sampler in place of the stand-in the plan named, keyed by the plan's id.
std::map<juce::uint32, juce::AudioProcessorGraph::NodeID>
substitutedSamplers(const Plan& plan, const RecordingHost& recorder, const PatchIdScope& scope) {
    std::map<juce::uint32, juce::AudioProcessorGraph::NodeID> found;
    for (const auto& reservation : plan.reservations) {
        if (reservation.insertIndex != Reservation::kInstrument ||
            reservation.buildIndex >= (int)recorder.results.size())
            continue;
        if (!recorder.results[(size_t)reservation.buildIndex].substitutedSampler)
            continue;
        if (auto bound = scope.boundIds.find(reservation.id); bound != scope.boundIds.end())
            found[reservation.id] = bound->second;
    }
    return found;
}

// The model wrote the patch for the stand-in synth (say a Wavetable's detune), but the app built the recorded
// instrument as a Sampler, which has no such jack. validatePatch would refuse the whole patch for it, so a
// modulation onto such a Sampler that the Sampler cannot take is dropped here, before validation, with a note.
// Every other modulation, and every modulation onto any other node, is left for validatePatch exactly as sent.
// Returns the patch to run: `root` itself when nothing was dropped, else a copy without those entries.
juce::var dropStandInModulations(const juce::var& root, const Plan& plan, const RecordingHost& recorder,
                                 const PatchIdScope& scope, juce::AudioProcessorGraph& graph,
                                 std::vector<PlanNote>& notes) {
    const auto samplers = substitutedSamplers(plan, recorder, scope);
    const juce::var modulations = root.getProperty("modulations", {});
    const auto* list = modulations.getArray();
    if (samplers.empty() || list == nullptr)
        return root;
    juce::Array<juce::var> kept;
    for (const auto& entry : *list) {
        auto* modulation = entry.getDynamicObject();
        juce::uint32 dest = 0;
        auto sampler = samplers.end();
        if (modulation != nullptr && readPlanId(modulation->getProperty("dest"), dest))
            sampler = samplers.find(dest);
        auto* node = sampler != samplers.end() ? graph.getNodeForId(sampler->second) : nullptr;
        auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (module == nullptr || !modulationMissesNode(*modulation, *module)) {
            kept.add(entry);
            continue;
        }
        const juce::String what = modulation->hasProperty("destParam")
                                      ? modulation->getProperty("destParam").toString()
                                      : "input " + modulation->getProperty("destPort").toString();
        notes.push_back({PlanNote::Kind::Repaired,
                         "Patch: Modulation onto node " + juce::String(dest) + " names \"" + what +
                             "\", which that node cannot be modulated on.",
                         "a modulation on the stand-in's " + what + ", which the recorded instrument doesn't have"});
    }
    if (kept.size() == list->size())
        return root;
    juce::var repaired = root.clone();
    repaired.getDynamicObject()->setProperty("modulations", juce::var(kept));
    return repaired;
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

// Phase 3's preparation of the writeLane ops, on a fresh copy in every run (the preview's uuids are the scratch
// graph's, never the live one's). A "nodeId" becomes the "nodeUuid" of the node that id now denotes, so TimelineOps
// only ever sees uuids; then each lane is checked against the graph and its values clamped into the parameter's
// range (repairWriteLane). A lane whose node or parameter does not exist is dropped with a note instead of
// failing the plan. `index` is rebuilt to follow the ops that stay.
void prepareLaneOps(juce::var& envelope, const Plan& plan, const TimelineDoc& doc, juce::AudioProcessorGraph& graph,
                    const PatchIdScope& scope, const std::map<int, juce::AudioProcessorGraph::NodeID>& idMap,
                    std::vector<int>& index, std::vector<PlanNote>& notes) {
    auto* ops = envelope.getProperty("timelineOps", {}).getArray();
    if (ops == nullptr)
        return;
    juce::Array<juce::var> kept;
    std::vector<int> keptIndex;
    for (int i = 0; i < ops->size(); ++i) {
        auto* op = ops->getReference(i).getDynamicObject();
        const int originalIndex = plan.otherOpIndex[(size_t)i];
        if (op == nullptr || op->getProperty("op").toString() != "writeLane") {
            kept.add(ops->getReference(i));
            keptIndex.push_back(originalIndex);
            continue;
        }
        const juce::String where = opLabel(originalIndex, "writeLane");
        auto skip = [&](const juce::String& error, const juce::String& note) {
            notes.push_back({PlanNote::Kind::Skipped, error, note});
        };
        if (op->hasProperty("nodeId")) {
            juce::uint32 id = 0;
            readPlanId(op->getProperty("nodeId"), id); // checked by readPlan
            std::optional<juce::AudioProcessorGraph::NodeID> nodeId;
            if (auto bound = scope.boundIds.find(id); bound != scope.boundIds.end())
                nodeId = bound->second;
            else if (auto mapped = idMap.find((int)id); mapped != idMap.end() && plan.patchNodeIds.count(id) > 0)
                nodeId = mapped->second;
            auto* node = nodeId.has_value() ? graph.getNodeForId(*nodeId) : nullptr;
            if (node == nullptr) {
                skip(where + "\"nodeId\" " + juce::String(id) +
                         " names no node after the patch applied (did the patch remove it?).",
                     "an automation lane on a module the answer removed again");
                continue;
            }
            op->setProperty("nodeUuid", AIStateMapper::ensureNodeUuid(node));
            op->removeProperty("nodeId");
        }
        const auto repair = repairWriteLane(*op, doc, graph);
        if (repair.outcome == LaneRepair::Outcome::UnknownNode) {
            skip(where + "writes to node uuid \"" + op->getProperty("nodeUuid").toString() +
                     "\", which no module in the current patch has. Automate a module that exists.",
                 "an automation lane on a module that is not in the patch");
            continue;
        }
        if (repair.outcome == LaneRepair::Outcome::UnknownParam) {
            skip(where + "writes parameter \"" + op->getProperty("paramId").toString() + "\", which module \"" +
                     repair.moduleName + "\" does not have. Use one of that module's parameter ids.",
                 "an automation lane on a setting the " + repair.moduleName + " doesn't have");
            continue;
        }
        if (repair.clamped > 0)
            notes.push_back({PlanNote::Kind::Clamped,
                             {},
                             "an automation value for " + repair.parameterName + " was brought back into its " +
                                 juce::String(repair.minAllowed) + " to " + juce::String(repair.maxAllowed) +
                                 " range"});
        kept.add(ops->getReference(i));
        keptIndex.push_back(originalIndex);
    }
    envelope = envelopeOf(kept);
    index = keptIndex;
}

} // namespace

RunResult runPlan(const Plan& plan, const juce::var& root, TimelineDoc& doc, juce::AudioProcessorGraph& graph,
                  TimelineOpsHost& buildHost, bool checkStructure) {
    RunResult out;
    out.notes = plan.notes;
    auto fail = [&out](const juce::String& message) {
        out.ok = false;
        out.message = message;
        return out;
    };
    juce::StringArray preview;

    // 1. Track-creating ops, in list order.
    PatchIdScope scope;
    juce::var patchRoot = root;
    if (!plan.trackOps.isVoid()) {
        const auto before = nodeIdsOf(graph);
        RecordingHost recorder(buildHost);
        if (const auto error = runOps(plan.trackOps, plan.trackOpIndex, doc, graph, recorder, preview);
            error.isNotEmpty())
            return fail(error);
        if (const auto error = bindReservations(plan, recorder, graph, before, scope); error.isNotEmpty())
            return fail(error);
        patchRoot = dropStandInModulations(root, plan, recorder, scope, graph, out.notes);
    }

    // 2. The patch, its namespace widened by the ids phase 1 bound.
    std::map<int, juce::AudioProcessorGraph::NodeID> idMap;
    if (plan.hasPatch)
        if (const auto error = runPatch(plan, patchRoot, graph, scope, checkStructure, idMap, out); error.isNotEmpty())
            return fail(error);

    // 3. Every other op, in list order, with nodeId references resolved to uuids.
    if (!plan.otherOps.isVoid()) {
        juce::var ops = juce::JSON::parse(juce::JSON::toString(plan.otherOps));
        std::vector<int> index;
        prepareLaneOps(ops, plan, doc, graph, scope, idMap, index, out.notes);
        if (exceedsBadDetailLimit(out.notes))
            return fail(firstBadDetailError(out.notes));
        if (!out.notes.empty() && firstBadDetailError(out.notes).isNotEmpty()) {
            const bool nothingLeft = !plan.hasPatch && !plan.hasTrackOps && ops.isVoid();
            const auto* kept = ops.getProperty("timelineOps", {}).getArray();
            if (nothingLeft || (plan.hasTrackOps && (kept == nullptr || !placesNotes(*kept))))
                return fail(firstBadDetailError(out.notes));
        }
        if (!ops.isVoid())
            if (const auto error = runOps(ops, index, doc, graph, buildHost, preview); error.isNotEmpty())
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
// no channel strip, no macro. The envelope is an unwired ADSR carrying the op's envelope params, so a
// modulation that names the envelope's id resolves its source and its range-checks run on the
// real module. The doc side is the real thing (the MIDI track the build
// would add), so later ops in the plan can address the track by name.
std::optional<InstrumentTrackBuildResult>
StandInHost::addInstrumentTrack(const juce::String& name, const juce::String& instrumentType, bool poly,
                                const std::vector<InstrumentTrackInsert>& inserts, const juce::var& envelopeParams,
                                const juce::var& instrumentParams) {
    juce::ignoreUnused(poly);
    return buildStandIns(name, instrumentType, instrumentType, inserts, envelopeParams, instrumentParams);
}

// `instrumentType` is the node built; `envelopeShapeType` decides whether the track has its own ADSR, which is the
// planned stand-in's rule in the app too (a Sampler built for a Wavetable plan keeps the Wavetable's envelope).
std::optional<InstrumentTrackBuildResult>
StandInHost::buildStandIns(const juce::String& name, const juce::String& instrumentType,
                           const juce::String& envelopeShapeType, const std::vector<InstrumentTrackInsert>& inserts,
                           const juce::var& envelopeParams, const juce::var& instrumentParams) {
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
    result.instrumentUuid = addStandIn(instrumentType, instrumentParams);
    if (!TimelineOps::instrumentOwnsItsEnvelope(envelopeShapeType))
        result.envelopeUuid = addStandIn("ADSR", envelopeParams);
    for (const auto& insert : inserts)
        result.insertUuids.push_back(addStandIn(insert.type, insert.params));
    return result;
}

// A core sound the pack provides is built as a Sampler by the app (MainComponentTimelineOpsHost), so the preview
// builds the same type: a patch that passes here is judged on the node the apply will have, never on the stand-in.
// Anything the app builds as the stand-in (pack missing, a drum kit, a sound that does not match) builds as one here.
std::optional<InstrumentTrackBuildResult>
StandInHost::addInstrumentTrackWithSound(const juce::String& name, const juce::String& instrumentType, bool poly,
                                         const std::vector<InstrumentTrackInsert>& inserts,
                                         const juce::var& envelopeParams, const juce::var& instrumentParams,
                                         const juce::String& sound) {
    const auto* core = synth::soundpacks::findCoreSound(sound);
    if (core == nullptr || core->drumKit || instrumentType != core->standIn ||
        !synth::soundpacks::coreSoundInstalled(*core))
        return addInstrumentTrack(name, instrumentType, poly, inserts, envelopeParams, instrumentParams);
    auto sampler = AIStateMapper::createModule("Sampler");
    if (sampler == nullptr || !synth::soundpacks::loadCoreSoundIntoSampler(*sampler, *core))
        return addInstrumentTrack(name, instrumentType, poly, inserts, envelopeParams, instrumentParams);
    auto result = buildStandIns(name, "Sampler", instrumentType, inserts, envelopeParams, {});
    if (result.has_value())
        result->substitutedSampler = true;
    return result;
}

// A preview validates a setTempo op but never applies it, so there is nothing to set.
bool StandInHost::setTempo(double bpm) {
    juce::ignoreUnused(bpm);
    return true;
}

bool StandInHost::recordBatch(const std::function<void()>& mutation) {
    mutation();
    return true;
}

} // namespace synth::projectedit
