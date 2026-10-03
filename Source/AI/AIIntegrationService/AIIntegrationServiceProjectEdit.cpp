// The edit plan ("project edit"): one response that carries a patch AND a sibling "timelineOps"
// list, previewed on a scratch copy and applied as ONE undo step in a fixed order - track-creating
// ops, then the patch (merge, its ids widened by what those ops built), then every other op. Also
// the way to ask for one: sendProjectMessage, hosted (project.generate) or local (sendPrompt with
// the same fields). Reading and running a plan lives in AIIntegrationServiceProjectEditRun.cpp; the
// whole mechanism is documented in docs/ai/timeline-ops.md#one-edit-plan.
#include "AI/PatchDiff.h"
#include "AI/PatchEval.h"
#include "AIIntegrationService.h"
#include "AIIntegrationServiceInternal.h"

#include <algorithm>

namespace synth {

namespace {

std::set<juce::uint32> liveNodeUids(const juce::AudioProcessorGraph& graph) {
    std::set<juce::uint32> uids;
    for (auto* node : graph.getNodes())
        uids.insert(node->nodeID.uid);
    return uids;
}

// Every node in `graph` that `before` did not hold, in creation order (uids only grow).
std::vector<juce::AudioProcessorGraph::NodeID> nodesCreatedSince(const juce::AudioProcessorGraph& graph,
                                                                 const std::set<juce::uint32>& before) {
    std::vector<juce::AudioProcessorGraph::NodeID> created;
    for (auto* node : graph.getNodes())
        if (before.count(node->nodeID.uid) == 0)
            created.push_back(node->nodeID);
    std::sort(created.begin(), created.end(), [](auto a, auto b) { return a.uid < b.uid; });
    return created;
}

juce::String countText(int n, const juce::String& singular, const juce::String& plural) {
    return juce::String(n) + " " + (n == 1 ? singular : plural);
}

// The patch phase in one sentence, from the same before/after snapshots the patch card diffs
// (PatchDiff.h): a merge as what it changes, a replace as what the new patch contains.
juce::String describePatchPhase(const projectedit::RunResult& run) {
    if (run.patchBefore.isVoid())
        return {};
    if (!run.merge) {
        const auto summary = summarizePatch(run.patchAfter);
        return "Replaces the patch with " + countText((int)summary.nodeTypes.size(), "module", "modules") + " and " +
               countText(summary.connectionCount, "connection", "connections");
    }
    int counts[7] = {};
    for (const auto& change : computeDiff(run.patchBefore, run.patchAfter))
        ++counts[(int)change.kind];
    using Kind = PatchChange::Kind;
    juce::StringArray parts;
    auto add = [&](Kind kind, const char* verb, const char* singular, const char* plural) {
        if (const int n = counts[(int)kind]; n > 0)
            parts.add(juce::String(verb) + " " + countText(n, singular, plural));
    };
    add(Kind::NodeAdded, "adds", "module", "modules");
    add(Kind::NodeRemoved, "removes", "module", "modules");
    add(Kind::ParamChanged, "changes", "parameter", "parameters");
    add(Kind::ConnectionAdded, "adds", "connection", "connections");
    add(Kind::ConnectionRemoved, "removes", "connection", "connections");
    add(Kind::ModulationAdded, "adds", "modulation", "modulations");
    add(Kind::ModulationRemoved, "removes", "modulation", "modulations");
    return parts.isEmpty() ? juce::String("Merges a patch that changes nothing")
                           : "Merges a patch that " + parts.joinIntoString(", ");
}

ProjectEditResult toResult(const projectedit::RunResult& run) {
    ProjectEditResult result;
    result.ok = run.ok;
    result.message = run.message;
    result.merge = run.merge;
    result.patchBefore = run.patchBefore;
    result.patchAfter = run.patchAfter;
    if (!run.ok)
        return result;
    juce::StringArray sentences;
    sentences.add(describePatchPhase(run));
    if (run.opsPreview.isNotEmpty())
        sentences.add(run.opsPreview.substring(0, 1).toUpperCase() + run.opsPreview.substring(1));
    sentences.removeEmptyStrings();
    result.previewText = sentences.joinIntoString(". ") + ".";
    for (const auto& sentence : sentences)
        result.previewLines.add(sentence + ".");
    return result;
}

ProjectEditResult rejected(const juce::String& message) {
    ProjectEditResult result;
    result.ok = false;
    result.message = message;
    return result;
}

} // namespace

// The preview mutates nothing live: the live graph is trusted-replayed into a scratch (exactly as
// computePatchPreview does), the doc is copied through its own toVar/fromVar, and the SAME
// runPlan the apply uses runs all three phases against the copies. Track builds go to a stand-in
// host (StandInHost: the doc track plus unwired nodes of the right types), because the graph side
// of an instrument track cannot be dry-run; every other check is the real one.
ProjectEditResult AIIntegrationService::previewProjectEdit(const juce::var& root) const {
    projectedit::Plan plan;
    if (const auto error = projectedit::readPlan(root, liveNodeUids(audioGraph), plan); error.isNotEmpty())
        return rejected(error);
    const bool carriesOps = !plan.trackOps.isVoid() || !plan.otherOps.isVoid();
    if (carriesOps && timelineDoc == nullptr)
        return rejected("This build has no timeline wired in, so timeline changes cannot be checked or applied.");
    if (plan.buildsInstrumentTracks && timelineOpsHost == nullptr)
        return rejected("This build cannot create instrument tracks from here.");

    TimelineDoc scratchDoc;
    if (timelineDoc != nullptr && !scratchDoc.fromVar(timelineDoc->toVar()))
        return rejected("The timeline could not be copied for validation, so nothing was checked or applied.");
    juce::AudioProcessorGraph scratchGraph;
    prepareGraphForPatchEval(scratchGraph);
    replayLiveGraphTrusted(scratchGraph);

    projectedit::StandInHost standIn(scratchDoc, scratchGraph);
    return toResult(projectedit::runPlan(plan, root, scratchDoc, scratchGraph, standIn, /*checkStructure=*/true));
}

// Preview first (nothing live is touched by a rejection), then all three phases inside ONE
// host->recordBatch: the graph + timeline + macro transaction every instrument-track flow uses, so
// one Cmd+Z reverts the tracks, the patch and the notes and lanes together. Still inside it, the host
// places every new node the patch left without a position (placeNewModules,
// docs/ai/timeline-ops.md#where-things-land), so where things land is part of that one step too.
//
// Listeners: aiPatchAboutToApply fires before the batch (the editor detaches its module
// components, so nothing holds a processor the patch phase frees) and aiPatchApplied after it, on
// success and on failure alike, because the backstop below rebuilds the graph too. On undo and redo
// the transaction's own snapshot actions keep the same invariant (docs/ai/engine.md): their
// pre-restore hook detaches every module component before a processor is freed and their
// post-restore hook refreshes the canvas, as for every other graph-changing track flow.
//
// Backstop: a phase failing here after the preview passed is unreachable by construction for a
// doc-only op and a patch (the preview ran the same code on identical copies). It is reachable
// only through a host build failing, whose graph side the preview could not dry-run. Then the doc
// is restored from its pre-batch toVar() and the graph from a pre-batch trusted graphToJSON() replay,
// and the failure is returned. The macro set is the host's and is not restored here (the real host
// removes what a failed build made; an EARLIER successful build in the same plan keeps its macro).
ProjectEditResult AIIntegrationService::applyProjectEdit(const juce::var& root) {
    TimelineDoc* doc = timelineOpsHost != nullptr ? timelineOpsHost->editableTimelineDoc() : nullptr;
    if (doc == nullptr)
        return applyPatchOnlyPlan(root);

    const auto preview = previewProjectEdit(root);
    if (!preview.ok)
        return preview;
    projectedit::Plan plan;
    const auto uidsBefore = liveNodeUids(audioGraph);
    if (const auto error = projectedit::readPlan(root, uidsBefore, plan); error.isNotEmpty())
        return rejected(error);

    juce::WeakReference<AIIntegrationService> weakThis(this);
    listeners.call([](Listener& l) { l.aiPatchAboutToApply(); });

    const juce::var docBefore = doc->toVar();
    const juce::var graphBefore = AIStateMapper::graphToJSON(audioGraph);
    projectedit::RunResult run;
    const bool pushed = timelineOpsHost->recordBatch([&] {
        run = projectedit::runPlan(plan, root, *doc, audioGraph, *timelineOpsHost, /*checkStructure=*/false);
        if (run.ok) {
            timelineOpsHost->placeNewModules(nodesCreatedSince(audioGraph, uidsBefore));
            return;
        }
        doc->fromVar(docBefore);
        AIStateMapper::applyJSONToGraph(graphBefore, audioGraph, /*clearExisting=*/true, /*trusted=*/true);
    });

    if (auto* self = weakThis.get())
        self->listeners.call([](Listener& l) { l.aiPatchApplied(); });

    juce::Logger::writeToLog(juce::String("applyProjectEdit ") + (run.ok ? "applied" : "rejected: " + run.message));
    if (!run.ok)
        return rejected(run.message);
    auto result = preview;
    result.message = pushed ? "Applied the edit plan as one undo step."
                            : "The project already matched this edit plan, so nothing changed.";
    return result;
}

// No host means no timeline to write and no batch to record into (a plugin build, or a test without
// a timeline). A plan with no timeline ops is then just a patch: preview it the same way, then hand
// it to applyPatch() in the mode the preview settled on, which is one undo step on the service's own
// undo manager. A plan that does carry ops is refused, as before.
ProjectEditResult AIIntegrationService::applyPatchOnlyPlan(const juce::var& root) {
    if (auto* ops = root.getProperty("timelineOps", {}).getArray(); ops != nullptr && !ops->isEmpty())
        return rejected("Edit plans cannot be applied from here.");
    auto preview = previewProjectEdit(root);
    if (!preview.ok)
        return preview;
    if (!applyPatch(juce::JSON::toString(root), preview.merge))
        return rejected(lastPatchError.isNotEmpty() ? lastPatchError : juce::String("The patch could not be applied."));
    preview.message = "Applied the patch as one undo step.";
    return preview;
}

// -- asking for one -----------------------------------------------------------------------------

// The timeline.generate body (buildArrangeRequestBody: userPrompt, arrangementContext, paramTargets,
// availableTracks) plus "currentPatch": the same stripped graph JSON the patch path sends the model,
// as an object, omitted when the graph has no nodes (project.generate then says "Current patch is
// empty."). productName is the provider's to add, as for every capability.
juce::var AIIntegrationService::buildProjectRequestBody(const juce::String& text) const {
    juce::var body = buildArrangeRequestBody(text);
    const juce::var patch = buildStrippedPatchJson();
    if (auto* nodes = patch.getProperty("nodes", {}).getArray(); nodes != nullptr && !nodes->isEmpty())
        body.getDynamicObject()->setProperty("currentPatch", patch);
    return body;
}

// The local transport's message, composed from the SAME body the hosted request sends and in the
// SAME section order project.generate's buildProjectUserMessage uses (synth-platform
// project-generate/capability.ts): the patch (or "Current patch is empty."), the arrangement when
// non-empty, the tracks, the targets, the prompt. The trailing line stands in for the dedicated
// system prompt the server swaps in; the response schema enforces the shape regardless.
juce::String AIIntegrationService::buildProjectAugmentedContent(const juce::String& text) const {
    const juce::var body = buildProjectRequestBody(text);
    juce::String content;
    if (body.hasProperty("currentPatch"))
        content << "Current patch state:\n```json\n" << juce::JSON::toString(body["currentPatch"]) << "\n```\n\n";
    else
        content << "Current patch is empty.\n\n";
    const juce::String arrangement = body["arrangementContext"].toString();
    if (arrangement.trim().isNotEmpty())
        content << "Arrangement context:\n" << arrangement << "\n\n";
    content << "Project tracks:\n```json\n" << juce::JSON::toString(body["availableTracks"]) << "\n```\n\n";
    content << "Automation targets:\n```json\n" << juce::JSON::toString(body["paramTargets"]) << "\n```\n\n";
    content << text << "\n\n";
    content << "Respond ONLY with one JSON object: the patch (\"nodes\", \"connections\", and \"mode\", \"remove\", "
               "\"modulations\" as needed) plus an optional \"timelineOps\" list. No prose.";
    return content;
}

// ONE intent, two transports: hosted -> the project.generate capability
// with buildProjectRequestBody; local -> sendPrompt with the same fields composed into the message
// and getPatchSchemaWithTimelineOps() (which offers "nodeId" and "destParam") as the contract. Both
// answer with a root that previewProjectEdit/applyProjectEdit consume. Same history contract as
// sendMessage: the raw text is the user turn; the composed context exists only on the wire.
AIProvider::RequestId AIIntegrationService::sendProjectMessage(const juce::String& text,
                                                               AIProvider::CompletionCallback callback) {
    chatHistory.push_back({"user", text});
    trimHistory();

    if (!provider) {
        if (callback) {
            AIProvider::AIResponse response;
            response.success = false;
            response.error.kind = AIProvider::AIErrorKind::Schema; // no provider configured — client precondition
            response.error.message = "Error: No AI provider selected.";
            callback(response);
        }
        return {};
    }

    if (provider->isHosted())
        return provider->sendCapabilityRequest("project.generate", buildProjectRequestBody(text),
                                               wrapCompletionForHistory(std::move(callback)));

    std::vector<AIProvider::Message> request = chatHistory;
    if (!request.empty())
        request.back().content = buildProjectAugmentedContent(text);
    return provider->sendPrompt(request, wrapCompletionForHistory(std::move(callback)),
                                AIStateMapper::getPatchSchemaWithTimelineOps());
}

} // namespace synth
