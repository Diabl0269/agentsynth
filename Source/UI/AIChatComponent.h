#pragma once

#include "../AI/AIIntegrationService.h"
#include "../AI/AccountService.h"
#include "../AI/ConversationHistorySource.h"
#include "../AI/LocalHistoryStore.h"
#include "../AI/PatchDiff.h"
#include "../AI/PatchFeedbackStore.h"
#include "AccountRow.h"
#include "PlanBadge.h"
#include "Theme/AppLookAndFeel.h"
#include "UIAnimation.h"
#include <atomic>
#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace synth {

/**
 * @class AIChatComponent
 * @brief Enhanced chat interface with collapsible patch cards and loading indicators.
 *
 * Phase 5 additions:
 *  - Cancel button: visible only while waiting; aborts in-flight request.
 *  - Animated pulse indicator: a small dot that throbs while waiting, stops on
 *    completion or cancel.  Confined to the input row — no per-tick global repaint.
 *  - Tooltips on all interactive controls.
 */
class AIChatComponent
    : public juce::Component
    , private juce::TextEditor::Listener
    , public juce::Timer
#ifndef NDEBUG
    , private juce::Logger
#endif
{
public:
    AIChatComponent(AIIntegrationService& service, juce::ApplicationProperties& props);
    ~AIChatComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Escape cancels an in-flight request. Reached by bubbling: while waiting, the read-only
    // inputField holds focus and does not consume Escape, so the press walks up to us.
    bool keyPressed(const juce::KeyPress& key) override;

    void refreshModels();
    void sendButtonClicked();
    void triggerSend() { sendButtonClicked(); }

    // Decides whether an outgoing message should carry the live patch JSON + structured-output
    // schema (see AIIntegrationService::sendMessage). Pure and free-standing (no UI state) so it
    // can be unit-tested directly: any message naming a real module/effect type, or using a
    // generic edit-intent verb, is treated as patch-related. `moduleTypeNames` is normally
    // AIStateMapper::moduleFactoryTypeNames() — passed in explicitly so a test can supply a
    // synthetic registry without touching the real module factory.
    static bool shouldUseStructuredOutput(const juce::String& text, const juce::StringArray& moduleTypeNames);

    /**
     * @brief Attaches (or detaches, with nullptr) the account UI to `service`.
     *
     * Non-owning, nullable — mirrors AIIntegrationService::setUndoManager()'s precedent
     * ("constructing without one keeps the old ... behaviour"). Forwards to
     * accountRow.setAccountService() and — per the single-owner-per-callback-slot rule below —
     * is the ONLY place that sets AccountService::onStateChanged / onAccessTokenChanged. Those
     * are single-slot std::function members, not a multicast listener list, so a second caller
     * anywhere that also assigned them would silently steal this one's callback. AccountRow and
     * SignInDialog never touch those slots themselves; they are notified by AccountRow::refresh()
     * (called from the onStateChanged lambda installed here), which forwards on to any open
     * SignInDialog. Only MainComponent calls this, exactly once, after construction.
     */
    void setAccountService(AccountService* service);

    // Testing hook: directly invokes the cancel action synchronously (mirrors triggerSend).
    // Use in headless tests instead of cancelButton->triggerClick(), which posts async.
    void simulateCancelClick() { handleUserCancel(); }

    // Testing hook: the Escape-key path, without needing real keyboard focus.
    void simulateEscapeKey() { keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); }

    // Testing hook: returns true if the cancel button is currently visible.
    bool isCancelVisible() const { return cancelButton.isVisible(); }

    // Testing hook: returns the current isWaitingForResponse flag.
    bool isWaiting() const { return isWaitingForResponse; }

    // Testing hook: responseMs of the most recent assistant message, or -1 if none / unmarked.
    int getLastAssistantResponseMs() const;

    // Compact wait-time label for the AI role row ("340ms", "1.2s", "1m 5s").
    static juce::String formatResponseTime(int ms);

    // Testing hook: text of the in-flight "AI is thinking..." status label, or empty when not waiting.
    juce::String getWaitingStatusText() const;

    // Testing hook: replaces the real "open in default browser" action a Quota error's Upgrade
    // button invokes, so tests can assert on the URL without ever launching a real browser.
    void setUrlOpenerForTesting(std::function<void(const juce::URL&)> opener) { urlOpener = std::move(opener); }

    // Testing hook: redirects the local feedback log to a caller-supplied file so tests never
    // touch the real per-user app-data location. Mirrors setUrlOpenerForTesting.
    void setPatchFeedbackFileForTesting(const juce::File& file) { patchFeedbackStore = PatchFeedbackStore(file); }

    // Testing hook: redirects local chat-history persistence (LocalHistoryStore) to a
    // caller-supplied directory so tests never touch the real per-user app-data location. Mirrors
    // setPatchFeedbackFileForTesting.
    void setLocalHistoryDirectoryForTesting(const juce::File& dir) { localHistoryDirOverride = dir; }

    // Testing hook: replaces the real local/cloud history backends (historyButtonClicked() would
    // otherwise construct a LocalHistorySource against the real directory, and a CloudHistorySource
    // that makes a real HTTP call) with fakes, so tests can assert which backend a given
    // AccountSnapshot routes to without touching disk or the network. Either may be null; a null
    // cloud source means historyButtonClicked() falls back to a no-op stand-in that reports
    // ok=false (mirrors "signed in but the request failed" rather than crashing).
    void setHistorySourcesForTesting(std::unique_ptr<ConversationHistorySource> local,
                                     std::unique_ptr<ConversationHistorySource> cloud) {
        testLocalHistorySource = std::move(local);
        testCloudHistorySource = std::move(cloud);
    }

    // Testing hook: the History button's click handler, exposed synchronously (mirrors
    // simulateCancelClick()/triggerSend()) so tests can drive the real list/restore/downgrade-date
    // path without a real click event.
    void simulateHistoryButtonClick() { historyButtonClicked(); }

    // Testing hook: true once historyButtonClicked() has shown a popup (real UI is skipped in
    // headless tests — juce::PopupMenu can't be driven from a test without a real event loop — but
    // this still proves the list/backend-selection logic ran, which is what these tests assert).
    bool didShowHistoryPopupForTesting() const { return lastHistoryPopupShown; }
    bool lastHistoryPopupWasCloudForTesting() const { return lastHistoryPopupWasCloud; }
    const std::vector<LocalConversationSummary>& lastHistoryListForTesting() const { return lastHistoryList; }

    // Testing hook: historyButton's tooltip, which carries the upsell strip's explanatory text
    // whenever !isProPlan(snapshot) — see updateUpsellStrip()'s doc comment. Not const: JUCE's
    // Button::getTooltip() (via SettableTooltipClient) isn't a const member function.
    juce::String getHistoryButtonTooltipForTesting() { return historyButton.getTooltip(); }

    // Testing hook: the "Clear my history" confirmation-menu-item handler, exposed synchronously.
    void simulateClearHistoryConfirmed() { performClearHistory(); }

    // Testing hook: restoring a specific history-popup entry, exposed synchronously — a real
    // juce::PopupMenu item click can't be driven headlessly (see showHistoryPopup()'s doc comment
    // on didShowHistoryPopupForTesting()), so tests drive the same restoreConversation() a real
    // click would.
    void simulateRestoreConversationForTesting(const juce::String& id, bool isCloud) {
        restoreConversation(id, isCloud);
    }

private:
    void timerCallback() override;

    // Refreshes the in-flight thinking label with the current elapsed wait (no full redraw).
    void refreshWaitingStatusLabel();

    // Stops the in-flight request and resets all waiting state.
    // Called both by the cancel button and by the timeout path.
    void cancelRequest();

    // The user-initiated cancel: cancelRequest() plus the "Cancelled." bubble and focus restore.
    // Shared by the Cancel button and the Escape key so the two cannot drift.
    void handleUserCancel();

    class MessageBubble;
    class PatchCard;
    // Sibling of PatchCard — same card conventions, one payload type over.
    class TimelineCard;

    // ---- Spinner component -----------------------------------------------
    // A tiny 8×8 dot that pulses (alpha 0.3→1.0→0.3) via AnimationDriver.
    // Confined to a fixed region in the input row.  Only animates while
    // isWaitingForResponse is true.  ABSOLUTELY NO writeToLog/DBG inside paint().
    class SpinnerDot : public juce::Component {
    public:
        SpinnerDot() = default;

        void paint(juce::Graphics& g) override {
            using synth::theme::AppLookAndFeel;
            auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
            juce::Colour accentColor = (lf != nullptr) ? lf->getTheme().colors.accent : juce::Colours::lightblue;
            g.setColour(accentColor.withAlpha(currentAlpha));
            g.fillEllipse(getLocalBounds().toFloat().reduced(1.0f));
        }

        // Call once to start pulsing.  Caller must hold vblankUpdater alive for the
        // entire pulsing lifetime.  The pointer is stored so the recursive onComplete
        // callback can restart without a dangling reference to the function parameter.
        void startPulse(juce::VBlankAnimatorUpdater& updater) {
            updaterPtr = &updater;
            pulseAnim.start(
                updater,
                600.0,                     // 600 ms per half-cycle
                [](float t) { return t; }, // linear — easing handled below
                [this](float t) {
                    // Ping-pong: 0→1 on even passes, 1→0 on odd passes.
                    currentAlpha = pingPongPhase ? juce::jmap(t, 1.0f, 0.3f) : juce::jmap(t, 0.3f, 1.0f);
                    repaint();
                },
                [this]() {
                    pingPongPhase = !pingPongPhase;
                    // Only keep pulsing while waiting; caller sets visible(false) on stop.
                    if (isVisible() && updaterPtr != nullptr)
                        startPulse(*updaterPtr);
                });
        }

        void stopPulse(juce::VBlankAnimatorUpdater& updater) {
            updaterPtr = nullptr;
            pulseAnim.stop(updater);
            currentAlpha = 0.0f;
            repaint();
        }

    private:
        synth::ui::AnimationDriver pulseAnim;
        juce::VBlankAnimatorUpdater* updaterPtr = nullptr;
        float currentAlpha = 0.3f;
        bool pingPongPhase = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpinnerDot)
    };
    // ----------------------------------------------------------------------

    AIIntegrationService& aiService;
    juce::ApplicationProperties& appProperties;
    bool isWaitingForResponse = false;

    // Wall-clock start of the in-flight wait (juce::Time::getMillisecondCounter).
    // Meaningful only while isWaitingForResponse is true.
    uint32_t requestStartMs = 0;

    // Non-owning pointer to the "AI is thinking..." label in messageList (owned by messageList).
    // Null whenever not waiting. Cleared before messageList.deleteAllChildren().
    juce::Label* waitingStatusLabel = nullptr;

    static constexpr int kRequestTimeoutMs = 120000;
    // Tick rate for the live thinking timer — updates the status label only (not a full-panel
    // repaint). Matches the AI-thinking spinner exception in the UI perf contract.
    static constexpr int kWaitingStatusIntervalMs = 500;

    // Non-owning; set (once, by MainComponent) via setAccountService(). Held only so the
    // destructor can clear the two callback slots it installs on the service — see
    // setAccountService()'s comment for the single-owner contract those slots are under.
    AccountService* accountServicePtr = nullptr;
    AccountRow accountRow;
    PlanBadge planBadge;

    // Handle for the request currently in flight, so cancelRequest() can tell the provider to
    // actually abandon it. Default (value 0) whenever nothing is outstanding — cleared by the
    // response handler before teardown so a completed request is never "cancelled".
    AIProvider::RequestId activeRequestId{};

    juce::Viewport viewport;
    juce::Component messageList;
    juce::TextEditor inputField;
    juce::TextButton sendButton;
    juce::TextButton cancelButton; // Visible ONLY while waiting
    juce::TextButton newChatButton;
    juce::TextButton historyButton; // P6-8: opens the history list/restore/clear popup
    juce::ComboBox modelPicker;

    // P4-6 privacy disclosure: visible only while the active provider is hosted (RemoteProvider).
    // Zero-height/invisible otherwise, same contract as accountRow/planBadge below it in the
    // bottom-chrome stack — see updateHostedModeNotice() and resized().
    juce::Label hostedModeNotice;

    // P6-8 upsell strip: an "Upgrade to Pro" button, shown whenever !isProPlan(snapshot) —
    // including with NO AccountService attached at all, which deliberately diverges from
    // accountRow/planBadge/hostedModeNotice's "invisible until a service says otherwise"
    // convention: those default to invisible because they have nothing true to say yet, but "not
    // Pro" is already true before any AccountService is ever attached (every caller starts on the
    // free tier, signed out). The explanatory text ("Your history is saved locally only —
    // subscribers get automatic cloud backup across devices.") lives on historyButton's tooltip
    // instead of a separate label, so it doesn't compete for space in the bottom-chrome stack —
    // see updateUpsellStrip().
    juce::TextButton upsellButton;

    // P6-8 downgrade notice: "Your subscription has lapsed — your saved history will be deleted on
    // {date}." Shown only once signed in, !isProPlan(snapshot), AND a grace-period deletion date is
    // known — which is learned ONLY from an explicit History-button click (listConversations()
    // writes on read server-side, see docs/AI_Engine.md — this must not be polled). See
    // historyButtonClicked()/updateDowngradeStrip().
    juce::Label downgradeStripLabel;
    juce::String lastDeletionScheduledAt; // "" = none known/pending

    // Pulse animation for the "AI is thinking" state indicator.
    // VBlankAnimatorUpdater is attached to this Component.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    SpinnerDot spinnerDot;

    // Opens a Quota error's "Upgrade to Pro" button target. Real default; overridden in tests via
    // setUrlOpenerForTesting() so no test ever launches a real browser.
    std::function<void(const juce::URL&)> urlOpener = [](const juce::URL& u) { u.launchInDefaultBrowser(); };

    // P6-3: local, append-only feedback log — see PatchFeedbackStore's doc comment for why this
    // is client-only for now (no server endpoint exists yet to sync to).
    PatchFeedbackStore patchFeedbackStore;

    void updateChatDisplay();
    void scrollToBottom();

    // Syncs hostedModeNotice's visibility to aiService.isCurrentProviderHosted(). Called
    // synchronously (provider identity is known immediately after setProvider(), no need to wait
    // for the async model fetch) from refreshModels() — the same post-setProvider() resync point
    // documented for model discovery (see CLAUDE.md "AI model discovery ordering").
    void updateHostedModeNotice();

    // P6-3: thumbs up/down on a patch card. `None` is the UI's un-rated default and is never
    // itself written to PatchFeedbackStore — only Up/Down get persisted.
    enum class PatchRatingUiState { None, Up, Down };

    struct MessageData {
        juce::String role;
        juce::String text;
        juce::String jsonPatch;
        bool isExpanded = false;
        // P4-4: true only for a live Quota-error response — renders an "Upgrade to Pro" button on
        // the bubble. Deliberately NOT reconstructed by the history-replay loop in the
        // constructor, so a New Chat or app restart drops it along with the rest of that turn's
        // transient UI state (mirrors how Cancel-button/spinner state is session-only).
        bool showUpgradeAction = false;

        // The TIMELINE half of a suggestion, independent of jsonPatch — a response may carry
        // a patch, a timelineOps envelope, or both, and each gets its own card and its own Apply.
        // `timelineOpsJson` is the raw envelope, re-parsed on Apply; EMPTY when there is nothing
        // appliable, including when the envelope arrived but was rejected by validation (the
        // rejection is still shown, in timelineOpsPreview, rather than dropped). `timelineOpsPreview`
        // is what the card displays: the validated summary, or the reason it was refused.
        juce::String timelineOpsJson;
        juce::String timelineOpsPreview;

        // P6-3: session-scoped UI rating state, same "not reconstructed on replay" precedent as
        // showUpgradeAction just above — the durable record lives in patchFeedbackStore, not here.
        PatchRatingUiState ratingState = PatchRatingUiState::None;
        juce::String ratingComment;

        // Patch diff preview, computed ONCE (attachPatchPreview()) at the point this message is
        // created, not on every updateChatDisplay() re-render — see that method's doc comment.
        // patchIsMerge also pins which mode Apply/Merge will actually use, so it can't drift if
        // the live graph changes while this message is still on screen.
        //
        // Only one of patchDiff/patchSummary is ever populated: merge-mode patches get patchDiff
        // (synth::computeDiff() against the live graph, since a merge has stable node identity to
        // diff against); replace-mode patches get patchSummary (synth::summarizePatch() of just
        // the new patch's contents, since replace mode has no stable node identity to diff against
        // — see PatchDiff.h). Both are empty when patchDiffAvailable is false.
        bool patchIsMerge = false;
        bool patchDiffAvailable = false;
        std::vector<PatchChange> patchDiff;
        PatchSummary patchSummary;

        // Elapsed wait ms for bubbles that ended an in-flight request; -1 = no marker
        // (history restore, patch-retry / apply-failure messages).
        int responseMs = -1;
    };
    std::vector<MessageData> messages;

    // Computes and caches data.patchIsMerge/patchDiff/patchDiffAvailable ONCE, at the point a
    // message carrying a patch is created (every messages.push_back() site that can set
    // jsonPatch) — NOT in updateChatDisplay(), which reruns on every redraw (message arrival,
    // apply result, retry announcement) and would otherwise rebuild a scratch graph per
    // patch-bearing message on every single one of those redraws. See docs/AI_Engine.md
    // "Patch Diff Preview". Also more correct, not just faster: the diff is a snapshot of the
    // graph at proposal time and must not silently change if the live graph is edited later
    // (e.g. an earlier patch in the same conversation gets applied) while this message is still
    // on screen. No-op when data.jsonPatch is empty. Reads `messages` (must already contain every
    // turn up to and including `data`, for the merge-vs-replace user-intent heuristic) and
    // `aiService`, so it must be called after data is appended to `messages`.
    void attachPatchPreview(MessageData& data);

    // P6-8: this session's local conversation identity — minted lazily on the first successful
    // exchange (saveCurrentConversationLocally()), not at construction, so a session that never
    // sends a message never creates an empty history file. Also adopted by restoreConversation()
    // so that restoring an earlier conversation makes subsequent local (and, for a Pro restore,
    // cloud) saves continue appending to THAT conversation rather than starting a new one.
    juce::String currentLocalConversationId;
    juce::String currentLocalConversationCreatedAt;

    // Testing hook storage for setLocalHistoryDirectoryForTesting()/setHistorySourcesForTesting().
    juce::File localHistoryDirOverride; // invalid File() = use LocalHistoryStore::getDefaultHistoryDirectory()
    std::unique_ptr<ConversationHistorySource> testLocalHistorySource;
    std::unique_ptr<ConversationHistorySource> testCloudHistorySource;

    // Testing observation hooks for historyButtonClicked() — see didShowHistoryPopupForTesting().
    bool lastHistoryPopupShown = false;
    bool lastHistoryPopupWasCloud = false;
    std::vector<LocalConversationSummary> lastHistoryList;

    // P6-8: syncs upsellButton's visibility, and historyButton's tooltip, to !isProPlan(snapshot)
    // (true whenever there is no AccountService at all — see upsellButton's member doc comment).
    // Called from setAccountService()'s onStateChanged handler and once at construction.
    void updateUpsellStrip();

    // P6-8: syncs downgradeStripLabel visibility/text to (signed in && !isProPlan &&
    // lastDeletionScheduledAt.isNotEmpty()). Called from historyButtonClicked()'s cloud callback —
    // NOT from onStateChanged, since the date it renders is only ever learned from an explicit
    // History click (see lastDeletionScheduledAt's doc comment).
    void updateDowngradeStrip();

    // P6-8: rebuilds `messages` from a flat (role, content) sequence — content still carries any
    // fenced ```json block unsplit, exactly like AIProvider::Message::content / a stored
    // LocalConversationMessage/AuthClient::ConversationMessage. Shared by the constructor's
    // aiService.getHistory() replay and restoreConversation()'s history-panel replay, so the two
    // can never drift.
    void replayMessagesFrom(const std::vector<std::pair<juce::String, juce::String>>& roleContentPairs);

    // P6-8: reconstructs `content` for one MessageData the same way replayMessagesFrom() expects to
    // consume it back (text, plus a re-fenced ```json block when jsonPatch is non-empty).
    static juce::String reconstructMessageContent(const MessageData& data);

    // P6-8: builds this session's LocalConversation snapshot from `messages` and writes it via
    // LocalHistoryStore::save(), minting currentLocalConversationId/CreatedAt on first call. Called
    // once per successful exchange, right after the assistant turn is appended to `messages` (see
    // sendButtonClicked()) — deliberately NOT from AIIntegrationService::sendMessage()'s own
    // callback, which can run on a provider worker thread and doesn't have `messages` (the UI's
    // own copy, split into text/jsonPatch) to hand.
    void saveCurrentConversationLocally();

    // P6-8: first user message's text, trimmed/truncated — the title stored alongside a locally
    // saved conversation. Empty `messages` (shouldn't happen when this is called, but defensively)
    // yields "New Conversation" rather than an empty title, which would render as a blank row in
    // the history popup.
    juce::String deriveConversationTitle() const;

    // P6-8: the directory saveCurrentConversationLocally()/historyButtonClicked() read/write
    // through — localHistoryDirOverride when set (tests), else the real per-user location.
    juce::File resolveLocalHistoryDirectory() const;

    // P6-8: History button handler. Always sources the LOCAL list when !isProPlan(snapshot) or no
    // AccountService/not signed in. When signed in, ALSO fires a cloud listConversations() call
    // regardless of plan — that call is the only source of a pending grace-period deletion date
    // (see lastDeletionScheduledAt), and when the plan IS Pro its result is the list itself. Never
    // called speculatively/on a timer — see ListConversationsResult's read-writes-on-read caveat in
    // docs/AI_Engine.md.
    void historyButtonClicked();

    // P6-8: builds a juce::PopupMenu from `list` (a "Clear my history" item plus one row per
    // conversation, titled with its readable updatedAt) and shows it. `isCloud` records which
    // backend to call get()/restore against when an item is picked.
    void showHistoryPopup(std::vector<LocalConversationSummary> list, bool isCloud);

    // P6-8: fetches the full conversation (from whichever backend showHistoryPopup() was built
    // against) and replays it into `messages` via replayMessagesFrom(). Also clears aiService's
    // own chatHistory (aiService.clearHistory()) and adopts `id` as currentLocalConversationId, so
    // subsequent exchanges in this session continue THIS conversation locally; for a cloud (Pro)
    // restore, also calls aiService.setConversationId(id) so the server continues the same thread.
    // Deliberately does NOT attempt to re-seed aiService's chatHistory with the restored turns
    // (there is no API for that — see docs/AI_Engine.md's "Local History (P6-8)" section), so the
    // model has no memory of the restored conversation until new turns accumulate.
    void restoreConversation(const juce::String& id, bool isCloud);

    // P6-8: "Clear my history" — shows an AlertWindow confirmation (mirrors ShortcutsSettingsTab's
    // "Reset to Defaults" pattern), then calls performClearHistory().
    void confirmAndClearHistory();

    // P6-8: the actual delete, wired to the same backend historyButtonClicked() last populated the
    // popup from (local deleteAll() when !isProPlan, cloud deleteAllConversations() when Pro).
    void performClearHistory();

    // P6-8: returns the backend to use for this call — the test double from
    // setHistorySourcesForTesting() when one was installed, otherwise a freshly constructed real
    // implementation OWNED BY `fallbackStorage` (a unique_ptr living in the CALLER's stack frame).
    // Every ConversationHistorySource implementation here only touches its own state during the
    // synchronous portion of list()/get()/deleteAll() (CloudHistorySource copies what its
    // background thread needs before returning — see its class comment), so it's safe for
    // `fallbackStorage` to go out of scope immediately after that one call completes; nothing
    // holds onto the returned pointer past it.
    ConversationHistorySource* resolveLocalHistorySource(std::unique_ptr<ConversationHistorySource>& fallbackStorage);
    ConversationHistorySource* resolveCloudHistorySource(std::unique_ptr<ConversationHistorySource>& fallbackStorage);

#ifndef NDEBUG
    juce::TextEditor debugConsole;
    juce::TextButton toggleDebugButton;
    bool debugConsoleVisible = false;
    void logMessage(const juce::String& message) override;
    void flushDebugLog();

    juce::CriticalSection logLock;
    juce::StringArray pendingLogLines;
    std::atomic<bool> logFlushScheduled{false};

public:
    void appendDebugLog(const juce::String& msg);

private:
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatComponent)
};

} // namespace synth
