#include "AIChatComponent.h"
#include "Branding.h"
#include "UI/Layout/FocusRegion.h"
#include <cmath>

namespace synth {

// Concern: construction/destruction, the request cancel/timeout watchdog, the panel's own
// paint()/paintOverChildren(), and the small testing-hook formatters that don't warrant their
// own unit -- everything left over once the concerns below have their own files.

AIChatComponent::AIChatComponent(AIIntegrationService& service, juce::ApplicationProperties& props)
    : aiService(service)
    , appProperties(props) {
    // T159: makes grabKeyboardFocus() on THIS component (the "aiPanel" focus region's root) succeed
    // deterministically rather than depending on JUCE's position-ordered descent into children
    // finding a focus-wanting one (see the identical comment in ModuleLibraryComponent's ctor).
    setWantsKeyboardFocus(true);

#ifdef NDEBUG
    juce::Logger::writeToLog("AIChatComponent initialized (Release)");
#else
    juce::Logger::writeToLog("AIChatComponent initialized (Debug)");

    // Add debug components first so tests that iterate children find main components last
    debugConsole.setMultiLine(true);
    debugConsole.setReadOnly(true);
    debugConsole.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    debugConsole.setColour(juce::TextEditor::textColourId, juce::Colours::lime);
    debugConsole.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    debugConsole.setVisible(false);
    addChildComponent(debugConsole);

    toggleDebugButton.setButtonText("Debug");
    toggleDebugButton.onClick = [this]() {
        debugConsoleVisible = !debugConsoleVisible;
        debugConsole.setVisible(debugConsoleVisible);
        resized();
    };
    addAndMakeVisible(toggleDebugButton);
    juce::Logger::setCurrentLogger(this);
#endif

    // addChildComponent (NOT addAndMakeVisible): AccountRow starts invisible and stays that way
    // until setAccountService() attaches a real AccountService — addAndMakeVisible would force
    // it visible here and immediately override that default. Visibility from then on is
    // controlled entirely by AccountRow's own internal state, not by this component hiding it,
    // so it plays correctly with AIChatComponent's own top-level setVisible() calls from
    // MainComponent.
    addChildComponent(accountRow);

    // Same addChildComponent (not addAndMakeVisible) rationale as accountRow just above: starts
    // invisible/zero-height and stays that way until setAccountService() attaches a real
    // AccountService with a known entitlement.
    addChildComponent(planBadge);

    addAndMakeVisible(viewport);
    viewport.setScrollBarsShown(true, false);
    viewport.setViewedComponent(&messageList);

    addAndMakeVisible(inputField);
    inputField.setReturnKeyStartsNewLine(false);
    inputField.onReturnKey = [this]() { sendButtonClicked(); };
    // Belt and braces for Escape: keyPressed() catches the bubbled press, but a TextEditor with an
    // onEscapeKey handler consumes it before it can bubble, so the two must agree.
    inputField.onEscapeKey = [this]() {
        if (isWaitingForResponse)
            handleUserCancel();
    };
    inputField.addListener(this);
    inputField.setTextToShowWhenEmpty("Ask AI to create or modify a patch...", juce::Colours::grey);
    inputField.setTooltip("Type a message and press Enter or Send");

    addAndMakeVisible(sendButton);
    sendButton.setButtonText("Send");
    sendButton.onClick = [this]() { sendButtonClicked(); };
    sendButton.setTooltip("Send message to AI  (Enter)");

    // Cancel button — hidden until a request is in flight.
    cancelButton.setButtonText("Cancel");
    cancelButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    cancelButton.onClick = [this]() { handleUserCancel(); };
    cancelButton.setTooltip("Cancel the in-flight AI request");
    cancelButton.setVisible(false);
    addChildComponent(cancelButton);

    // Spinner dot — 8×8 ellipse, hidden until waiting.
    spinnerDot.setSize(8, 8);
    spinnerDot.setVisible(false);
    addChildComponent(spinnerDot);

    addAndMakeVisible(newChatButton);
    newChatButton.setButtonText("New Chat");
    newChatButton.setTooltip("Start a new conversation (clears history)");
    newChatButton.onClick = [this]() {
        aiService.clearHistory();
        messages.clear();
        // A fresh conversation gets a fresh local-history id — the outgoing conversation's file is
        // left alone (its own save() already captured everything up to this point).
        currentLocalConversationId.clear();
        currentLocalConversationCreatedAt.clear();
        // Also clear the CLOUD conversation id (mirrors the same clear sendButtonClicked() does on
        // a Pro-to-Free downgrade): without this, a Pro user's next message after New Chat would
        // still carry the OLD x-conversation-id, so the server would append the "new" chat's turns
        // onto the previous cloud thread while a separate fresh file starts locally — local and
        // cloud silently diverging. The server mints a fresh id on the next response either way.
        aiService.setConversationId({});
        updateChatDisplay();
    };

    // P6-8: opens the history list/restore/clear popup — see historyButtonClicked(). Tooltip is
    // set by updateUpsellStrip() below (it varies by plan, so setting a static default here would
    // just be overwritten), not here.
    addAndMakeVisible(historyButton);
    historyButton.setButtonText("History");
    historyButton.onClick = [this]() { historyButtonClicked(); };

    addAndMakeVisible(modelPicker);
    modelPicker.setTooltip("Select the AI model to use");
    modelPicker.onChange = [this]() {
        juce::String model = modelPicker.getText();
        aiService.setModel(model);
        appProperties.getUserSettings()->setValue("aiModel", model);
        appProperties.getUserSettings()->saveIfNeeded();
    };

    // Patch/Arrange mode selector — an EXPLICIT routing control (never a keyword heuristic:
    // shouldUseStructuredOutput() stays a patch-path concern), provider-agnostic per the
    // local/remote parity rule. Starts invisible; visible only while the timeline feature is
    // active — see refreshModeControls(), called from refreshModels() below and re-called by
    // MainComponent when the timeline preference toggles. Selection is session-scoped,
    // defaulting to Patch.
    addChildComponent(modeSelector);
    modeSelector.addItem("Patch", kModeSelectorPatchId);
    modeSelector.addItem("Arrange", kModeSelectorArrangeId);
    modeSelector.setSelectedId(kModeSelectorPatchId, juce::dontSendNotification);
    modeSelector.setTooltip("Patch: create or modify the synth patch. "
                            "Arrange: add tracks, notes and automation on the timeline.");

    // Starts invisible (same contract as accountRow/planBadge) — updateHostedModeNotice(), called
    // from refreshModels() below, sets its real visibility once a provider is known.
    addChildComponent(hostedModeNotice);
    hostedModeNotice.setJustificationType(juce::Justification::centredLeft);
    hostedModeNotice.setMinimumHorizontalScale(1.0f);
    hostedModeNotice.setFont(juce::Font(11.0f));
    hostedModeNotice.setText("Hosted mode sends your prompt and current patch to Agent Synth's servers.",
                             juce::dontSendNotification);
    // Tooltip repeats the (possibly ellipsis-truncated) label text in full rather than describing
    // the other mode — hovering a cut-off label should always reveal what it already started
    // saying, never switch topic to something else.
    hostedModeNotice.setTooltip(
        "Hosted mode sends your prompt and current patch to Agent Synth's servers for processing. See " +
        juce::String(synth::branding::kWebsiteUrl) + "/privacy for details.");

    // P6-8 upsell strip. Starts visible (see the member doc comment for why this diverges from
    // accountRow/planBadge/hostedModeNotice's invisible-until-known default) — updateUpsellStrip()
    // below sets its real state, and historyButton's tooltip, from whatever AccountSnapshot is
    // available at this point (none, at construction).
    addChildComponent(upsellButton);
    upsellButton.setButtonText("Upgrade to Pro");
    upsellButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF6B4FBB));
    upsellButton.onClick = [this] { urlOpener(juce::URL(synth::branding::kUpgradeUrl)); };

    // P6-8 downgrade notice — invisible until historyButtonClicked() learns a real grace-period
    // deletion date (see lastDeletionScheduledAt's doc comment); never shown speculatively.
    addChildComponent(downgradeStripLabel);
    downgradeStripLabel.setJustificationType(juce::Justification::centredLeft);
    downgradeStripLabel.setMinimumHorizontalScale(1.0f);
    downgradeStripLabel.setFont(juce::Font(11.0f));

    // Restore the persisted request timeout (falls back to kDefaultRequestTimeoutMs when unset)
    // and push it into aiService immediately, so the active provider is in sync from app startup
    // rather than only once Settings is opened — see AIProvider::setRequestTimeoutMs()'s and
    // AIIntegrationService::setProvider()'s doc comments for why a value has to be pushed even
    // when nothing has changed it yet.
    // getUserSettings() can be null here: a composing owner (MainComponent) constructs this as a
    // member before calling appProperties.setStorageParameters() in its own constructor body (see
    // that class's ORDERING CONTRACT comment on aiChatComponent's declaration) — in that case this
    // falls back to the default, and MainComponent re-reads the real value once its properties file
    // is actually open.
    if (auto* settings = appProperties.getUserSettings())
        requestTimeoutMs = settings->getIntValue("aiRequestTimeoutMs", kDefaultRequestTimeoutMs);
    aiService.setRequestTimeoutMs(requestTimeoutMs);

    refreshModels();
    updateUpsellStrip();

    // Populate history from aiService's own in-memory record (its lifetime spans app restarts
    // within the same session but not across them — see replayMessagesFrom()'s doc comment for why
    // this is shared with restoreConversation()'s history-panel replay).
    {
        std::vector<std::pair<juce::String, juce::String>> pairs;
        for (const auto& msg : aiService.getHistory())
            pairs.push_back({msg.role, msg.content});
        replayMessagesFrom(pairs);
    }

    updateChatDisplay();
}

AIChatComponent::~AIChatComponent() {
    // Clear the two callback slots this component installed in setAccountService(), so a later
    // publishSnapshot()/setAccessTokenFromWorker() call on a still-alive AccountService (e.g.
    // MainComponent destroys accountService after aiChatComponent) can't copy a callback that
    // captures this half-destroyed object. Belt and braces alongside the SafePointer guard
    // inside those lambdas, which is what actually makes a call arriving mid/after teardown safe.
    if (accountServicePtr != nullptr) {
        accountServicePtr->onStateChanged = nullptr;
        accountServicePtr->onAccessTokenChanged = nullptr;
    }

    stopTimer();
    // Stop any running pulse animation before members are destroyed.
    spinnerDot.stopPulse(vblankUpdater);

    // Free the conversation's bubbles. `messageList` is a plain juce::Component, and both kinds of
    // child it holds are raw-`new`ed by updateChatDisplay() — the MessageBubbles and, while a request
    // is in flight, waitingStatusLabel. addAndMakeVisible does NOT take ownership, and
    // juce::Component's destructor only *removes* its children, it never deletes them, so the ONE
    // thing that frees them is updateChatDisplay()'s own deleteAllChildren() on the next redraw.
    // Without this, the last batch — whatever was on screen at teardown — outlived the component, in
    // the app as well as in tests. Same two lines updateChatDisplay() opens with, in the same order:
    // the non-owning waitingStatusLabel pointer has to be dropped BEFORE the object behind it goes.
    waitingStatusLabel = nullptr;
    messageList.deleteAllChildren();

#ifndef NDEBUG
    juce::Logger::setCurrentLogger(nullptr);
#endif
}

void AIChatComponent::setAccountService(AccountService* service) {
    accountServicePtr = service;
    accountRow.setAccountService(service);
    planBadge.setAccountService(service);
    updateUpsellStrip();
    updateDowngradeStrip();

    if (service == nullptr)
        return;

    // SafePointer guard: AccountService::publishSnapshot()/setAccessTokenFromWorker() copy the
    // std::function out before dispatching it via MessageManager::callAsync(), so clearing these
    // members in ~AIChatComponent() alone cannot stop a call already queued at teardown time —
    // this is what actually makes such a call safe.
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    service->onStateChanged = [safeThis] {
        if (auto* self = safeThis.getComponent()) {
            self->accountRow.refresh();
            self->planBadge.refresh();
            self->updateUpsellStrip();
            // NOT self->updateDowngradeStrip() here — the date it renders is only ever learned
            // from an explicit History-button click (see lastDeletionScheduledAt's doc comment),
            // so a plan/sign-in change alone must not resurrect a stale one. updateDowngradeStrip()
            // is still worth calling: if the account just signed out or went Pro again, its own
            // gate (signedIn && !pro) already hides the strip even with a stale cached date.
            self->updateDowngradeStrip();
        }
    };
    service->onAccessTokenChanged = [safeThis](juce::String token) {
        if (auto* self = safeThis.getComponent())
            self->aiService.setAuthToken(token);
    };
}

void AIChatComponent::timerCallback() {
    if (!isWaitingForResponse)
        return;

    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    if (elapsed < requestTimeoutMs) {
        refreshWaitingStatusLabel();
        return;
    }

    // Request has timed out.
    cancelRequest();
    messages.push_back(
        {"assistant", "Error: Request timed out after " + juce::String(requestTimeoutMs / 60000) + " minutes.", ""});
    messages.back().responseMs = elapsed;
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

void AIChatComponent::refreshWaitingStatusLabel() {
    if (waitingStatusLabel == nullptr || !isWaitingForResponse)
        return;

    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    waitingStatusLabel->setText("AI is thinking... " + formatResponseTime(elapsed), juce::dontSendNotification);
}

bool AIChatComponent::keyPressed(const juce::KeyPress& key) {
    // Escape only means "cancel" while something is actually in flight; otherwise let it through
    // so it keeps whatever meaning the enclosing window gives it (closing a panel).
    if (key == juce::KeyPress::escapeKey && isWaitingForResponse) {
        handleUserCancel();
        return true;
    }

    return juce::Component::keyPressed(key);
}

void AIChatComponent::handleUserCancel() {
    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    cancelRequest();
    messages.push_back({"assistant", "Cancelled.", ""});
    messages.back().responseMs = elapsed;
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

void AIChatComponent::cancelRequest() {
    // Actually abandon the request rather than just hiding the UI for it. Until this call existed
    // the HTTP request ran to completion — billed on a metered backend, and (because
    // OllamaProvider drains its queue serially) blocking the next message the user sent. Cleared
    // first so a provider that completes the cancellation synchronously and re-enters here cannot
    // cancel the same id twice.
    const auto cancelling = activeRequestId;
    activeRequestId = {};
    if (cancelling.value != 0)
        aiService.cancelRequest(cancelling);

    // Stop the live thinking-status / timeout timer.
    stopTimer();

    // Stop the pulse animation and hide the spinner.
    spinnerDot.stopPulse(vblankUpdater);
    spinnerDot.setVisible(false);

    // Hide the cancel button, restore normal input state.
    cancelButton.setVisible(false);
    sendButton.setEnabled(true);
    inputField.setReadOnly(false);
    isWaitingForResponse = false;
    waitingStatusLabel = nullptr;
}

void AIChatComponent::paint(juce::Graphics& g) {
    auto lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf != nullptr) {
        g.fillAll(lf->getTheme().colors.bg0);
    } else {
        g.fillAll(juce::Colours::darkgrey.darker(0.5f));
    }
}

// T159: focus-region outline (Source/UI/Layout/FocusRegion.h) -- see the paintOverChildren declaration's
// comment in the header for why this can't just be tacked onto the end of paint() above.
void AIChatComponent::paintOverChildren(juce::Graphics& g) { synth::ui::paintFocusRegionOutline(*this, g); }

juce::String AIChatComponent::formatResponseTime(int ms) {
    if (ms < 0)
        ms = 0;

    if (ms < 1000)
        return juce::String(ms) + "ms";

    if (ms < 60000) {
        const double seconds = (double)ms / 1000.0;
        return juce::String(seconds, 1) + "s";
    }

    const int totalSeconds = ms / 1000;
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return juce::String(minutes) + "m " + juce::String(seconds) + "s";
}

int AIChatComponent::computeWrappedTextHeight(const juce::Font& font, const juce::String& text, int width) {
    if (text.isEmpty())
        return (int)std::ceil(font.getHeight());

    juce::GlyphArrangement ga;
    ga.addJustifiedText(font, text, 0.0f, 0.0f, (float)juce::jmax(20, width), juce::Justification::left);
    const int wrapped = (int)std::ceil(ga.getBoundingBox(0, -1, true).getHeight());
    // +2px slack for the line-boundary rounding case described in the header doc comment.
    return juce::jmax((int)std::ceil(font.getHeight()), wrapped) + 2;
}

int AIChatComponent::getLastAssistantResponseMs() const {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "assistant")
            return it->responseMs;
    }
    return -1;
}

juce::String AIChatComponent::getWaitingStatusText() const {
    return waitingStatusLabel != nullptr ? waitingStatusLabel->getText() : juce::String();
}

#ifndef NDEBUG
void AIChatComponent::appendDebugLog(const juce::String& msg) {
    {
        const juce::ScopedLock sl(logLock);
        pendingLogLines.add(msg);
    }
    bool expected = false;
    if (logFlushScheduled.compare_exchange_strong(expected, true)) {
        juce::Component::SafePointer<AIChatComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent())
                self->flushDebugLog();
        });
    }
}

void AIChatComponent::flushDebugLog() {
    logFlushScheduled.store(false);

    juce::StringArray batch;
    {
        const juce::ScopedLock sl(logLock);
        batch.swapWith(pendingLogLines);
    }

    if (batch.isEmpty())
        return;

    debugConsole.moveCaretToEnd();
    debugConsole.insertTextAtCaret(batch.joinIntoString("\n") + "\n");

    const int maxChars = 8000;
    auto txt = debugConsole.getText();
    if (txt.length() > maxChars) {
        debugConsole.setText(txt.substring(txt.length() - maxChars), juce::dontSendNotification);
        debugConsole.moveCaretToEnd();
    }
}

void AIChatComponent::logMessage(const juce::String& message) { appendDebugLog(message); }
#endif

} // namespace synth
