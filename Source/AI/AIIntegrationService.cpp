#include "AIIntegrationService.h"
#include "../AppUndoManager.h"
#include "../Branding.h"
#include "PatchEval.h"
#include <algorithm>

namespace synth {

AIIntegrationService::AIIntegrationService(juce::AudioProcessorGraph& graph, AppUndoManager* undoManager)
    : audioGraph(graph)
    , undoManager(undoManager) {
    initSystemPrompt();
}

AIIntegrationService::~AIIntegrationService() {}

void AIIntegrationService::setProvider(std::unique_ptr<AIProvider> newProvider) {
    provider = std::move(newProvider);

    // Re-push contract (mirrors AIChatComponent::refreshModels(), see docs/AI_Engine.md "Model
    // Discovery Ordering Contract"): a caller may have called setAuthToken() before a provider
    // existed at all, so the value must be forwarded to whatever provider is installed now.
    if (provider && currentAuthToken.isNotEmpty())
        provider->setAuthToken(currentAuthToken);

    // Same re-push shape for the conversation id (see setConversationId()'s doc comment).
    if (provider && currentConversationId.isNotEmpty())
        provider->setConversationId(currentConversationId);
}

void AIIntegrationService::setAuthToken(const juce::String& token) {
    currentAuthToken = token;
    if (provider)
        provider->setAuthToken(currentAuthToken);
}

void AIIntegrationService::setConversationId(const juce::String& id) {
    currentConversationId = id;
    if (provider)
        provider->setConversationId(currentConversationId);
}

AIProvider::RequestId AIIntegrationService::sendMessage(const juce::String& text,
                                                        AIProvider::CompletionCallback callback,
                                                        bool useStructuredOutput) {
    // The stored history always keeps the user's original text. Patch context is ephemeral:
    // it is spliced into the outgoing request only, never retained in chatHistory.
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

    std::vector<AIProvider::Message> request = chatHistory;
    if (useStructuredOutput && !request.empty())
        request.back().content = buildPatchAugmentedContent(text);

    // With the timeline tools on, the output contract grows an OPTIONAL `timelineOps` array —
    // patch-only responses stay exactly as valid, so nothing changes for pure patch asks.
    auto schema = useStructuredOutput ? (timelineToolsActive() ? AIStateMapper::getPatchSchemaWithTimelineOps()
                                                               : AIStateMapper::getPatchSchema())
                                      : juce::var();

    return provider->sendPrompt(request, wrapCompletionForHistory(std::move(callback)), schema);
}

AIProvider::CompletionCallback AIIntegrationService::wrapCompletionForHistory(AIProvider::CompletionCallback callback) {
    auto weakThis = juce::WeakReference<AIIntegrationService>(this);
    return [weakThis, callback](const AIProvider::AIResponse& response) {
        if (weakThis.get() == nullptr)
            return; // Service was destroyed

        auto* self = weakThis.get();

        // A cancelled request produced no assistant turn. Recording one would put words in the
        // model's mouth that the user never saw and — because chatHistory is replayed as
        // context — feed that invention back on every later message. The user's own turn
        // stays: they did say it, and it is still on screen.
        if (response.error.kind != AIProvider::AIErrorKind::Cancelled && response.success) {
            self->chatHistory.push_back({"assistant", response.content});
            self->trimHistory();

            // Capture a persisted-conversation id from a Pro-plan hosted backend and re-push
            // it so the NEXT call in this session continues the same server-side thread. A
            // free-plan response carries no id (see AIProvider::AIResponse::conversationId's
            // doc comment) — this is a no-op for that case, not a special branch.
            //
            // Same thread-context as the chatHistory mutation just above (this callback can
            // run on a provider worker thread in test/forceSynchronous mode, or via
            // MessageManager::callAsync in production): safe because no caller starts request
            // N+1 while N's callback is still landing — AIChatComponent disables Send for the
            // whole in-flight window (isWaitingForResponse), so there is never a concurrent
            // processRequest() reading currentConversationId while this write happens.
            if (response.conversationId.isNotEmpty())
                self->setConversationId(response.conversationId);
        }

        if (callback) {
            callback(response);
        }
    };
}

void AIIntegrationService::cancelRequest(AIProvider::RequestId requestId) {
    if (provider)
        provider->cancel(requestId);
}

juce::String AIIntegrationService::buildPatchAugmentedContent(const juce::String& text) {
    // graphToJSON builds a fresh DynamicObject tree on every call (not shared with the live graph
    // or any other caller), so stripping "state" from these nodes in place cannot mutate anything
    // else. "state" is trusted-path-only (a Hosted Plugin's opaque blob, a Sampler's disk path) —
    // the model can never author it, so it is pure leakage and token waste over the wire.
    juce::var graphJson = AIStateMapper::graphToJSON(audioGraph);
    juce::String patchSection;
    if (auto* obj = graphJson.getDynamicObject()) {
        if (auto* nodeArr = obj->getProperty("nodes").getArray()) {
            for (auto& nodeVar : *nodeArr) {
                if (auto* nodeObj = nodeVar.getDynamicObject())
                    nodeObj->removeProperty("state");
            }
            if (!nodeArr->isEmpty()) {
                patchSection = "Current patch state:\n```json\n" + juce::JSON::toString(graphJson) + "\n```";
            }
        }
    }

    // The timeline sibling of the patch section above, added only when there is an arrangement to
    // report. See ArrangementContext::summarize() (Source/Timeline/ArrangementContext.h) for the
    // security model (read-path only; name-only file references; no plugin identifiers).
    juce::String arrangementSection;
#if SYNTH_ENABLE_TIMELINE
    if (timelineDoc != nullptr && transportService != nullptr && !timelineDoc->isEmpty()) {
        const juce::String summary =
            ArrangementContext::summarize(*timelineDoc, audioGraph, transportService->getPositionSnapshot());
        if (summary.isNotEmpty())
            arrangementSection = "## Arrangement\n" + summary;
    }
#endif

    juce::String content;
    if (patchSection.isNotEmpty())
        content << patchSection << "\n\n";
    else
        // Structured output was requested but the live graph has no nodes. Silently falling back
        // to bare `text` gave the model no signal either way about whether a patch already exists,
        // so a fresh-session "create a bass patch" request would come back asking the user to
        // paste their (nonexistent) current patch. Say explicitly that the canvas is empty.
        content << "Current patch is empty.\n\n";
    if (arrangementSection.isNotEmpty())
        content << arrangementSection << "\n\n";
#if SYNTH_ENABLE_TIMELINE
    if (timelineToolsActive()) {
        const juce::String targets = buildAutomationTargetsSection();
        if (targets.isNotEmpty())
            content << targets << "\n\n";
    }
#endif
    content << "User request: " << text;
    return content;
}

#if SYNTH_ENABLE_TIMELINE
std::vector<AIIntegrationService::AutomationTargetInfo> AIIntegrationService::enumerateAutomationTargets() const {
    // Float parameters only — that is what an automation lane drives — and only nodes that carry
    // a uuid (a node without one is not addressable by writeLane at all). See the header doc
    // comment: this ONE enumeration feeds both the local model's text section and the remote
    // timeline.generate body, so the criteria live here and nowhere else.
    std::vector<AutomationTargetInfo> targets;
    for (auto* node : audioGraph.getNodes()) {
        if (node == nullptr || node->getProcessor() == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isEmpty())
            continue;

        for (auto* p : node->getProcessor()->getParameters()) {
            auto* f = dynamic_cast<juce::AudioParameterFloat*>(p);
            if (f == nullptr)
                continue;
            const auto& range = f->getNormalisableRange();
            // getDefaultValue() is normalised (AudioProcessorParameter contract) and only public
            // on the BASE class — AudioParameterFloat re-privatises it — hence the call through
            // `p`. The model needs the default in the parameter's own units, same as min/max.
            targets.push_back({uuid, node->getProcessor()->getName(), f->paramID, range.start, range.end,
                               f->convertFrom0to1(p->getDefaultValue())});
        }
    }
    return targets;
}

juce::String AIIntegrationService::buildAutomationTargetsSection() const {
    // One line per addressable node: `- "<uuid>" <Display Name>: <paramId> [min..max], ...`.
    // Bounded like the arrangement summary: whole LINES are dropped from the tail past the cap,
    // with a marker, so a huge patch can't flood the request.
    constexpr int kMaxChars = 2000;

    const auto targets = enumerateAutomationTargets();

    juce::StringArray lines;
    int dropped = 0;
    int usedChars = 0;
    // Group consecutive targets by node: enumerateAutomationTargets() emits in graph order, and
    // a uuid is per-node identity, so one node's parameters are always one contiguous run.
    for (size_t i = 0; i < targets.size();) {
        const juce::String uuid = targets[i].nodeUuid;
        const juce::String nodeName = targets[i].nodeName;
        juce::StringArray params;
        for (; i < targets.size() && targets[i].nodeUuid == uuid; ++i)
            params.add(targets[i].paramId + " [" + juce::String(targets[i].min, 3) + ".." +
                       juce::String(targets[i].max, 3) + "]");

        const juce::String line = "- \"" + uuid + "\" " + nodeName + ": " + params.joinIntoString(", ");
        if (usedChars + line.length() > kMaxChars) {
            ++dropped;
            continue;
        }
        usedChars += line.length();
        lines.add(line);
    }

    if (lines.isEmpty())
        return {};
    juce::String section =
        "## Automation targets (for writeLane: nodeUuid + paramId + raw value range)\n" + lines.joinIntoString("\n");
    if (dropped > 0)
        section << "\n... [+" << dropped << " more nodes]";
    return section;
}

juce::var AIIntegrationService::buildArrangeRequestBody(const juce::String& text) const {
    juce::DynamicObject::Ptr body = new juce::DynamicObject();

    // The RAW user text — see the header doc comment for why this is never pre-wrapped the way
    // buildPatchAugmentedContent() wraps the patch path's last message.
    body->setProperty("userPrompt", text);

    // Required key, allowed empty (the schema's own words: "a caller with nothing to say should
    // say so explicitly rather than have the field quietly go missing").
    juce::String arrangement;
    if (timelineDoc != nullptr && transportService != nullptr && !timelineDoc->isEmpty())
        arrangement = ArrangementContext::summarize(*timelineDoc, audioGraph, transportService->getPositionSnapshot());
    body->setProperty("arrangementContext", arrangement);

    juce::Array<juce::var> paramTargets;
    for (const auto& t : enumerateAutomationTargets()) {
        if (paramTargets.size() >= kMaxRemoteParamTargets)
            break; // server cap — see kMaxRemoteParamTargets' doc comment
        juce::DynamicObject::Ptr target = new juce::DynamicObject();
        target->setProperty("nodeUuid", t.nodeUuid);
        target->setProperty("nodeName", t.nodeName);
        target->setProperty("paramId", t.paramId);
        target->setProperty("min", t.min);
        target->setProperty("max", t.max);
        target->setProperty("default", t.defaultValue);
        paramTargets.add(juce::var(target.get()));
    }
    body->setProperty("paramTargets", paramTargets);

    // One {name, kind, index} per live track, in doc order — index is the track's position in
    // that order, which is exactly how a placeClips/writeLane op addresses it. TimelineDoc's
    // kMaxTracks equals the server's TIMELINE_OPS_MAX_TRACKS (256), so no cap is needed here:
    // a doc that exists cannot describe more tracks than the schema accepts.
    juce::Array<juce::var> availableTracks;
    if (timelineDoc != nullptr) {
        int index = 0;
        for (const auto& track : timelineDoc->getTracks()) {
            juce::DynamicObject::Ptr trackObj = new juce::DynamicObject();
            trackObj->setProperty("name", track.name);
            trackObj->setProperty("kind", track.kind == TrackKind::Midi    ? "midi"
                                          : track.kind == TrackKind::Audio ? "audio"
                                                                           : "automation");
            trackObj->setProperty("index", index++);
            availableTracks.add(juce::var(trackObj.get()));
        }
    }
    body->setProperty("availableTracks", availableTracks);

    return juce::var(body.get());
}

AIProvider::RequestId AIIntegrationService::sendArrangeMessage(const juce::String& text,
                                                               AIProvider::CompletionCallback callback) {
    // Same history contract as sendMessage(): the stored history keeps the user's original text;
    // the structured request fields are ephemeral, built for the wire and never retained.
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

    // One intent, two transports (the local/remote parity rule): a hosted provider has a
    // dedicated capability whose input is the structured fields; a local provider gets the SAME
    // fields composed into the outgoing message (buildArrangeAugmentedContent mirrors the
    // server's own section layout) with an envelope-only response schema. Both providers answer
    // with the identical timelineOps envelope, so the downstream extract → validate → card flow
    // cannot tell them apart — the transport difference is absorbed HERE, never surfaced as a
    // behaviour difference.
    if (provider->isHosted())
        return provider->sendCapabilityRequest("timeline.generate", buildArrangeRequestBody(text),
                                               wrapCompletionForHistory(std::move(callback)));

    // Same splice-into-the-request-copy shape as sendMessage(): chatHistory keeps the user's raw
    // text; the composed arrange context exists only on the wire.
    std::vector<AIProvider::Message> request = chatHistory;
    if (!request.empty())
        request.back().content = buildArrangeAugmentedContent(text);

    return provider->sendPrompt(request, wrapCompletionForHistory(std::move(callback)),
                                AIStateMapper::getTimelineOpsEnvelopeSchema());
}

juce::String AIIntegrationService::buildArrangeAugmentedContent(const juce::String& text) const {
    // Composed from the SAME fields the hosted request sends (buildArrangeRequestBody), in the
    // SAME section order the server's buildTimelineUserMessage uses (synth-platform
    // timeline-generate/capability.ts: arrangement context when non-empty, then tracks, then
    // targets, then the prompt) — one source of truth for what an arrange request tells the
    // model, however it travels. The one addition is the trailing instruction: the server swaps
    // in a dedicated arrange system prompt, which a mid-conversation local request cannot do, so
    // that steering rides in the message instead (the envelope-only schema enforces the shape
    // regardless; the line is for answer quality, not for safety).
    const juce::var body = buildArrangeRequestBody(text);

    juce::String content;
    const juce::String arrangement = body["arrangementContext"].toString();
    if (arrangement.trim().isNotEmpty())
        content << "Arrangement context:\n" << arrangement << "\n\n";
    content << "Project tracks:\n```json\n" << juce::JSON::toString(body["availableTracks"]) << "\n```\n\n";
    content << "Automation targets:\n```json\n" << juce::JSON::toString(body["paramTargets"]) << "\n```\n\n";
    content << text << "\n\n";
    content << "Respond ONLY with a JSON object containing a \"timelineOps\" array. No patch, no prose.";
    return content;
}
#endif

void AIIntegrationService::trimHistory() {
    if (chatHistory.empty())
        return;

    size_t start = (chatHistory.front().role == "system") ? 1 : 0;
    size_t conversationSize = chatHistory.size() - start;
    size_t maxConversation = static_cast<size_t>(kMaxHistoryTurns) * 2;

    if (conversationSize <= maxConversation)
        return;

    size_t excess = conversationSize - maxConversation;
    size_t pairsToRemove = (excess + 1) / 2; // round up to whole pairs
    size_t messagesToRemove = std::min(pairsToRemove * 2, conversationSize);

    chatHistory.erase(chatHistory.begin() + static_cast<long>(start),
                      chatHistory.begin() + static_cast<long>(start + messagesToRemove));
}

void AIIntegrationService::replayLiveGraphTrusted(juce::AudioProcessorGraph& scratch) const {
    juce::var currentState = AIStateMapper::graphToJSON(audioGraph);
    AIStateMapper::applyJSONToGraph(currentState, scratch, /*clearExisting=*/true, /*trusted=*/true);
}

bool AIIntegrationService::computePatchPreview(const juce::String& jsonString, bool mergeMode, juce::var& before,
                                               juce::var& after) {
    juce::String extractedJson = extractJsonFromResponse(jsonString);
    juce::var json = juce::JSON::parse(extractedJson);
    if (!json.isObject())
        return false;

    bool clearExisting = !mergeMode;

    // Mirror applyPatch()'s mode-less-patch repair (see the identical check there): a patch with
    // no stated "mode" that only validates as a merge is applied as a merge, so the previewed
    // diff must use the same clearExisting the real apply will use, or the user approves one
    // diff and gets a different one.
    if (clearExisting && !hasExplicitMode(json)) {
        auto asReplace = AIStateMapper::validatePatch(json, audioGraph, /*clearExisting=*/true, /*trusted=*/false);
        if (!asReplace.ok) {
            auto asMerge = AIStateMapper::validatePatch(json, audioGraph, /*clearExisting=*/false, /*trusted=*/false);
            if (asMerge.ok)
                clearExisting = false;
        }
    }

    juce::AudioProcessorGraph scratch;
    synth::prepareGraphForPatchEval(scratch);
    if (!clearExisting)
        replayLiveGraphTrusted(scratch);

    // Both "before" and "after" are read off graphToJSON() — never the raw patch JSON — so the
    // diff sees exactly what applying the patch would actually do to the graph (auto-wiring,
    // deletions, value rescaling included). For merge mode, "before" is the scratch's
    // just-replayed state rather than a second graphToJSON(audioGraph) call: both travel the same
    // param round-trip (denormalize -> setValueNotifyingHost -> renormalize, including any
    // snapToLegalValue on a skewed/int range), so a no-op patch can't show a phantom param change
    // from replay-only rounding that the live graph's own snapshot never went through.
    before = clearExisting ? AIStateMapper::graphToJSON(audioGraph) : AIStateMapper::graphToJSON(scratch);

    bool applied = AIStateMapper::applyJSONToGraph(json, scratch, clearExisting, /*trusted=*/false);
    after = AIStateMapper::graphToJSON(scratch);
    return applied;
}

bool AIIntegrationService::hasExplicitMode(const juce::var& json) {
    auto* obj = json.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("mode"))
        return false;
    // An empty or non-string "mode" is no statement of intent, so it must not block the repair.
    return obj->getProperty("mode").toString().isNotEmpty();
}

bool AIIntegrationService::applyPatch(const juce::String& jsonString, bool mergeMode) {
    juce::String extractedJson = extractJsonFromResponse(jsonString);
    juce::var json = juce::JSON::parse(extractedJson);
    bool clearExisting = !mergeMode;

    // Validate BEFORE touching any listener or the undo stack — juce::JSON::parse returns a void var for
    // unparseable input, which validatePatch rejects (NotAnObject) along with any other structurally invalid
    // patch. A failure here means the graph is never mutated, so listeners must not be told a patch is about
    // to apply, and no no-op entry may be left on the undo stack.
    lastPatchError.clear();
    lastPatchErrorCode = PatchValidationError::None;

    lastPatchModeRepaired = false;

    auto validation = AIStateMapper::validatePatch(json, audioGraph, clearExisting, /*trusted=*/false);

    // Narrow, non-destructive repair: the patch is a merge delta that we were about to apply as a
    // replace. This happens when the model omits "mode" and the caller's guess (in the UI, a
    // keyword heuristic over the user's wording) goes the other way; the patch then references
    // ids that exist only in the live graph and is rejected as *UnknownNode.
    //
    // Deliberately one-directional. Re-reading a rejected patch as a *merge* can only preserve
    // nodes the user already had; the reverse — quietly turning a merge into a replace — would
    // wipe their patch, so it is never attempted. Nothing about the patch's content is altered:
    // validation itself is the arbiter, and this only runs when the model expressed no mode.
    if (!validation.ok && clearExisting && !hasExplicitMode(json)) {
        const auto asMerge = AIStateMapper::validatePatch(json, audioGraph, /*clearExisting=*/false,
                                                          /*trusted=*/false);
        if (asMerge.ok) {
            juce::Logger::writeToLog("applyPatch: reinterpreting mode-less patch as a merge (as a replace it was "
                                     "rejected: " +
                                     validation.message + ")");
            clearExisting = false;
            mergeMode = true;
            lastPatchModeRepaired = true;
            validation = asMerge;
        }
    }

    if (!validation.ok) {
        lastPatchErrorCode = validation.error;
        // One log per rejected patch — user-click frequency, so it does not violate the no-high-frequency
        // logging rule. In Debug builds this reaches AIChatComponent's console panel.
        lastPatchError = validation.message.isNotEmpty() ? validation.message : "Patch failed validation.";
        juce::Logger::writeToLog("applyPatch rejected (" + juce::String(mergeMode ? "merge" : "replace") +
                                 "): " + lastPatchError);
        return false;
    }

    // Structural gate: a patch can be schema-valid and still be useless — silent (nothing wired to
    // Audio Output) or dangling (an oscillator nobody connected). This is the same bar
    // Tools/AIEvalHarness measures every model against (PatchEval.h); enforcing it live gives the
    // model a chance to self-correct via the existing retry mechanism instead of silently handing
    // the user a patch that produces no sound. Checked on a scratch graph, never the live one, so a
    // rejection here never fires a listener notification or touches the undo stack (same
    // "validate BEFORE touching anything" contract as the schema check above).
    //
    // allParamsInRange is deliberately not gated on: PatchEval's own doc comment notes it's already
    // guaranteed by NormalisableRange clamping on every write path.
    {
        juce::AudioProcessorGraph scratch;
        synth::prepareGraphForPatchEval(scratch);

        bool beforeOk = true;
        if (!clearExisting) {
            // Merge mode: gate on REGRESSION, not absolute state. A delta is only responsible for
            // what it changes — an already-incomplete canvas (e.g. no Oscillator yet) merging in an
            // unrelated edit must not be rejected for a pre-existing gap the edit didn't cause.
            // Replaying the live graph's current state as trusted mirrors exactly what undo/redo's
            // own snapshot-restore does (see the "Preserve node identity" comment in
            // AIStateMapper::applyJSONToGraph), so ids line up with what the candidate patch
            // references. Shared with computePatchPreview() — see replayLiveGraphTrusted().
            replayLiveGraphTrusted(scratch);
            const auto before = synth::evaluatePatch(scratch);
            beforeOk = before.hasAudioOutput && before.sourceReachesOutput;
        }

        AIStateMapper::applyJSONToGraph(json, scratch, clearExisting, /*trusted=*/false);
        const auto after = synth::evaluatePatch(scratch);
        const bool afterOk = after.hasAudioOutput && after.sourceReachesOutput;

        // Replace mode has no "before" to regress from — clearExisting leaves beforeOk at its
        // default true, so a from-scratch patch is always held to the unconditional bar.
        if (beforeOk && !afterOk) {
            lastPatchError = after.detail.isNotEmpty() ? after.detail : "patch produces no usable signal path";
            juce::Logger::writeToLog("applyPatch rejected (" + juce::String(mergeMode ? "merge" : "replace") +
                                     ", structural): " + lastPatchError);
            return false;
        }
    }

    // Notify listeners to detach graph-referencing UI BEFORE the graph is rebuilt (avoids a use-after-free
    // where a ScopeComponent timer reads a freed VisualBuffer once applyJSONToGraph clears old processors).
    // Undo and redo rebuild the graph exactly the same way, so the pair must fire around those too — they
    // are handed to the undoable action as its pre/post restore hooks. Skipping them on undo would leave the
    // graph editor holding stale ModuleComponents that reference freed VisualBuffers.
    juce::WeakReference<AIIntegrationService> weakThis(this);
    auto notifyAboutToApply = [weakThis] {
        if (auto* self = weakThis.get())
            self->listeners.call([](Listener& l) { l.aiPatchAboutToApply(); });
    };
    auto notifyApplied = [weakThis] {
        if (auto* self = weakThis.get())
            self->listeners.call([](Listener& l) { l.aiPatchApplied(); });
    };

    auto applyNow = [this, json, clearExisting, notifyAboutToApply, notifyApplied] {
        notifyAboutToApply();
        if (AIStateMapper::applyJSONToGraph(json, audioGraph, clearExisting)) {
            notifyApplied();
            return true;
        }
        lastPatchError = "Patch could not be applied to the graph.";
        juce::Logger::writeToLog("applyPatch failed during applyJSONToGraph: " + lastPatchError);
        return false;
    };

    // With an undo manager installed the apply is wrapped in a snapshot transaction so Cmd+Z restores the
    // user's previous patch; without one (e.g. tests that construct the service standalone) it applies directly.
    if (undoManager != nullptr) {
        return undoManager->recordAIPatch(audioGraph, mergeMode ? "AI merge" : "AI patch", applyNow, notifyAboutToApply,
                                          notifyApplied);
    }

    return applyNow();
}

void AIIntegrationService::applyPatchWithRetry(const juce::String& jsonString, bool mergeMode,
                                               PatchApplyCallback onComplete, PatchRetryCallback onRetry) {
    // Attempt 1 is the patch the caller already has in hand; retries are what follow.
    if (applyPatch(jsonString, mergeMode)) {
        if (onComplete)
            onComplete(true, {});
        return;
    }

    // Nothing to ask for a correction — report the rejection as-is rather than pretending to retry.
    if (provider == nullptr) {
        if (onComplete)
            onComplete(false, lastPatchError);
        return;
    }

    // Captured once, before any correction turn is appended to chatHistory — see
    // mostRecentUserRequest()'s doc comment for why this must not be re-derived per retry.
    requestPatchCorrection(1, mergeMode, mostRecentUserRequest(), std::move(onComplete), std::move(onRetry));
}

void AIIntegrationService::requestPatchCorrection(int failedAttempt, bool mergeMode,
                                                  const juce::String& originalRequest, PatchApplyCallback onComplete,
                                                  PatchRetryCallback onRetry) {
    const int totalAttempts = kMaxPatchRetries + 1;
    const juce::String error = lastPatchError;

    // The bound. Without it a model that keeps producing the same invalid patch would keep us
    // round-tripping forever while the user waits on a spinner.
    if (failedAttempt >= totalAttempts) {
        juce::Logger::writeToLog("applyPatch gave up after " + juce::String(failedAttempt) +
                                 " attempts, last error: " + error);
        if (onComplete)
            onComplete(false, error);
        return;
    }

    if (onRetry)
        onRetry({failedAttempt, totalAttempts, error});

    // One log per retry. Retries happen at user-click frequency, not per token or per validation
    // pass inside a loop, so this stays within the no-high-frequency-logging rule.
    juce::Logger::writeToLog("applyPatch retrying (attempt " + juce::String(failedAttempt + 1) + " of " +
                             juce::String(totalAttempts) + ") after: " + error);

    juce::WeakReference<AIIntegrationService> weakThis(this);
    sendMessage(
        buildCorrectionPrompt(originalRequest, error),
        [weakThis, failedAttempt, mergeMode, originalRequest, onComplete,
         onRetry](const AIProvider::AIResponse& response) {
            auto* self = weakThis.get();
            if (self == nullptr)
                return; // service destroyed mid-retry; nothing left to apply to

            if (!response.success) {
                if (onComplete)
                    onComplete(false, response.error.message);
                return;
            }

            if (self->applyPatch(response.content, mergeMode)) {
                if (onComplete)
                    onComplete(true, {});
                return;
            }

            self->requestPatchCorrection(failedAttempt + 1, mergeMode, originalRequest, onComplete, onRetry);
        },
        /*useStructuredOutput=*/true);
}

juce::String AIIntegrationService::buildCorrectionPrompt(const juce::String& originalRequest,
                                                         const juce::String& error) {
    // Naming the specific failure is the point of the retry. A bare "that didn't work, try again"
    // tends to reproduce the same mistake, because nothing told the model which part was wrong.
    //
    // Restating the original request matters just as much: RemoteProvider sends only this message,
    // not the conversation, so without it the model has no idea what the patch was even supposed to
    // be — verified live, it invents a generic fix referencing node ids that don't exist anywhere.
    return "Original request: " + originalRequest +
           "\n\nThe patch you returned for that request was rejected by the synthesizer and was NOT applied."
           "\n\nReason: " +
           error +
           "\n\nReturn a corrected patch for the ORIGINAL REQUEST above, as raw JSON, that fixes exactly this "
           "problem. Keep everything else about the patch the same. Use only module types and parameter choice "
           "strings that appear in the schema, and reference only node ids that exist in the current patch or "
           "that this patch itself creates.";
}

juce::String AIIntegrationService::mostRecentUserRequest() const {
    for (auto it = chatHistory.rbegin(); it != chatHistory.rend(); ++it)
        if (it->role == "user")
            return it->content;
    return {};
}

juce::String AIIntegrationService::extractJsonFromResponse(const juce::String& response) {
    // 1. Try to find JSON between backticks
    int start = response.indexOf("```json");
    if (start != -1) {
        start += 7;
        int end = response.indexOf(start, "```");
        if (end != -1)
            return response.substring(start, end).trim();
    }

    // 2. Try to find JSON between any backticks
    start = response.indexOf("```");
    if (start != -1) {
        start += 3;
        int end = response.indexOf(start, "```");
        if (end != -1)
            return response.substring(start, end).trim();
    }

    // 3. Try to find first '{' and last '}'
    start = response.indexOf("{");
    int end = response.lastIndexOf("}");
    if (start != -1 && end != -1 && end > start)
        return response.substring(start, end + 1).trim();

    return response.trim();
}

#if SYNTH_ENABLE_TIMELINE
juce::var AIIntegrationService::extractTimelineOps(const juce::String& response) {
    const juce::var parsed = juce::JSON::parse(extractJsonFromResponse(response));
    // Presence, not well-formedness: a malformed "timelineOps" must reach previewTimelineOps() and
    // be reported, never be quietly dropped as if the model had asked for nothing (the same rule
    // that keeps applyPatch from swallowing a rejected patch).
    return TimelineOps::carriesOps(parsed) ? parsed : juce::var();
}

TimelineOpsResult AIIntegrationService::previewTimelineOps(const juce::var& envelope) const {
    if (timelineDoc == nullptr)
        return {false, "This build has no timeline wired in, so timeline changes cannot be checked or applied.", {}};

    return TimelineOps::validate(envelope, *timelineDoc, audioGraph);
}

TimelineOpsResult AIIntegrationService::applyTimelineOps(const juce::var& envelope) {
    if (!timelineOpsApply)
        return {false, "Timeline changes cannot be applied from here.", {}};

    const auto result = timelineOpsApply(envelope);
    // One log per Apply click — user-click frequency, so it stays within the no-high-frequency
    // logging rule, and in Debug builds it lands in AIChatComponent's console.
    juce::Logger::writeToLog(juce::String("applyTimelineOps ") + (result.ok ? "applied: " : "rejected: ") +
                             result.message);
    return result;
}
#endif

juce::String AIIntegrationService::getPatchContext() {
    juce::var json = AIStateMapper::graphToJSON(audioGraph);
    return juce::JSON::toString(json);
}

void AIIntegrationService::clearHistory() {
    chatHistory.clear();
    initSystemPrompt();
}

void AIIntegrationService::initSystemPrompt() { chatHistory.push_back({"system", buildSystemPrompt()}); }

void AIIntegrationService::refreshSystemPrompt() {
    // Swap the system message in place: a mid-conversation toggle (the timeline preference, or
    // the timeline context arriving after construction) must not clear the user's chat. With no
    // system message yet (never initialised — cannot happen in practice, but cheap to honour),
    // fall back to the normal init.
    if (!chatHistory.empty() && chatHistory.front().role == "system") {
        chatHistory.front().content = buildSystemPrompt();
        return;
    }
    initSystemPrompt();
}

juce::String AIIntegrationService::buildSystemPrompt() const {
    juce::String schema = AIStateMapper::getModuleSchema();

    juce::String systemMsg =
        "You are " + juce::String(synth::branding::kProductName) + " AI, an expert sound designer for the " +
        juce::String(synth::branding::kProductName) +
        " modular synthesizer. "
        "Your goal is to help users create and modify patches. " +
        juce::String(synth::branding::kProductName) +
        " uses a nodes-and-connections model. "
        "\n\n" +
        schema +
        "\n"
        "### AVAILABLE PARAMETER CHOICES (USE THESE!):\n"
        "For any parameter listed as a 'Choice' in the module tables above, you MUST use one of the exact option "
        "strings shown there for that parameter. "
        "For example, for an LFO 'shape' parameter, if the schema says `\"LFO\": { \"shape\": [\"Sine\", \"S&H\"] }, "
        "you MUST NOT guess other shapes or use numbers. For 'rateSync', use the exact string, e.g., \"1/4\", NOT the "
        "number 0.25.\n"
        "If you do not use the exact string from the schema, the parameter will be ignored or set to default.\n"
        "\n"
        "### MODES OF OPERATION:\n"
        "1. **Conversational Mode**: When the user asks a general question, respond naturally in Markdown.\n"
        "2. **Structured Patch Mode**: When requested to create or modify a patch, you MUST provide a JSON block. "
        "If a 'format' schema is provided in the API request, your entire response MUST be the raw JSON adhering to "
        "that schema, with NO additional text or Markdown formatting.\n"
        "\n"
        "### CRITICAL SIGNAL ROUTING (CONNECTIONS VS MODULATIONS):\n"
        "1. **AUDIO/MIDI SIGNAL FLOW (Connections)**: Use the `connections` array to route audio (Oscillator output -> "
        "Filter input) and MIDI (Sequencer -> Oscillator). "
        "Audio/MIDI ports are usually 0 for audio or -1 for MIDI.\n"
        "2. **MODULATION/CONTROL SIGNAL FLOW (Modulations)**: Use the `modulations` array ONLY when you want a control "
        "signal (LFO, envelope) to modulate a parameter (e.g., Filter Cutoff, VCA Level). "
        "The system will automatically create an Attenuverter node to handle this.\n"
        "\n"
        "### SIGNAL ROUTING EXAMPLE (Adding Oscillator to existing Filter):\n"
        "User request: 'Add a new Saw oscillator connected to existing filter node 100'.\n"
        "```json\n"
        "{\n"
        "  \"mode\": \"merge\",\n"
        "  \"nodes\": [{ \"id\": 9005, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\" } }],\n"
        "  \"connections\": [{ \"src\": 9005, \"srcPort\": 0, \"dst\": 100, \"dstPort\": 0 }]\n"
        "}\n"
        "```\n"
        "\n### IMPORTANT INSTRUCTIONS FOR PATCHES:\n"
        "- **Parameter IDs are Case-Sensitive**: Use the exact `Parameter ID` from the table above (e.g., use "
        "`cutoff`, not `Cutoff`).\n"
        "- **Values**: Use raw, unnormalized values within the specified ranges. Do NOT use normalized 0-1 values. "
        "For example, Oscillator `octave` range is -4 to 4 (default 0), `coarse` is -12 to 12 (default 0), "
        "`fine` is -100 to 100 (default 0).\n"
        "- **Oscillator has no `frequency` parameter**: pitch comes from the incoming MIDI note (or A4/440Hz if "
        "none is connected) offset by `octave`/`coarse`/`fine`. To pitch an Oscillator, set those three, not a "
        "frequency value.\n"
        "- **Omit default parameters**: ONLY include the parameters you specifically want to change. Do NOT send full "
        "lists of all parameters. Omitted parameters keep their default values. For example, if you are adding an "
        "Oscillator and only want to set 'octave' to 1, only include `{\"octave\": 1.0}` in the `params` object.\n"
        "- **Choice Parameters**: Use the exact string name (e.g., `\"waveform\": \"Saw\"`).\n"
        "- **Connections**: Ensure `srcPort` and `dstPort` are valid for the given module type. Most modules use port "
        "0 for their primary audio/midi signal.\n"
        "\nExample format:\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 1, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\", \"octave\": -1 } },\n"
        "    { \"id\": 2, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 1, \"srcPort\": 0, \"dst\": 2, \"dstPort\": 0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "\n### DELTA / MERGE MODE:\n"
        "When the user's message includes their current patch state (as JSON) and they ask to ADD, MODIFY, or REMOVE "
        "elements, respond with only the CHANGES (delta), not the entire patch. Include `\"mode\": \"merge\"` in your "
        "JSON.\n"
        "- **Adding nodes**: Include only NEW nodes in `nodes`. Use existing node IDs (from the current patch state) "
        "in "
        "`connections` to wire new nodes to existing ones.\n"
        "- **Modifying parameters**: Include the existing node (same ID, same type) with only the changed params.\n"
        "- **Removing nodes**: Use `\"remove\": [nodeId]` to delete nodes by their ID from the current patch.\n"
        "- **Full replacement**: When creating from scratch or when no current patch exists, use `\"mode\": "
        "\"replace\"` "
        "(or omit `mode`).\n"
        "\nDelta example (adding a Reverb to an existing VCA node 1003):\n"
        "```json\n"
        "{\"mode\": \"merge\", \"nodes\": [{\"id\": 9001, \"type\": \"Reverb\"}], "
        "\"connections\": [{\"src\": 1003, \"srcPort\": 0, \"dst\": 9001, \"dstPort\": 0}]}\n"
        "```\n"
        "\nRemoval example:\n"
        "```json\n"
        "{\"mode\": \"merge\", \"remove\": [1003], \"nodes\": [], \"connections\": []}\n"
        "```\n"
        "\n### MODULATION ROUTING (CRITICAL):\n"
        "**ALWAYS use the `modulations` array** when routing a modulation source (LFO, ADSR, envelope) "
        "to a parameter target (filter cutoff, VCA CV, etc.). Do NOT use `connections` for modulation — "
        "`connections` are ONLY for audio signal flow (e.g., Oscillator->Filter->VCA->Output) and MIDI. "
        "The system automatically creates the necessary Attenuverter intermediary.\n"
        "\nEach modulation entry needs:\n"
        "- `source`: node ID of the modulation source (LFO, envelope, etc.)\n"
        "- `sourcePort` (optional, default 0): output port of the source module\n"
        "- `dest`: node ID of the destination module\n"
        "- `destPort`: the input port for the target parameter (see Modulation Targets table above)\n"
        "- `amount` (optional, default 1.0): modulation depth from -1.0 (inverted) to 1.0\n"
        "- `bypass` (optional, default false): whether the modulation is bypassed\n"
        "\nModulation example (LFO modulating filter cutoff at 50% depth):\n"
        "```json\n"
        "{\"nodes\": [{\"id\": 5, \"type\": \"LFO\", \"params\": {\"rateHz\": 2.0}}, "
        "{\"id\": 3, \"type\": \"Filter\"}], "
        "\"connections\": [], "
        "\"modulations\": [{\"source\": 5, \"dest\": 3, \"destPort\": 1, \"amount\": 0.5}]}\n"
        "```\n"
        "\nMerge example (adding LFO modulation to existing filter node 1003):\n"
        "```json\n"
        "{\"mode\": \"merge\", \"nodes\": [{\"id\": 9001, \"type\": \"LFO\", \"params\": {\"rateHz\": 4.0}}], "
        "\"connections\": [], "
        "\"modulations\": [{\"source\": 9001, \"dest\": 1003, \"destPort\": 1, \"amount\": 0.8}]}\n"
        "```\n"
        "\nTo remove a modulation in merge mode, use `removeModulations`:\n"
        "```json\n"
        "{\"mode\": \"merge\", \"removeModulations\": [{\"source\": 5, \"dest\": 3, \"destPort\": 1}], "
        "\"nodes\": [], \"connections\": []}\n"
        "```\n"
        "\n### WORKED EXAMPLES (COMPLETE, CORRECTLY-CONNECTED PATCHES):\n"
        "The patches below are full worked examples, not syntax fragments — study how every node is wired "
        "to something and every signal path actually reaches Audio Output. The single most common mistake "
        "is a structurally valid patch that is silent (no Audio Output) or dangling (a node nobody connected "
        "to anything); these examples show the complete, correct shape to copy.\n"
        "\n**Example 1 — from scratch, bass:**\n"
        "User request: \"Patch together a growling, punchy analog-style bass line\"\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 101, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\", \"octave\": -1 } },\n"
        "    { \"id\": 102, \"type\": \"Filter\", \"params\": { \"cutoff\": 800.0, \"resonance\": 0.4 } },\n"
        "    { \"id\": 103, \"type\": \"VCA\" },\n"
        "    { \"id\": 104, \"type\": \"Filter Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.3, \"sustain\": "
        "0.2, \"release\": 0.2 } },\n"
        "    { \"id\": 105, \"type\": \"Amp Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.15, \"sustain\": "
        "0.7, \"release\": 0.2 } },\n"
        "    { \"id\": 106, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 101, \"srcPort\": 0, \"dst\": 102, \"dstPort\": 0 },\n"
        "    { \"src\": 102, \"srcPort\": 0, \"dst\": 103, \"dstPort\": 0 },\n"
        "    { \"src\": 103, \"srcPort\": 0, \"dst\": 106, \"dstPort\": 0 }\n"
        "  ],\n"
        "  \"modulations\": [\n"
        "    { \"source\": 104, \"dest\": 102, \"destPort\": 1, \"amount\": 0.6 },\n"
        "    { \"source\": 105, \"dest\": 103, \"destPort\": 1, \"amount\": 1.0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "Note every node is on the audio path (Oscillator -> Filter -> VCA -> Audio Output) and both "
        "envelopes drive real modulation targets (Filter's `Cutoff` is destPort 1, VCA's `CV` is destPort 1) "
        "rather than sitting unconnected.\n"
        "\n**Example 2 — from scratch, lead:**\n"
        "User request: \"Design a snappy square-wave pluck lead\"\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 201, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Square\", \"octave\": 1 } },\n"
        "    { \"id\": 202, \"type\": \"Filter\", \"params\": { \"cutoff\": 3000.0, \"resonance\": 0.3 } },\n"
        "    { \"id\": 203, \"type\": \"VCA\" },\n"
        "    { \"id\": 204, \"type\": \"Filter Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.2, \"sustain\": "
        "0.0, \"release\": 0.1 } },\n"
        "    { \"id\": 205, \"type\": \"Amp Env\", \"params\": { \"attack\": 0.01, \"decay\": 0.15, \"sustain\": "
        "0.0, \"release\": 0.1 } },\n"
        "    { \"id\": 206, \"type\": \"LFO\", \"params\": { \"mode\": false, \"rateHz\": 5.0, \"level\": 1.0 } },\n"
        "    { \"id\": 207, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 201, \"srcPort\": 0, \"dst\": 202, \"dstPort\": 0 },\n"
        "    { \"src\": 202, \"srcPort\": 0, \"dst\": 203, \"dstPort\": 0 },\n"
        "    { \"src\": 203, \"srcPort\": 0, \"dst\": 207, \"dstPort\": 0 }\n"
        "  ],\n"
        "  \"modulations\": [\n"
        "    { \"source\": 204, \"dest\": 202, \"destPort\": 1, \"amount\": 0.7 },\n"
        "    { \"source\": 205, \"dest\": 203, \"destPort\": 1, \"amount\": 1.0 },\n"
        "    { \"source\": 206, \"dest\": 201, \"destPort\": 4, \"amount\": 1.0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "The LFO targets the Oscillator's `Fine` input (destPort 4) for a gentle pitch drift — a real, "
        "working CV target. Oscillator's own `Pitch` input (destPort 0) is a trap: it exists in the schema "
        "but mono oscillators ignore CV on it, so modulating it does nothing audible.\n"
        "\n**Example 3 — FX chain:**\n"
        "User request: \"Chain a saw-wave source through drive, chorus, and reverb for a wide, driven "
        "texture\"\n"
        "```json\n"
        "{\n"
        "  \"nodes\": [\n"
        "    { \"id\": 301, \"type\": \"Oscillator\", \"params\": { \"waveform\": \"Saw\" } },\n"
        "    { \"id\": 302, \"type\": \"Filter\", \"params\": { \"cutoff\": 1500.0 } },\n"
        "    { \"id\": 303, \"type\": \"VCA\" },\n"
        "    { \"id\": 304, \"type\": \"Distortion\", \"params\": { \"drive\": 3.0, \"mix\": 0.3 } },\n"
        "    { \"id\": 305, \"type\": \"Chorus\", \"params\": { \"rate\": 0.6, \"depth\": 0.4, \"mix\": 0.5 } },\n"
        "    { \"id\": 306, \"type\": \"Reverb\", \"params\": { \"roomSize\": 0.7, \"wet\": 0.4 } },\n"
        "    { \"id\": 307, \"type\": \"Audio Output\" }\n"
        "  ],\n"
        "  \"connections\": [\n"
        "    { \"src\": 301, \"srcPort\": 0, \"dst\": 302, \"dstPort\": 0 },\n"
        "    { \"src\": 302, \"srcPort\": 0, \"dst\": 303, \"dstPort\": 0 },\n"
        "    { \"src\": 303, \"srcPort\": 0, \"dst\": 304, \"dstPort\": 0 },\n"
        "    { \"src\": 304, \"srcPort\": 0, \"dst\": 305, \"dstPort\": 0 },\n"
        "    { \"src\": 305, \"srcPort\": 0, \"dst\": 306, \"dstPort\": 0 },\n"
        "    { \"src\": 306, \"srcPort\": 0, \"dst\": 307, \"dstPort\": 0 }\n"
        "  ]\n"
        "}\n"
        "```\n"
        "Every FX module sits IN LINE between the source and Audio Output — never bolted on to the side "
        "unconnected. FX modules like Delay and Reverb list CV modulation targets in the schema above, but "
        "their audio engines don't actually read that CV; automate their effect with `params` changes "
        "instead, not `modulations`.\n"
        "\n**Example 4 — merge mode, adding a node:**\n"
        "Existing patch: node 401 Oscillator (Saw) -> 402 Filter (cutoff 1200) -> 403 VCA -> 404 Audio "
        "Output.\n"
        "User request: \"Stack a second oscillator a fifth above the existing one, feeding into the same "
        "filter\"\n"
        "```json\n"
        "{\"mode\": \"merge\", \"nodes\": [{\"id\": 9401, \"type\": \"Oscillator\", \"params\": {\"waveform\": "
        "\"Saw\", \"coarse\": 7}}], "
        "\"connections\": [{\"src\": 9401, \"srcPort\": 0, \"dst\": 402, \"dstPort\": 0}]}\n"
        "```\n"
        "Only the new node and the one new connection are included — 401, 403, and 404 are untouched. Two "
        "sources connected to the same destination port sum there, which is how you mix oscillators without "
        "a dedicated mixer node.\n"
        "\n**Example 5 — merge mode, removing a node:**\n"
        "Existing patch: node 501 Oscillator (Square) -> 502 Distortion -> 503 Filter (cutoff 2500) -> 504 "
        "VCA -> 505 Audio Output.\n"
        "User request: \"Take out the distortion — it's too harsh — and connect straight through instead\"\n"
        "```json\n"
        "{\"mode\": \"merge\", \"remove\": [502], \"nodes\": [], "
        "\"connections\": [{\"src\": 501, \"srcPort\": 0, \"dst\": 503, \"dstPort\": 0}]}\n"
        "```\n"
        "Removing a node is not enough on its own — the chain must be rewired around the gap, or the patch "
        "ends up with 501 dangling and nothing reaching Audio Output.";

#if SYNTH_ENABLE_TIMELINE
    // The timeline tool section exists only while the runtime switch is on AND a timeline context
    // is installed — off, this prompt is byte-identical to the pre-timeline one (pinned by
    // AIIntegrationServiceTest.TimelineToolsToggleGatesThePromptAndSchema).
    if (timelineToolsActive()) {
        systemMsg +=
            "\n\n### TIMELINE & AUTOMATION OPERATIONS (timelineOps):\n"
            "The user also has a TIMELINE: tracks, MIDI clips with notes, and parameter-automation lanes. "
            "When they ask for arrangement content — notes/melodies/chords on a track, a new track, or a "
            "parameter changing OVER TIME (\"automate\", \"sweep\", \"fade\", \"over N bars\") — add a "
            "top-level `timelineOps` array to your JSON response (alongside `nodes`/`connections`; use empty "
            "arrays for those when the graph itself needs no change). Beats are quarter-note beats; beat 0 is "
            "bar 1. The current tracks appear in the \"## Arrangement\" section and the automatable targets "
            "in the \"## Automation targets\" section of the user's message.\n"
            "The operations (max 64 per response; ANY invalid op rejects the whole batch, so keep batches "
            "small and exact):\n"
            "1. `{\"op\": \"addTrack\", \"kind\": \"midi\"|\"automation\", \"name\": \"...\"}` — a new empty "
            "track.\n"
            "2. `{\"op\": \"placeClips\", \"track\": \"<exact MIDI track name>\" or {\"index\": N}, "
            "\"clips\": [{\"startBeat\": 0, \"lengthBeats\": 4, \"name\": \"...\", \"notes\": [{\"startBeat\": "
            "0, \"lengthBeats\": 1, \"pitch\": 60, \"velocity\": 100}]}]}` — writes MIDI notes; a note's "
            "startBeat is relative to ITS CLIP's start, and notes must fit inside the clip.\n"
            "3. `{\"op\": \"writeLane\", \"nodeUuid\": \"<uuid from Automation targets>\", \"paramId\": "
            "\"<param id>\", \"points\": [{\"beat\": 0, \"value\": 200}, {\"beat\": 8, \"value\": 8000}]}` — "
            "parameter automation over time. Values are RAW values inside the parameter's listed range "
            "(never 0-1 normalised). Points REPLACE any existing points inside the written beat span. Only "
            "(nodeUuid, paramId) pairs from the Automation targets section resolve — never invent a uuid.\n"
            "4. `placeMidiClip` (a base64 .mid blob) exists but PREFER placeClips — explicit notes are "
            "checkable.\n"
            "The user always sees a preview and must click Apply before anything changes.\n"
            "\nAutomation example — \"sweep the filter cutoff up over 8 beats\" (Filter uuid \"abc-123\"):\n"
            "```json\n"
            "{\"nodes\": [], \"connections\": [], \"timelineOps\": [{\"op\": \"writeLane\", \"nodeUuid\": "
            "\"abc-123\", \"paramId\": \"cutoff\", \"points\": [{\"beat\": 0, \"value\": 200.0}, {\"beat\": 8, "
            "\"value\": 8000.0}]}]}\n"
            "```\n"
            "Melody example — notes on existing MIDI track \"Lead\":\n"
            "```json\n"
            "{\"nodes\": [], \"connections\": [], \"timelineOps\": [{\"op\": \"placeClips\", \"track\": "
            "\"Lead\", \"clips\": [{\"startBeat\": 0, \"lengthBeats\": 4, \"name\": \"Riff\", \"notes\": ["
            "{\"startBeat\": 0, \"lengthBeats\": 1, \"pitch\": 60, \"velocity\": 100}, {\"startBeat\": 1, "
            "\"lengthBeats\": 1, \"pitch\": 63, \"velocity\": 100}, {\"startBeat\": 2, \"lengthBeats\": 2, "
            "\"pitch\": 67, \"velocity\": 100}]}]}]}\n"
            "```";
    }
#endif

    return systemMsg;
}

void AIIntegrationService::setModel(const juce::String& name) {
    if (provider)
        provider->setModel(name);
}

juce::String AIIntegrationService::getCurrentModel() const { return provider ? provider->getCurrentModel() : ""; }

void AIIntegrationService::fetchAvailableModels(
    std::function<void(const juce::StringArray& models, bool success)> callback) {
    if (provider)
        provider->fetchAvailableModels(callback);
    else if (callback)
        callback({}, false);
}
} // namespace synth
