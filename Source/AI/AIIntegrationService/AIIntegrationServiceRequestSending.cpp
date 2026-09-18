// Request building & sending: sendMessage/sendArrangeMessage and the content/body builders they
// share, plus chat-history bookkeeping (trim/clear) that every send path feeds into.
#include "AIIntegrationService.h"
#include <algorithm>

namespace synth {

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

// Every outgoing request needs the same success bookkeeping: append the assistant turn to
// chatHistory (never for a cancelled request — see the comment inside) and capture/re-push a
// Pro-plan conversation id. Shared by sendMessage() and sendArrangeMessage() so the two paths
// cannot drift on history or conversation-id behaviour.
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
    if (timelineDoc != nullptr && transportService != nullptr && !timelineDoc->isEmpty()) {
        const juce::String summary =
            ArrangementContext::summarize(*timelineDoc, audioGraph, transportService->getPositionSnapshot());
        if (summary.isNotEmpty())
            arrangementSection = "## Arrangement\n" + summary;
    }

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
    if (timelineToolsActive()) {
        const juce::String targets = buildAutomationTargetsSection();
        if (targets.isNotEmpty())
            content << targets << "\n\n";
    }
    content << "User request: " << text;
    return content;
}

std::vector<AIIntegrationService::AutomationTargetInfo> AIIntegrationService::enumerateAutomationTargets() const {
    // Uuid-bearing nodes only (a node without one is not addressable by writeLane), float
    // parameters only (that is what an automation lane drives), real ranges from the parameter's
    // own NormalisableRange. See the AutomationTargetInfo doc comment in the header: this ONE
    // enumeration feeds both the local model's text section and the remote timeline.generate body,
    // so the criteria live here and nowhere else.
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

// The (nodeUuid, paramId, range) inventory a `writeLane` op needs — the model cannot name a node
// it was never told about. Uuids appear here ON PURPOSE, despite ArrangementContext's no-uuid
// rule: that rule keeps identifiers out of the human-readable SUMMARY (where a display name serves
// better and a leak buys nothing); this section is the ADDRESSING channel without which the
// writeLane grammar is unusable. A node uuid is random per-node identity — never a file path,
// plugin identifier or factory key — and validate() only accepts pairs that resolve against the
// live graph anyway. (Also documented in docs/ai/timeline-ops.md#the-local-path.)
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

// Builds the `timeline.generate` request body for `text` — everything the input schema wants
// except productName, which the provider adds (it owns branding). Fields (see
// TimelineGenerateInputSchema, synth-platform timeline-generate/capability.ts):
//  - `userPrompt`: the RAW user text, deliberately NOT pre-wrapped with patch/arrangement context
//    the way buildPatchAugmentedContent() does — timeline.generate composes its context sections
//    server-side from the structured fields below, unconditionally, so pre-wrapping would
//    duplicate every section in the model input.
//  - `arrangementContext`: ArrangementContext::summarize() of the live doc; "" when the doc is
//    empty or no timeline context is installed (the schema requires the key but allows empty — "a
//    caller with nothing to say should say so explicitly").
//  - `paramTargets`: the SAME (uuid-bearing node, float param, real range) enumeration
//    buildAutomationTargetsSection() renders as text, as structured objects {nodeUuid, nodeName,
//    paramId, min, max, default}, capped at kMaxRemoteParamTargets.
//  - `availableTracks`: one {name, kind, index} per live TimelineDoc track, in doc order.
juce::var AIIntegrationService::buildArrangeRequestBody(const juce::String& text) const {
    juce::DynamicObject::Ptr body = new juce::DynamicObject();

    // The RAW user text (see the comment above).
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

// ONE intent, two transports — the local/remote parity rule: the transport difference is absorbed
// HERE, never surfaced as a behaviour difference. Hosted provider: the `timeline.generate`
// capability, with the structured request body from buildArrangeRequestBody(). Local provider:
// sendPrompt() with the SAME fields composed into the outgoing message (buildArrangeAugmentedContent,
// mirroring the server's own section layout) and AIStateMapper::getTimelineOpsEnvelopeSchema() as
// the response contract. Both providers answer with the identical timelineOps envelope, so the
// downstream extract -> validate -> card flow cannot tell them apart.
//
// No client-side retry on a validation rejection: the server runs its own bounded repair-retry
// inside the capability, and for the local model the envelope-only grammar plays the same role —
// an envelope that still fails TimelineOps::validate is surfaced to the user as the card's
// rejection message. On a hosted provider without a capability endpoint (a test double), the
// AIProvider::sendCapabilityRequest default delivers a typed Schema error.
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

// Trims chatHistory to the system prompt plus the most recent kMaxHistoryTurns pairs, removing
// oldest whole user+assistant pairs so history never starts on an assistant turn.
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

void AIIntegrationService::clearHistory() {
    chatHistory.clear();
    initSystemPrompt();
}

} // namespace synth
