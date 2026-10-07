#include "AI/PatchDiff.h"
#include "AIChatComponent.h"

namespace synth {

// Concern: sending a request and reading its answer -- routes the message (an edit plan, or a
// free-text answer), turns the response into a bubble plus at most one edit-plan card, and
// previews that plan once.

namespace {

// readPlan's own patch keys plus "timelineOps": a JSON object with none of them is not a plan, and
// is shown as text rather than as a card that could only say "nothing to apply".
bool isEditPlan(const juce::var& root) {
    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return false;
    for (const char* key : {"nodes", "connections", "remove", "modulations", "removeModulations", "timelineOps"})
        if (object->hasProperty(key))
            return true;
    return false;
}

} // namespace

juce::var AIChatComponent::extractEditPlan(const juce::String& response, bool wholeResponseIsJson,
                                           juce::String& prose) {
    prose = response;
    const int start = response.indexOf("```json");
    const int end = start == -1 ? -1 : response.indexOf(start + 7, "```");
    if (end != -1) {
        const juce::var plan = juce::JSON::parse(response.substring(start + 7, end).trim());
        if (!isEditPlan(plan))
            return {};
        prose = response.substring(0, start) + response.substring(end + 3);
        return plan;
    }
    if (!wholeResponseIsJson)
        return {};
    const juce::var plan = juce::JSON::parse(response.trim());
    if (!isEditPlan(plan))
        return {};
    prose = {};
    return plan;
}

bool AIChatComponent::shouldUseStructuredOutput(const juce::String& text, const juce::StringArray& moduleTypeNames) {
    // Any real module/effect type name (Chorus, Distortion, Oscillator, ...) means the user is
    // almost certainly talking about the graph, even without an explicit edit verb ("what does
    // the Reverb's decay knob do?"). Deriving this from the module factory registry — rather than
    // a hand-picked handful — means a new module type is covered automatically; the old hardcoded
    // list (oscillator/filter/vca/adsr) silently missed everything else, which is what caused this
    // bug ("Add a chorus between the distortion to the delay" matched none of the four).
    for (const auto& moduleType : moduleTypeNames)
        if (text.containsIgnoreCase(moduleType))
            return true;

    // Generic edit-intent verbs/nouns that show up in a patch-authoring request regardless of
    // which module is named (or when no module is named at all, e.g. "add a filter" without
    // capitalizing on a specific type, or "increase the cutoff"). Bias toward inclusion: a false
    // positive here just spends ~1.5k tokens of unnecessary patch context; a false negative
    // reproduces the original bug (request goes out with no graph context and the model has to
    // guess or ask).
    // The second group covers what used to need the Arrange mode: tracks, notes, automation and
    // modulation now go out in the same request as a patch edit, so they must count as edits too.
    static const char* kEditIntentWords[] = {
        "patch", "create",   "modify",   "sound", "preset", "add",      "remove",    "delete",  "connect", "change",
        "set",   "increase", "decrease", "swap",  "insert", "between",  "track",     "automat", "modulat", "arrang",
        "clip",  "melod",    "chord",    "drum",  "notes",  "bassline", "bass line", "lane",    "write",
    };
    for (const auto* word : kEditIntentWords)
        if (text.containsIgnoreCase(word))
            return true;

    return false;
}

void AIChatComponent::sendButtonClicked() {
    auto text = inputField.getText().trim();
    if (text.isEmpty())
        return;

    inputField.clear();

    // One input, one kind of answer. With a timeline wired in, a message that asks for a change
    // (the classifier below; always on a hosted provider, which has no free-text chat) asks for an
    // edit plan through sendProjectMessage: hosted project.generate, or the local model with the
    // combined schema. A local conversational question still gets a free-text answer. With no
    // timeline (tests, a host without one) the request is the patch request it has always been.
    const bool editRequest = shouldUseStructuredOutput(text, AIStateMapper::moduleFactoryTypeNames());
    const bool projectPlan = aiService.hasTimelineContext() && (editRequest || aiService.isCurrentProviderHosted());
    const bool wantsPlan = projectPlan || editRequest;

    // A message is going out to the assistant (usage statistics count requests, never their text).
    if (onMessageSent)
        onMessageSent();

    // Add user message to local state immediately
    messages.push_back({"user", text, ""});
    setWaiting(true);
    requestStartMs = juce::Time::getMillisecondCounter();
    updateChatDisplay();

    sendButton.setEnabled(false);
    inputField.setReadOnly(true);

    // Show cancel affordance and start the pulse spinner.
    cancelButton.setVisible(true);
    spinnerDot.setVisible(true);
    spinnerDot.startPulse(vblankUpdater);

    // Live thinking-status timer (also enforces the request timeout). It covers every route below:
    // a hosted plan takes 20 to 30 seconds, well inside the 4-minute default.
    startTimer(kWaitingStatusIntervalMs);

    // Conversation-id persistence is Pro-only (server-enforced; see RemoteProvider's
    // x-conversation-id header). AIIntegrationService::sendMessage() auto-captures/re-pushes a
    // response id on its own for the common case (a free-plan response never carries one, so
    // there's nothing to gate there) — this only handles the Pro-to-Free downgrade mid-session,
    // where a stale id from an earlier Pro response would otherwise still be sitting in
    // AIIntegrationService and get resent to a now-free account. Clearing it here means
    // RemoteProvider naturally has nothing to send; it never has plan awareness of its own.
    if (accountServicePtr == nullptr || !isProPlan(accountServicePtr->getSnapshot()))
        aiService.setConversationId({});

    AIProvider::CompletionCallback completion = [this, wantsPlan](const AIProvider::AIResponse& aiResponse) {
        juce::Component::SafePointer<AIChatComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, aiResponse, wantsPlan]() {
            if (auto* self = safeThis.getComponent())
                self->handleResponse(aiResponse, wantsPlan);
        });
    };

    const auto requestId = projectPlan ? aiService.sendProjectMessage(text, std::move(completion))
                                       : aiService.sendMessage(text, std::move(completion), editRequest);

    // Only record the handle if we are still waiting. A provider that answers synchronously (test
    // doubles, or the "no provider selected" path) has already run the teardown above by now, and
    // storing the handle here would leave a stale id for the next cancel.
    if (isWaitingForResponse)
        activeRequestId = requestId;
}

void AIChatComponent::handleResponse(const AIProvider::AIResponse& aiResponse, bool wantsPlan) {
    if (!isWaitingForResponse)
        return; // Ignore late responses if a timeout already occurred

    // The request is finished, so there is nothing left to cancel. Clearing the handle before
    // teardown keeps cancelRequest() from asking the provider to cancel a completed id.
    activeRequestId = {};
    cancelRequest(); // stops timer, spinner, cancel btn, restores input

    // The user already got their "Cancelled." bubble from handleUserCancel(); the provider is just
    // confirming. An error bubble here would contradict it.
    if (aiResponse.error.kind == AIProvider::AIErrorKind::Cancelled)
        return;

    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);

    if (aiResponse.success) {
        // Parsed once: the plan's root goes to the preview here and its JSON to the card's Apply.
        juce::String prose;
        const juce::var plan = extractEditPlan(aiResponse.content, wantsPlan, prose);
        const juce::String planJson = plan.isVoid() ? juce::String() : juce::JSON::toString(plan);
        messages.push_back({"assistant", prose.trim(), planJson});
        messages.back().responseMs = elapsed;
        // Only present on a Pro-plan hosted response whose persistence succeeded (see
        // AIResponse::messageId's doc comment) — empty for every other case (local Ollama, free
        // plan, no provider), which is exactly what keeps the later rating-sync check a no-op.
        messages.back().serverMessageId = aiResponse.messageId;
        attachPlanPreview(messages.back(), plan);

        // Local-first — every session writes here regardless of plan, right after the assistant
        // turn lands and `messages` reflects the full exchange. Not in the service's own callback:
        // that one can run on a provider worker thread and only has aiService.chatHistory
        // (unsplit text+plan), not this component's own text/planJson-split `messages`.
        saveCurrentConversationLocally();
    } else if (aiResponse.error.kind == AIProvider::AIErrorKind::Quota) {
        // The server's message is already a complete, user-facing sentence — no "Error: " prefix,
        // same precedent as TrialExhausted/ServiceCapacityExceeded. showUpgradeAction=true adds the
        // Upgrade-to-Pro button (see MessageBubble).
        messages.push_back({"assistant", aiResponse.error.message, "", false, /*showUpgradeAction=*/true});
        messages.back().responseMs = elapsed;
        // The user may have just paid mid-session — check again so a retry right after upgrading
        // reflects the new plan without restarting the app.
        if (accountServicePtr != nullptr)
            accountServicePtr->refreshEntitlement();
    } else {
        // Some provider messages already start with "Error: " (RemoteProvider's HTTP failures);
        // prefixing those again read "Error: Error: ..." in the bubble.
        const auto& text = aiResponse.error.message;
        messages.push_back({"assistant", text.startsWith("Error: ") ? text : "Error: " + text, ""});
        messages.back().responseMs = elapsed;
    }

    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

void AIChatComponent::attachPlanPreview(MessageData& data, const juce::var& plan) {
    if (data.planJson.isEmpty())
        return;

    const auto preview = aiService.previewProjectEdit(plan);
    data.planOk = preview.ok;
    if (!preview.ok) {
        data.planPreviewLines = {"This plan was rejected and was not applied: " + preview.message};
        if (data.text.isEmpty())
            data.text = "I couldn't produce changes that apply to this project.";
        return;
    }
    data.planPreviewLines = preview.previewLines;
    if (data.text.isEmpty())
        data.text = "Here is what I would change.";

    // The details behind the card's toggle, from the same before/after snapshots the preview
    // sentence counts (PatchDiff.h): a merge as one line per change, a replace as what the new patch
    // holds, since a replace has no stable node identity to diff against.
    if (preview.patchBefore.isVoid())
        return;
    juce::StringArray lines;
    if (preview.merge) {
        for (const auto& group : groupChangesByKind(computeDiff(preview.patchBefore, preview.patchAfter)))
            lines.add(group.describe());
    } else {
        const auto summary = summarizePatch(preview.patchAfter);
        lines.add("New patch: " + juce::String((int)summary.nodeTypes.size()) +
                  (summary.nodeTypes.size() == 1 ? " module" : " modules"));
        for (const auto& type : summary.nodeTypes)
            lines.add(type);
        if (summary.connectionCount > 0)
            lines.add(juce::String(summary.connectionCount) +
                      (summary.connectionCount == 1 ? " connection" : " connections"));
    }
    data.planDetails = lines.joinIntoString("\n");
}

} // namespace synth
