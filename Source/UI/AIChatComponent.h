#pragma once

#include "../AI/AIIntegrationService.h"
#include "../AI/AccountService.h"
#include "../AI/PatchDiff.h"
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

    // Testing hook: replaces the real "open in default browser" action a Quota error's Upgrade
    // button invokes, so tests can assert on the URL without ever launching a real browser.
    void setUrlOpenerForTesting(std::function<void(const juce::URL&)> opener) { urlOpener = std::move(opener); }

private:
    void timerCallback() override;

    // Stops the in-flight request and resets all waiting state.
    // Called both by the cancel button and by the timeout path.
    void cancelRequest();

    // The user-initiated cancel: cancelRequest() plus the "Cancelled." bubble and focus restore.
    // Shared by the Cancel button and the Escape key so the two cannot drift.
    void handleUserCancel();

    class MessageBubble;
    class PatchCard;

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
    juce::ComboBox modelPicker;

    // P4-6 privacy disclosure: visible only while the active provider is hosted (RemoteProvider).
    // Zero-height/invisible otherwise, same contract as accountRow/planBadge below it in the
    // bottom-chrome stack — see updateHostedModeNotice() and resized().
    juce::Label hostedModeNotice;

    // Pulse animation for the "AI is thinking" state indicator.
    // VBlankAnimatorUpdater is attached to this Component.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    SpinnerDot spinnerDot;

    // Opens a Quota error's "Upgrade to Pro" button target. Real default; overridden in tests via
    // setUrlOpenerForTesting() so no test ever launches a real browser.
    std::function<void(const juce::URL&)> urlOpener = [](const juce::URL& u) { u.launchInDefaultBrowser(); };

    void updateChatDisplay();
    void scrollToBottom();

    // Syncs hostedModeNotice's visibility to aiService.isCurrentProviderHosted(). Called
    // synchronously (provider identity is known immediately after setProvider(), no need to wait
    // for the async model fetch) from refreshModels() — the same post-setProvider() resync point
    // documented for model discovery (see CLAUDE.md "AI model discovery ordering").
    void updateHostedModeNotice();

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
