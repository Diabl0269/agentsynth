#pragma once

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

#include "Timeline/ArrangementContext.h"
#include "Timeline/TimelineOps.h"
#include "Transport/TransportService.h"

class AppUndoManager; // Forward declaration — the service only holds a non-owning pointer.

namespace synth {

/**
 * @class AIIntegrationService
 * @brief Orchestrates AI interactions and bridges them with the synth engine.
 */
class AIIntegrationService {
public:
    /** @param undoManager Optional — when supplied, applyPatch() becomes undoable. Defaults to null
     *  so callers that don't own an undo manager (e.g. tests) keep working unchanged. */
    AIIntegrationService(juce::AudioProcessorGraph& graph, AppUndoManager* undoManager = nullptr);
    ~AIIntegrationService();

    /** Installs (or clears) the undo manager used to make applyPatch() undoable. */
    void setUndoManager(AppUndoManager* um) { undoManager = um; }

    /** Sets (or clears) the bearer token forwarded to the active provider. Safe to call before a
     *  provider is installed — the value is re-pushed once one is (see setProvider()). */
    void setAuthToken(const juce::String& token);

    /** Sets the request timeout, in milliseconds, forwarded to the active provider. Same re-push
     *  contract as setAuthToken()/setConversationId() — safe to call before a provider exists. */
    void setRequestTimeoutMs(int timeoutMs);

    /** The currently configured request timeout, in milliseconds. Defaults to 240000 (4 minutes)
     *  until changed via setRequestTimeoutMs(). */
    int getRequestTimeoutMs() const { return currentRequestTimeoutMs; }

    /** Installs (or clears) the timeline/transport this service reads for arrangement context.
     *  Non-owning — MainComponent owns both for the app's lifetime.
     *
     *  Mirrors setProvider()/setUndoManager(): a plain pointer setter, safe to call with either
     *  argument null (arrangement context is then simply omitted from the outgoing request, same
     *  as an empty TimelineDoc would produce). */
    void setTimelineContext(const TimelineDoc* doc, const TransportService* transport) {
        timelineDoc = doc;
        transportService = transport;
        refreshSystemPrompt(); // the timeline tool section is gated on context being present
    }

    /** Switches the LOCAL model's timeline/automation authoring on or off. `MainComponent` sets
     *  this unconditionally on at startup now that the timeline is GA (there is no more
     *  Preferences toggle to drive it). Kept as its own switch, separate from setTimelineContext(),
     *  so tests can flip authoring on/off without standing up or tearing down a timeline context.
     *
     *  On (and with a timeline context installed): the system prompt teaches the `timelineOps`
     *  grammar, the structured-output schema handed to the provider grows an optional
     *  `timelineOps` property (AIStateMapper::getPatchSchemaWithTimelineOps), and the outgoing
     *  request's context gains an "Automation targets" section (the (nodeUuid, paramId) pairs
     *  writeLane needs — see buildAutomationTargetsSection). Off: prompt, schema and context are
     *  byte-identical to the pre-timeline behaviour. Extraction/preview/apply stay wired either
     *  way — they act on what a response actually carries, and the Apply gate is the user's. */
    void setTimelineToolsEnabled(bool enabled) {
        if (timelineToolsEnabled == enabled)
            return;
        timelineToolsEnabled = enabled;
        refreshSystemPrompt();
    }
    bool areTimelineToolsEnabled() const { return timelineToolsEnabled; }

    // -- Timeline operations -----------------------------------------------------------------
    // The write half of the timeline seam. A timelineOps envelope is a SIBLING of a patch
    // suggestion, never nested inside one — a "timeline" key inside patch JSON stays refused by
    // validatePatch forever. See AIIntegrationServiceTimelineOps.cpp for the full flow (also
    // documented in docs/AI_Engine_patch_safety.md §9 "Sibling, never nested").

    /** The timelineOps envelope carried by a model response, or a void var if it has none. Static
     *  and public so a harness/test can reproduce the real extraction. */
    static juce::var extractTimelineOps(const juce::String& response);

    /** True once MainComponent has wired the live timeline in (setTimelineContext). Without it
     *  there is nothing to validate against, and timeline suggestions are not offered at all. */
    bool hasTimelineContext() const { return timelineDoc != nullptr; }

    /** Validates an envelope against the live timeline + graph WITHOUT applying it. Fails (applying
     *  nothing, as always) when no timeline context is installed. */
    TimelineOpsResult previewTimelineOps(const juce::var& envelope) const;

    /** Installed by the app-level owner (MainComponent) to route an Apply back to
     *  `TimelineOps::apply` with the real doc, graph and undo manager.
     *
     *  The service holds the timeline only as a CONST pointer (it is a context reader), and it
     *  owns no undo manager for the timeline — so the host supplies the write path, exactly as
     *  AIChatComponent supplies its own urlOpener. With no callback installed, applyTimelineOps()
     *  reports that it cannot apply rather than silently doing nothing. */
    using TimelineOpsApplyCallback = std::function<TimelineOpsResult(const juce::var& envelope)>;
    void setTimelineOpsApplyCallback(TimelineOpsApplyCallback callback) { timelineOpsApply = std::move(callback); }

    /** Applies a previously previewed envelope through the host callback. One undo step. */
    TimelineOpsResult applyTimelineOps(const juce::var& envelope);

    /** Hard cap on how many paramTargets one arrange request offers — mirrors the server's
     *  MAX_PARAM_TARGETS (synth-platform automation-generate/capability.ts): a longer list would be
     *  rejected with a 400 before any model ever saw it. Targets past the cap are dropped from the
     *  tail, in graph order — the same "bound the request, never fail it" posture as
     *  buildAutomationTargetsSection()'s character cap. */
    static constexpr int kMaxRemoteParamTargets = 64;

    /** Sends an arrange-mode request; the answer is a `{"timelineOps": [...]}` envelope consumed by
     *  the same extractTimelineOps() -> previewTimelineOps() -> user-gated Apply flow as any other
     *  timelineOps response — this method adds a way to ASK, never a second way to APPLY. Same
     *  history contract as sendMessage(): the user's raw text is recorded as the user turn (the
     *  composed arrange context exists only on the wire). Fails synchronously, like sendMessage(),
     *  with no provider installed. */
    AIProvider::RequestId sendArrangeMessage(const juce::String& text, AIProvider::CompletionCallback callback);

    /** Builds the `timeline.generate` request body for `text`. Public for the same reason
     *  extractTimelineOps() is: tests and harnesses reproduce the real request rather than
     *  approximating it. See its definition for the field-by-field breakdown. */
    juce::var buildArrangeRequestBody(const juce::String& text) const;

    /** Sets (or clears) the conversation id forwarded to the active provider. Same re-push contract
     *  as setAuthToken(). Callers normally never call this directly — sendMessage() manages it —
     *  except AIChatComponent, which clears it (empty string) on a plan downgrade. */
    void setConversationId(const juce::String& id);

    /** The current server-side conversation id (P6-9), i.e. what setConversationId()/the
     *  conversationId re-push contract above most recently stored — NOT the client's own
     *  local-history id (AIChatComponent::currentLocalConversationId is a different, unrelated
     *  identifier). Empty when nothing has been persisted server-side yet (free plan, or no
     *  successful hosted response so far this session). Used by AIChatComponent's P6-9 rating sync
     *  to key the feedback POST against the right server conversation. */
    juce::String getConversationId() const { return currentConversationId; }

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

    /** Maximum number of retained user/assistant turn pairs, beyond the system prompt. Oldest
     *  pairs are trimmed first once this cap is exceeded. */
    static constexpr int kMaxHistoryTurns = 8;

    /** Sends a user message and gets a response. */
    AIProvider::RequestId sendMessage(const juce::String& text, AIProvider::CompletionCallback callback,
                                      bool useStructuredOutput = false);

    /** Abandons an in-flight request obtained from sendMessage(). The caller's callback still
     *  fires exactly once, with AIErrorKind::Cancelled, and no assistant turn is added to the
     *  history. A stale or unknown handle is a safe no-op. */
    void cancelRequest(AIProvider::RequestId requestId);

    /** Applies a JSON patch to the graph. */
    bool applyPatch(const juce::String& jsonString, bool mergeMode = false);

    /** Computes the before/after graph snapshots a proposed patch would produce, WITHOUT applying
     *  anything to the live graph — the basis for the chat UI's diff preview.
     *
     *  @return true if the patch applied cleanly to the scratch graph (matching what applyPatch()
     *          would report on a fresh live graph); false if it failed validation or application —
     *          `before`/`after` are still populated either way (after reflects the unapplied,
     *          pre-patch state on failure), so a caller can still show "preview unavailable" using
     *          the same values. Never touches getLastPatchError()/getLastPatchErrorCode()/
     *          didLastPatchRepairMode() — this never mutates the graph the user is looking at, so
     *          it must not clobber the error state from a previous real Apply attempt. */
    bool computePatchPreview(const juce::String& jsonString, bool mergeMode, juce::var& before, juce::var& after);

    /** How many correction round-trips applyPatchWithRetry() may make after the first rejected
     *  patch. Total attempts are kMaxPatchRetries + 1.
     *
     *  Deliberately small. Each retry is a full model round-trip the user is waiting on, and a
     *  model that has failed twice on the same stated reason is not usually one more nudge away
     *  from success — surfacing the error beats spinning. */
    static constexpr int kMaxPatchRetries = 2;

    /** Reported before each correction round-trip, so the UI can show that a retry is happening
     *  and why, instead of appearing to hang. */
    struct PatchRetryInfo {
        int failedAttempt = 0; // 1-based index of the attempt that was just rejected
        int totalAttempts = 0; // kMaxPatchRetries + 1
        juce::String error;    // validation message being sent back to the model
    };

    using PatchApplyCallback = std::function<void(bool success, const juce::String& error)>;
    using PatchRetryCallback = std::function<void(const PatchRetryInfo&)>;

    /** Applies a patch; on a validation failure, asks the model to correct it and retries, bounded
     *  by kMaxPatchRetries (each retry announced via `onRetry`). `onComplete` is invoked exactly
     *  once — synchronously when no provider is installed or the first attempt succeeds. */
    void applyPatchWithRetry(const juce::String& jsonString, bool mergeMode, PatchApplyCallback onComplete,
                             PatchRetryCallback onRetry = {});

    /** Why the most recent applyPatch() returned false, in human-readable form.
     *
     *  Empty when the last apply succeeded. Callers MUST surface this — a rejected patch that is
     *  swallowed silently looks to the user like a dead Apply/Merge button. */
    const juce::String& getLastPatchError() const { return lastPatchError; }

    /** The typed reason the most recent applyPatch() returned false.
     *
     *  PatchValidationError::None when the last apply succeeded, or when it failed inside
     *  applyJSONToGraph rather than validation. Callers that need to react by category (retry,
     *  repair, give up) should switch on this rather than parse getLastPatchError(). */
    PatchValidationError getLastPatchErrorCode() const { return lastPatchErrorCode; }

    /** Whether the most recent applyPatch() reinterpreted a mode-less patch as a merge.
     *
     *  See applyPatch(): the repair only ever turns a rejected *replace* into a *merge* (never the
     *  destructive direction), only when the model stated no "mode", and only when validation
     *  accepts the patch that way. */
    bool didLastPatchRepairMode() const { return lastPatchModeRepaired; }

    /** Extracts the JSON payload from a model response that may wrap it in prose or fences. Public
     *  and static so the offline measurement harness can reproduce exactly the extraction
     *  applyPatch() performs, rather than approximating it. */
    static juce::String extractJsonFromResponse(const juce::String& response);

    /** Returns the current graph state as a JSON string for context. */
    juce::String getPatchContext();

    /** Returns the chat history. */
    const std::vector<AIProvider::Message>& getHistory() const { return chatHistory; }

    /** Clears the chat history (except the system prompt). */
    void clearHistory();

    void setModel(const juce::String& name);
    juce::String getCurrentModel() const;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback);

    /** True when the active provider sends the prompt/patch to a remote/hosted server
     *  (RemoteProvider). False for a local provider (Ollama) or when none is installed yet. */
    bool isCurrentProviderHosted() const { return provider != nullptr && provider->isHosted(); }

private:
    std::unique_ptr<AIProvider> provider;
    std::vector<AIProvider::Message> chatHistory;
    juce::AudioProcessorGraph& audioGraph;
    AppUndoManager* undoManager = nullptr;
    juce::String currentAuthToken;
    juce::String currentConversationId;
    int currentRequestTimeoutMs = 240000;
    juce::String lastPatchError;
    PatchValidationError lastPatchErrorCode = PatchValidationError::None;
    bool lastPatchModeRepaired = false;
    juce::ListenerList<Listener> listeners;

    // Non-owning, installed post-construction via setTimelineContext() — see its doc comment.
    // Either or both may be null until setTimelineContext() is called, which is why
    // buildPatchAugmentedContent()'s arrangement section null-checks both before using them.
    const TimelineDoc* timelineDoc = nullptr;
    const TransportService* transportService = nullptr;

    // The host's write path, installed by MainComponent — see setTimelineOpsApplyCallback().
    TimelineOpsApplyCallback timelineOpsApply;

    // The runtime switch behind setTimelineToolsEnabled(). Off by default: the timeline prompt
    // section, schema extension and targets context only exist once the app explicitly opts in.
    bool timelineToolsEnabled = false;

    // The (nodeUuid, paramId, range) inventory a `writeLane` op needs. See
    // AIIntegrationServiceRequestSending.cpp for why uuids are included here despite
    // ArrangementContext's no-uuid rule (also documented in docs/AI_Engine_patch_safety.md §9).
    juce::String buildAutomationTargetsSection() const;

    // One automatable parameter on one addressable node — the shared enumeration behind BOTH
    // renderings of the same inventory: buildAutomationTargetsSection() (the local model's text
    // section) and buildArrangeRequestBody()'s paramTargets (timeline.generate's structured
    // field). One enumeration, two renderings, so the two surfaces can never disagree about which
    // parameters are automatable. min/max/defaultValue are in the parameter's OWN units (the
    // model writes raw values; TimelineOps::validate re-checks against the live range anyway).
    struct AutomationTargetInfo {
        juce::String nodeUuid;
        juce::String nodeName; // display name (AudioProcessor::getName()), same as the text section
        juce::String paramId;
        float min = 0.0f;
        float max = 0.0f;
        float defaultValue = 0.0f;
    };

    // Graph order, unbounded — each caller applies its own cap. See
    // AIIntegrationServiceRequestSending.cpp's definition for what's included/excluded.
    std::vector<AutomationTargetInfo> enumerateAutomationTargets() const;

    // See its definition in AIIntegrationServiceRequestSending.cpp for what this composes.
    juce::String buildArrangeAugmentedContent(const juce::String& text) const;

    // True while the timeline tool surface should be offered to the model: the switch is on AND
    // a timeline context is installed.
    bool timelineToolsActive() const { return timelineToolsEnabled && hasTimelineContext(); }

    void initSystemPrompt();

    /** The full system-message text sent as the system turn. initSystemPrompt() pushes it;
     *  refreshSystemPrompt() swaps it in place (see its own comment). */
    juce::String buildSystemPrompt() const;
    void refreshSystemPrompt();

    /** Builds the patch-augmented request content for a user message, without mutating chatHistory. */
    juce::String buildPatchAugmentedContent(const juce::String& text);

    // Wraps a caller's completion callback with shared success bookkeeping (assistant-turn
    // history, conversation-id re-push). Shared by sendMessage() and sendArrangeMessage().
    AIProvider::CompletionCallback wrapCompletionForHistory(AIProvider::CompletionCallback callback);

    // Keeps chatHistory bounded to kMaxHistoryTurns pairs; see its definition for the invariant it
    // preserves.
    void trimHistory();

    /** One correction round-trip of applyPatchWithRetry(), recursing until the patch applies or
     *  `failedAttempt` reaches kMaxPatchRetries + 1. */
    void requestPatchCorrection(int failedAttempt, bool mergeMode, const juce::String& originalRequest,
                                PatchApplyCallback onComplete, PatchRetryCallback onRetry);

    /** The message sent back to the model naming the specific validation failure and restating
     *  `originalRequest`. See its definition for why restating the request matters. */
    static juce::String buildCorrectionPrompt(const juce::String& originalRequest, const juce::String& error);

    /** The most recent user-authored chat turn, for restating what a correction round-trip's patch
     *  was actually for. */
    juce::String mostRecentUserRequest() const;

    /** Whether the patch states a non-empty "mode" — an intent the mode repair in applyPatch()
     *  must not override. */
    static bool hasExplicitMode(const juce::var& json);

    /** Trusted-replays the live graph's current AIStateMapper::graphToJSON() into `scratch`
     *  (clearExisting=true, trusted=true). See its definition for why that's safe here and where
     *  it's shared. */
    void replayLiveGraphTrusted(juce::AudioProcessorGraph& scratch) const;

    JUCE_DECLARE_WEAK_REFERENCEABLE(AIIntegrationService)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIIntegrationService)
};

} // namespace synth
