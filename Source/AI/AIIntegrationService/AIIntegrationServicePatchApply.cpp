// Patch apply & retry: schema/structural validation of an incoming patch, the undoable apply
// itself, the model-correction retry loop, and the scratch-graph preview/replay helpers they share.
#include "AI/PatchEval.h"
#include "AIIntegrationService.h"
#include "AppUndoManager.h"

namespace synth {

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

juce::String AIIntegrationService::getPatchContext() {
    juce::var json = AIStateMapper::graphToJSON(audioGraph);
    return juce::JSON::toString(json);
}

} // namespace synth
