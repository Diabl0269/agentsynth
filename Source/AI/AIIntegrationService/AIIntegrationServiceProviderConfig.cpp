// Provider lifecycle & model management: construction, provider install/swap, and the
// auth-token/conversation-id/timeout re-push contract described on each setter's doc comment.
#include "AIIntegrationService.h"

namespace synth {

AIIntegrationService::AIIntegrationService(juce::AudioProcessorGraph& graph, AppUndoManager* undoManager)
    : audioGraph(graph)
    , undoManager(undoManager) {
    initSystemPrompt();
}

AIIntegrationService::~AIIntegrationService() {}

void AIIntegrationService::setProvider(std::unique_ptr<AIProvider> newProvider) {
    provider = std::move(newProvider);

    // Re-push contract (mirrors AIChatComponent::refreshModels(), see
    // docs/ai/chat-component.md#model-discovery-ordering-contract): a caller may have called
    // setAuthToken() before a provider
    // existed at all, so the value must be forwarded to whatever provider is installed now.
    if (provider && currentAuthToken.isNotEmpty())
        provider->setAuthToken(currentAuthToken);

    // Same re-push shape for the conversation id (see setConversationId()'s doc comment).
    if (provider && currentConversationId.isNotEmpty())
        provider->setConversationId(currentConversationId);

    // Unlike the token/conversation id above, the timeout is always pushed (never gated on
    // "non-empty") — a provider always has SOME timeout, so failing to re-push here would mean
    // the newly installed provider silently falls back to its own hardcoded default instead of
    // whatever the user configured, reintroducing the UI-watchdog/provider-timeout drift this
    // value exists to prevent.
    if (provider)
        provider->setRequestTimeoutMs(currentRequestTimeoutMs);
}

// Stored regardless of whether a provider is currently installed — setProvider() re-pushes it to
// whatever provider it installs next, mirroring the model-discovery re-push contract documented
// for this class (see docs/ai/chat-component.md#model-discovery-ordering-contract):
// AIChatComponent/AccountService can be wired up before MainComponent::initialiseCommon() installs
// the real provider, so a value set first must not be lost.
void AIIntegrationService::setAuthToken(const juce::String& token) {
    currentAuthToken = token;
    if (provider)
        provider->setAuthToken(currentAuthToken);
}

// Same re-push contract as setAuthToken(): stored regardless of whether a provider is currently
// installed, and setProvider() re-pushes it to whatever provider it installs next. Normally
// callers don't need to call this directly — sendMessage() captures a non-empty
// AIResponse::conversationId from a successful response and stores/re-pushes it itself, so the
// next call in the session continues the same server-side thread. AIChatComponent calls this
// directly only to CLEAR it (empty string) when the active plan isn't Pro, so a stale id from an
// earlier Pro session isn't sent to a since-downgraded account.
void AIIntegrationService::setConversationId(const juce::String& id) {
    currentConversationId = id;
    if (provider)
        provider->setConversationId(currentConversationId);
}

// Same re-push contract as setAuthToken()/setConversationId(): stored regardless of whether a
// provider is currently installed, and setProvider() re-pushes it (unconditionally, since unlike a
// token or conversation id there's always a meaningful value) to whatever provider it installs
// next — otherwise a provider swap would silently fall back to that provider's own hardcoded
// default, re-introducing the exact drift this value exists to prevent (see
// docs/ai/chat-component.md#request-timeout).
void AIIntegrationService::setRequestTimeoutMs(int timeoutMs) {
    currentRequestTimeoutMs = timeoutMs;
    if (provider)
        provider->setRequestTimeoutMs(currentRequestTimeoutMs);
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
