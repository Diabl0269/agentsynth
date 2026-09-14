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

    // Re-push contract (mirrors AIChatComponent::refreshModels(), see docs/AI_Engine_chat_component.md "Model
    // Discovery Ordering Contract"): a caller may have called setAuthToken() before a provider
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

void AIIntegrationService::setAuthToken(const juce::String& token) {
    currentAuthToken = token;
    if (provider)
        provider->setAuthToken(currentAuthToken);
}

void AIIntegrationService::setConversationId(const juce::String& id) {
    currentConversationId = id;
    if (provider)
        provider->setConversationId(currentConversationId);
}

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
