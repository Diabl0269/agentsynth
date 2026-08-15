#pragma once

#include "AIProvider.h"
#include "AIStateMapper.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

class AppUndoManager; // Forward declaration — the service only holds a non-owning pointer.

namespace synth {

/**
 * @class AIIntegrationService
 * @brief Orchestrates AI interactions and bridges them with the synth engine.
 */
class AIIntegrationService {
public:
    /**
     * @param graph The graph AI patches are applied to.
     * @param undoManager Optional — when supplied, applyPatch() becomes undoable. Defaults to null so
     *                    callers that don't own an undo manager (e.g. tests) keep working unchanged.
     */
    AIIntegrationService(juce::AudioProcessorGraph& graph, AppUndoManager* undoManager = nullptr);
    ~AIIntegrationService();

    /**
     * @brief Installs (or clears) the undo manager used to make applyPatch() undoable.
     */
    void setUndoManager(AppUndoManager* um) { undoManager = um; }

    /**
     * @brief Sets (or clears) the bearer token forwarded to the active provider's
     *        AIProvider::setAuthToken().
     *
     * Stored regardless of whether a provider is currently installed — setProvider() re-pushes
     * it to whatever provider it installs next, mirroring the model-discovery re-push contract
     * documented for this class (see docs/AI_Engine.md "Model Discovery Ordering Contract"):
     * AIChatComponent/AccountService can be wired up before MainComponent::initialiseCommon()
     * installs the real provider, so a value set first must not be lost.
     */
    void setAuthToken(const juce::String& token);

    /**
     * @class Listener
     * @brief Interface for observing AI-driven changes to the synthesizer state.
     */
    class Listener {
    public:
        virtual ~Listener() = default;
        // Called BEFORE the patch is applied to the graph — listeners must tear down any UI that holds
        // references into the graph's processors (e.g. detach module components so ScopeComponent timers
        // stop) before the old processors/VisualBuffers are freed. Default no-op for listeners that don't.
        virtual void aiPatchAboutToApply() {}
        virtual void aiPatchApplied() = 0;
    };

    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    void setProvider(std::unique_ptr<AIProvider> newProvider);

    /**
     * @brief Maximum number of retained user/assistant turn pairs, beyond the system prompt.
     *        Oldest pairs are trimmed first once this cap is exceeded.
     */
    static constexpr int kMaxHistoryTurns = 8;

    /**
     * @brief Sends a user message and gets a response.
     */
    AIProvider::RequestId sendMessage(const juce::String& text, AIProvider::CompletionCallback callback,
                                      bool useStructuredOutput = false);

    /**
     * @brief Abandons an in-flight request obtained from sendMessage().
     *
     * The caller's callback still fires exactly once, with AIErrorKind::Cancelled, and no
     * assistant turn is added to the history. A stale or unknown handle is a safe no-op.
     */
    void cancelRequest(AIProvider::RequestId requestId);

    /**
     * @brief Applies a JSON patch to the graph.
     */
    bool applyPatch(const juce::String& jsonString, bool mergeMode = false);

    /**
     * @brief Computes the before/after graph snapshots a proposed patch would produce, WITHOUT
     *        applying anything to the live graph — the basis for the chat UI's diff preview.
     *
     * `before` is always the live graph's current AIStateMapper::graphToJSON(). `after` is
     * graphToJSON() of a scratch graph the patch was actually applied to (merge mode first
     * trusted-replays the live graph into that scratch, exactly like applyPatch()'s own
     * PatchEval regression check, so pre-existing nodes keep their live id/uuid). Diffing these
     * two snapshots — never the raw patch JSON — is what makes the preview correct: it is the
     * only way to see merge mode's auto-wiring of new nodes, replace mode's implicit deletion of
     * everything the patch doesn't restate, and the untrusted-apply [0,1] rescale heuristic. See
     * PatchDiff.h.
     *
     * Mirrors applyPatch()'s mode-less-patch repair (a replace that only validates as a merge is
     * applied as a merge) so the previewed diff matches what Apply/Merge will actually do.
     *
     * @return true if the patch applied cleanly to the scratch graph (matching what applyPatch()
     *         would report on a fresh live graph); false if it failed validation or application —
     *         `before`/`after` are still populated (after reflects the unapplied, pre-patch
     *         state) so a caller can still show "preview unavailable" using the same values.
     *         Does NOT touch getLastPatchError()/getLastPatchErrorCode()/didLastPatchRepairMode()
     *         — this never mutates the graph the user is looking at, so it must not clobber the
     *         error state from a previous real Apply attempt.
     */
    bool computePatchPreview(const juce::String& jsonString, bool mergeMode, juce::var& before, juce::var& after);

    /**
     * @brief How many correction round-trips applyPatchWithRetry() may make after the first
     *        rejected patch. Total attempts are kMaxPatchRetries + 1.
     *
     * Deliberately small. Each retry is a full model round-trip the user is waiting on, and a
     * model that has failed twice on the same stated reason is not usually one more nudge away
     * from success — surfacing the error beats spinning.
     */
    static constexpr int kMaxPatchRetries = 2;

    /**
     * @brief Reported before each correction round-trip, so the UI can show that a retry is
     *        happening and why, instead of appearing to hang.
     */
    struct PatchRetryInfo {
        int failedAttempt = 0; // 1-based index of the attempt that was just rejected
        int totalAttempts = 0; // kMaxPatchRetries + 1
        juce::String error;    // validation message being sent back to the model
    };

    using PatchApplyCallback = std::function<void(bool success, const juce::String& error)>;
    using PatchRetryCallback = std::function<void(const PatchRetryInfo&)>;

    /**
     * @brief Applies a patch; on a validation failure, asks the model to correct it and retries.
     *
     * The specific validation message is fed back to the model ("that patch was rejected
     * because X"), which is the whole point — a bare "try again" tends to reproduce the same
     * mistake. Retries are bounded by kMaxPatchRetries and each one is announced through
     * `onRetry`; when they run out, `onComplete` reports the last error rather than looping.
     *
     * `onComplete` is invoked exactly once. With no provider installed, or when the very first
     * attempt succeeds, it is invoked synchronously.
     */
    void applyPatchWithRetry(const juce::String& jsonString, bool mergeMode, PatchApplyCallback onComplete,
                             PatchRetryCallback onRetry = {});

    /**
     * @brief Why the most recent applyPatch() returned false, in human-readable form.
     *
     * Empty when the last apply succeeded. Callers MUST surface this — a rejected patch that is
     * swallowed silently looks to the user like a dead Apply/Merge button.
     */
    const juce::String& getLastPatchError() const { return lastPatchError; }

    /**
     * @brief The typed reason the most recent applyPatch() returned false.
     *
     * PatchValidationError::None when the last apply succeeded, or when it failed inside
     * applyJSONToGraph rather than validation. Callers that need to react by category
     * (retry, repair, give up) should switch on this rather than parse getLastPatchError().
     */
    PatchValidationError getLastPatchErrorCode() const { return lastPatchErrorCode; }

    /**
     * @brief Whether the most recent applyPatch() reinterpreted a mode-less patch as a merge.
     *
     * See applyPatch(): the repair only ever turns a rejected *replace* into a *merge* (never the
     * destructive direction), only when the model stated no "mode", and only when validation
     * accepts the patch that way.
     */
    bool didLastPatchRepairMode() const { return lastPatchModeRepaired; }

    /**
     * @brief Extracts the JSON payload from a model response that may wrap it in prose or fences.
     *
     * Public and static so the offline measurement harness can reproduce exactly the extraction
     * applyPatch() performs, rather than approximating it.
     */
    static juce::String extractJsonFromResponse(const juce::String& response);

    /**
     * @brief Returns the current graph state as a JSON string for context.
     */
    juce::String getPatchContext();

    /**
     * @brief Returns the chat history.
     */
    const std::vector<AIProvider::Message>& getHistory() const { return chatHistory; }

    /**
     * @brief Clears the chat history (except system prompt).
     */
    void clearHistory();

    /**
     * @brief Model management methods.
     */
    void setModel(const juce::String& name);
    juce::String getCurrentModel() const;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback);

    /**
     * @brief True when the active provider sends the prompt/patch to a remote/hosted server
     *        (RemoteProvider). False for a local provider (Ollama) or when none is installed yet.
     */
    bool isCurrentProviderHosted() const { return provider != nullptr && provider->isHosted(); }

private:
    std::unique_ptr<AIProvider> provider;
    std::vector<AIProvider::Message> chatHistory;
    juce::AudioProcessorGraph& audioGraph;
    AppUndoManager* undoManager = nullptr;
    juce::String currentAuthToken;
    juce::String lastPatchError;
    PatchValidationError lastPatchErrorCode = PatchValidationError::None;
    bool lastPatchModeRepaired = false;
    juce::ListenerList<Listener> listeners;

    void initSystemPrompt();

    /**
     * @brief Builds the patch-augmented request content for a user message, without mutating chatHistory.
     */
    juce::String buildPatchAugmentedContent(const juce::String& text);

    /**
     * @brief Trims chatHistory to the system prompt plus the most recent kMaxHistoryTurns pairs,
     *        removing oldest whole user+assistant pairs so history never starts on an assistant turn.
     */
    void trimHistory();

    /**
     * @brief One correction round-trip of applyPatchWithRetry(), recursing until the patch
     *        applies or `failedAttempt` reaches kMaxPatchRetries + 1.
     */
    void requestPatchCorrection(int failedAttempt, bool mergeMode, const juce::String& originalRequest,
                                PatchApplyCallback onComplete, PatchRetryCallback onRetry);

    /**
     * @brief The message sent back to the model naming the specific validation failure.
     *
     * Restates `originalRequest` explicitly rather than relying on conversation history to carry
     * it: RemoteProvider (Source/AI/RemoteProvider.h) sends only the last message, so a correction
     * turn with no restated intent reaches the model as a bare "fix this JSON" with no idea what
     * the patch was even supposed to be — confirmed live: the model invents an unrelated patch
     * referencing node ids that don't exist anywhere. OllamaProvider already sends full history, so
     * this is redundant-but-harmless there.
     */
    static juce::String buildCorrectionPrompt(const juce::String& originalRequest, const juce::String& error);

    /**
     * @brief The most recent user-authored chat turn, so a correction round-trip can restate what
     *        the patch being corrected was actually for. Captured once at the start of
     *        applyPatchWithRetry() — before any correction turns are appended to chatHistory — and
     *        threaded through requestPatchCorrection()'s recursion rather than re-derived on each
     *        retry, so a second retry doesn't mistake the first retry's own correction text for the
     *        original request.
     */
    juce::String mostRecentUserRequest() const;

    /**
     * @brief Whether the patch states a non-empty "mode", i.e. the model expressed an intent
     *        that the mode repair in applyPatch() must not override.
     */
    static bool hasExplicitMode(const juce::var& json);

    /**
     * @brief Trusted-replays the live graph's current AIStateMapper::graphToJSON() into `scratch`
     *        (clearExisting=true, trusted=true) — the shared first step for building a scratch
     *        graph that starts as an exact copy of the live one, used by both applyPatch()'s
     *        PatchEval regression check and computePatchPreview(). Only meaningful for merge mode:
     *        a replace-mode candidate patch has no "before" to build on top of.
     */
    void replayLiveGraphTrusted(juce::AudioProcessorGraph& scratch) const;

    JUCE_DECLARE_WEAK_REFERENCEABLE(AIIntegrationService)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIIntegrationService)
};

} // namespace synth
