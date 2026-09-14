#include "AI/PatchDiff.h"
#include "AIChatComponent.h"

namespace synth {

// Concern: sending & streaming requests -- builds the outgoing message, calls
// AIIntegrationService, and attaches the computed patch-diff preview to the response.

// Helper function to extract JSON blocks
juce::StringArray extractJSONBlocks(const juce::String& text) {
    juce::StringArray blocks;
    int searchFrom = 0;

    while (true) {
        int start = text.indexOf(searchFrom, "```json");
        if (start == -1)
            break;

        int end = text.indexOf(start + 7, "```");
        if (end == -1)
            break;

        blocks.add(text.substring(start + 7, end).trim());
        searchFrom = end + 3;
    }

    return blocks;
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
    static const char* kEditIntentWords[] = {
        "patch",   "create", "modify", "sound",    "preset",   "add",  "remove", "delete",
        "connect", "change", "set",    "increase", "decrease", "swap", "insert", "between",
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

    // EXPLICIT routing, decided by the user's mode selector alone — arrange mode is never
    // inferred from the message text. (arrangeModeActive() is always false when the selector is
    // hidden.) In arrange mode the patch-path keyword heuristic is bypassed entirely: the request
    // carries no patch schema and the response is a timelineOps envelope, not a patch.
    const bool arrangeMode = arrangeModeActive();

    bool useStructuredOutput = !arrangeMode && shouldUseStructuredOutput(text, AIStateMapper::moduleFactoryTypeNames());

    // Add user message to local state immediately
    messages.push_back({"user", text, ""});
    isWaitingForResponse = true;
    requestStartMs = juce::Time::getMillisecondCounter();
    updateChatDisplay();

    sendButton.setEnabled(false);
    inputField.setReadOnly(true);

    // Show cancel affordance and start the pulse spinner.
    cancelButton.setVisible(true);
    spinnerDot.setVisible(true);
    spinnerDot.startPulse(vblankUpdater);

    // Live thinking-status timer (also enforces the 120 s timeout).
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

    // Conversation-id persistence is Pro-only (server-enforced; see RemoteProvider's
    // x-conversation-id header). AIIntegrationService::sendMessage() auto-captures/re-pushes a
    // response id on its own for the common case (a free-plan response never carries one, so
    // there's nothing to gate there) — this only handles the Pro-to-Free downgrade mid-session,
    // where a stale id from an earlier Pro response would otherwise still be sitting in
    // AIIntegrationService and get resent to a now-free account. Clearing it here means
    // RemoteProvider naturally has nothing to send; it never has plan awareness of its own.
    if (accountServicePtr == nullptr || !isProPlan(accountServicePtr->getSnapshot()))
        aiService.setConversationId({});

    AIProvider::CompletionCallback completion = [this, useStructuredOutput,
                                                 arrangeMode](const AIProvider::AIResponse& aiResponse) {
        juce::Component::SafePointer<AIChatComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, aiResponse, useStructuredOutput, arrangeMode]() {
            if (safeThis.getComponent() == nullptr)
                return;
            auto* self = safeThis.getComponent();

            if (!self->isWaitingForResponse) {
                return;
            } // Ignore late responses if a timeout already occurred

            // The request is finished, so there is nothing left to cancel. Clearing the handle
            // before teardown keeps cancelRequest() from asking the provider to cancel a
            // completed id.
            self->activeRequestId = {};
            self->cancelRequest(); // stops timer, spinner, cancel btn, restores input

            // The user already got their "Cancelled." bubble from handleUserCancel(); the
            // provider is just confirming. An error bubble here would contradict it.
            if (aiResponse.error.kind == AIProvider::AIErrorKind::Cancelled)
                return;

            const int elapsed = (int)(juce::Time::getMillisecondCounter() - self->requestStartMs);

            if (aiResponse.success) {
                const juce::String& response = aiResponse.content;
                juce::String json;
                juce::String cleanText = response;

                // 1. Try to find JSON between backticks
                juce::StringArray jsonBlocks = extractJSONBlocks(response);
                if (!jsonBlocks.isEmpty()) {
                    json = jsonBlocks[0]; // Use the first block found
                    // Attempt to remove the JSON block from the cleanText
                    int start = response.indexOf("```json");
                    if (start != -1) {
                        int end = response.indexOf(start + 7, "```");
                        if (end != -1) {
                            cleanText = response.substring(0, start) + response.substring(end + 3);
                        }
                    }
                } else if (useStructuredOutput) {
                    // 2. If we requested structured output, the WHOLE response should be JSON
                    // Verify if it's actually JSON
                    juce::var parsed = juce::JSON::parse(response);
                    if (!parsed.isVoid()) {
                        json = response.trim();
                        cleanText = "I've created a new patch based on your request.";
                    }
                }

                // The timeline half of the SAME response, extracted independently of the
                // patch half — a model may send a patch, a timelineOps envelope, or both, and
                // "timelineOps" is a sibling key, never nested inside the patch. Offered only
                // when a live timeline is wired in, and under the identical posture the patch
                // card is under: validate NOW so the user reads a checked summary, apply only
                // when they click.
                juce::String timelineOpsJson;
                juce::String timelineOpsPreview;
                if (self->aiService.hasTimelineContext()) {
                    const juce::var envelope = AIIntegrationService::extractTimelineOps(response);
                    if (!envelope.isVoid()) {
                        const auto preview = self->aiService.previewTimelineOps(envelope);
                        if (preview.ok) {
                            timelineOpsJson = juce::JSON::toString(envelope);
                            timelineOpsPreview = preview.previewText;
                        } else {
                            // Shown, never swallowed — but with no Apply button, since there is
                            // nothing valid to apply.
                            timelineOpsPreview =
                                "These timeline changes were rejected and were NOT applied: " + preview.message;
                        }
                    }
                }

                // Arrange mode's whole response body IS the envelope JSON (timeline.generate's
                // output schema) — the timeline card below is the real rendering, so the
                // bubble carries a sentence rather than raw JSON. A rejected envelope still
                // reads as a rejection: the card shows the validator's message. If no envelope
                // was found at all, cleanText keeps the raw body — visible is debuggable,
                // the same never-swallow rule the card itself follows.
                if (arrangeMode) {
                    if (timelineOpsJson.isNotEmpty())
                        cleanText = "Here are the timeline changes I suggest.";
                    else if (timelineOpsPreview.isNotEmpty())
                        cleanText = "I couldn't produce valid timeline changes for that request.";
                }

                self->messages.push_back({"assistant", cleanText.trim(), json, /*isExpanded=*/false,
                                          /*showUpgradeAction=*/false, timelineOpsJson, timelineOpsPreview});
                self->messages.back().responseMs = elapsed;
                // P6-9: only present on a Pro-plan hosted response whose persistence
                // succeeded (see AIResponse::messageId's doc comment) — empty for every other
                // case (local Ollama, free plan, no provider), which is exactly what keeps the
                // later rating-sync check a no-op for those.
                self->messages.back().serverMessageId = aiResponse.messageId;
                self->attachPatchPreview(self->messages.back());

                // P6-8: local-first — every session writes here regardless of plan, right after
                // the assistant turn lands and `messages` reflects the full exchange. Not in
                // AIIntegrationService::sendMessage()'s own callback: that one can run on a
                // provider worker thread and only has aiService.chatHistory (unsplit
                // text+patch), not this component's own text/jsonPatch-split `messages`.
                self->saveCurrentConversationLocally();
            } else if (aiResponse.error.kind == AIProvider::AIErrorKind::Quota) {
                // The server's message is already a complete, user-facing sentence — no
                // "Error: " prefix, same precedent as TrialExhausted/ServiceCapacityExceeded.
                // showUpgradeAction=true adds the Upgrade-to-Pro button (see MessageBubble).
                self->messages.push_back({"assistant", aiResponse.error.message, "", false,
                                          /*showUpgradeAction=*/true});
                self->messages.back().responseMs = elapsed;
                // The user may have just paid mid-session — check again so a retry right after
                // upgrading reflects the new plan without restarting the app.
                if (self->accountServicePtr != nullptr)
                    self->accountServicePtr->refreshEntitlement();
            } else {
                self->messages.push_back({"assistant", "Error: " + aiResponse.error.message, ""});
                self->messages.back().responseMs = elapsed;
            }

            self->updateChatDisplay();
            self->inputField.grabKeyboardFocus();
        });
    };

    // Arrange mode routes to the hosted timeline.generate capability; everything downstream of
    // the response (extraction, preview card, user-gated Apply) is the same seam either way.
    const auto requestId = arrangeMode ? aiService.sendArrangeMessage(text, std::move(completion))
                                       : aiService.sendMessage(text, std::move(completion), useStructuredOutput);

    // Only record the handle if we are still waiting. A provider that answers synchronously (test
    // doubles, or the "no provider selected" path) has already run the teardown above by now, and
    // storing the handle here would leave a stale id for the next cancel.
    if (isWaitingForResponse)
        activeRequestId = requestId;
}

void AIChatComponent::attachPatchPreview(MessageData& data) {
    if (data.jsonPatch.isEmpty())
        return;

    // Determine merge mode for this message's patch (used for button text + apply behavior, and
    // to pick the merge/replace branch computePatchPreview() diffs below).
    bool isMerge = false;
    juce::var parsed = juce::JSON::parse(data.jsonPatch);
    if (auto* obj = parsed.getDynamicObject()) {
        juce::String mode = obj->getProperty("mode").toString();
        if (mode == "merge") {
            isMerge = true;
        } else if (mode.isEmpty()) {
            // AI didn't specify mode — infer from user intent + graph state. `data` is already
            // the last element of `messages` (see this method's doc comment), so scan backward
            // from the end for the preceding user turn.
            juce::String userText;
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                if (it->role == "user") {
                    userText = it->text;
                    break;
                }
            }
            juce::var ctx = juce::JSON::parse(aiService.getPatchContext());
            bool graphHasNodes = false;
            if (auto* ctxObj = ctx.getDynamicObject()) {
                if (auto* nodes = ctxObj->getProperty("nodes").getArray())
                    graphHasNodes = !nodes->isEmpty();
            }
            if (graphHasNodes && userText.isNotEmpty()) {
                isMerge = userText.containsIgnoreCase("add") || userText.containsIgnoreCase("change") ||
                          userText.containsIgnoreCase("modify") || userText.containsIgnoreCase("tweak") ||
                          userText.containsIgnoreCase("adjust") || userText.containsIgnoreCase("remove") ||
                          userText.containsIgnoreCase("delete") || userText.containsIgnoreCase("make it") ||
                          userText.containsIgnoreCase("more") || userText.containsIgnoreCase("less") ||
                          userText.containsIgnoreCase("brighter") || userText.containsIgnoreCase("warmer") ||
                          userText.containsIgnoreCase("darker");
            }
        }
    }
    data.patchIsMerge = isMerge;

    // The human-readable preview — the PatchCard's default view. Computed from before/after
    // AIStateMapper::graphToJSON() snapshots (never the raw patch JSON): see PatchDiff.h for why
    // that's the only correct way to preview merge-mode auto-wiring, replace-mode deletions, and
    // value rescaling. Merge mode has stable node identity to diff against, so it gets
    // computeDiff(); replace mode does not (PatchDiff.h), so it gets summarizePatch() of the new
    // patch's contents instead — never a diff against the old graph.
    juce::var diffBefore, diffAfter;
    data.patchDiffAvailable = aiService.computePatchPreview(data.jsonPatch, isMerge, diffBefore, diffAfter);
    if (data.patchDiffAvailable) {
        if (isMerge)
            data.patchDiff = computeDiff(diffBefore, diffAfter);
        else
            data.patchSummary = summarizePatch(diffAfter);
    }
}

} // namespace synth
