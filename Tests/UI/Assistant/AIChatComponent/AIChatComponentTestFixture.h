#pragma once

// Shared mocks, tree-walking helpers, and fixture for the AIChatComponent test suite
// (Tests/UI/Assistant/AIChatComponent/AIChatComponent*Tests.cpp). Header-only; not compiled on its own and not
// registered in Tests/CMakeLists.txt.

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AccountService.h"
#include "AI/ConversationHistorySource.h"
#include "AI/LocalHistoryStore.h"
#include "AudioEngine.h"
#include "Auth/InMemoryTokenStore.h"
#include "Branding.h"
#include "UI/Assistant/AIChatComponent.h"
#include "UI/Assistant/AccountRow.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MockChatProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockChatProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1", "MockModel2"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema = juce::var(),
                         std::function<void(const juce::String&)> onDelta = {}) override {
        juce::ignoreUnused(conversation, responseSchema, onDelta);
        AIResponse response;
        response.success = true;
        response.content = "Mock response text.";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    // Empty by default (not pre-seeded with a model name) so tests that check
    // getCurrentModel() after refreshModels()/setModel() genuinely prove that a model was
    // selected, rather than being masked by a non-empty default.
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// Like MockChatProvider, but fetchAvailableModels() does not resolve until the test calls
// resolvePending() — mirrors the real OllamaProvider, whose discovery hop is asynchronous.
// This lets a test observe modelPicker's state mid-refresh, before the fetch that would
// otherwise immediately clear() and repopulate it.
class DeferredChatProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "DeferredChatProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        pendingCallback = callback;
    }

    void resolvePending(const juce::StringArray& models, bool success) {
        auto callback = pendingCallback;
        pendingCallback = nullptr;
        if (callback)
            callback(models, success);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        callback(AIResponse{});
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    std::function<void(const juce::StringArray&, bool)> pendingCallback;
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// Holds sendPrompt's CompletionCallback until the test resolves it — used to assert wait-state
// timing (responseMs) after a real elapsed interval, and to cancel while still waiting.
class DeferredPromptProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "DeferredPromptProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        pendingPromptCallback = std::move(callback);
        return {42};
    }

    void resolvePrompt(const AIResponse& response) {
        auto callback = std::move(pendingPromptCallback);
        pendingPromptCallback = nullptr;
        if (callback)
            callback(response);
    }

    bool hasPendingPrompt() const { return static_cast<bool>(pendingPromptCallback); }

    void cancel(RequestId) override {
        if (!pendingPromptCallback)
            return;
        AIResponse cancelled;
        cancelled.success = false;
        cancelled.error.kind = AIErrorKind::Cancelled;
        cancelled.error.message = "Cancelled";
        resolvePrompt(cancelled);
    }

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    CompletionCallback pendingPromptCallback;
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// Synchronously delivers a failed AIResponse with a caller-chosen error kind/message — used to
// exercise AIChatComponent's failure-branch UI (P4-4: the Quota error's upgrade bubble, and the
// regression lock that every other kind keeps the old flat bubble).
class ErrorProvider : public synth::AIProvider {
public:
    ErrorProvider(AIErrorKind kind, juce::String message)
        : kind(kind)
        , message(std::move(message)) {}

    juce::String getProviderName() const override { return "ErrorProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = false;
        response.error.kind = kind;
        response.error.message = message;
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    AIErrorKind kind;
    juce::String message;
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// P4-6: stands in for RemoteProvider without any network dependency — isHosted() true, and
// fetchAvailableModels() resolves success=true with an empty list, mirroring RemoteProvider's own
// "the service picks its own model server-side" contract (see its doc comment).
class HostedMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "HostedMockProvider"; }
    bool isHosted() const override { return true; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        callback(AIResponse{});
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// Returns an assistant response containing a single fenced ```json patch, so tests can exercise
// AIChatComponent's PatchCard (and its P6-3 thumbs feedback) without a real provider.
class MockPatchProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPatchProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "```json\n"
                           R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],)"
                           R"("connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})"
                           "\n```";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// A fenced ```json block that PARSES fine but names a module type that doesn't exist —
// AIStateMapper::applyJSONToGraph() (untrusted) rejects it, so AIIntegrationService::
// computePatchPreview() reports diffAvailable=false and PatchCard falls back to its
// "Preview unavailable - this patch may be rejected when applied." status line. Used to
// reproduce the bug where that status line's real (wrapped) height was estimated from a fixed
// line count rather than measured, and got clipped inside diffDisplay.
class MockInvalidPatchProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockInvalidPatchProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "```json\n"
                           R"({"nodes":[{"id":1,"type":"TotallyNotARealModuleType"}],"connections":[]})"
                           "\n```";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// Every response carries a fixed conversationId (mirrors a Pro-plan hosted backend persisting the
// exchange server-side), and every setConversationId() call is recorded — so a test can lock
// AIIntegrationService's re-push/clear contract (see AIIntegrationService.cpp's sendMessage()
// comment) without a real RemoteProvider.
class ConversationIdRecordingProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "ConversationIdRecordingProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
        response.conversationId = "server-conv-1";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    void setConversationId(const juce::String& id) override { setConversationIdCalls.push_back(id); }

    std::vector<juce::String> setConversationIdCalls;

private:
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// P6-9: like MockPatchProvider (a single fenced ```json patch, so PatchCard/thumbs render), but
// the response also carries a fixed conversationId + messageId, mirroring a Pro-plan hosted
// backend whose persistence succeeded — the one condition that makes the rating callback's
// server-sync path fire at all (see MessageData::serverMessageId).
class MockPatchProviderWithServerIds : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPatchProviderWithServerIds"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "```json\n"
                           R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],)"
                           R"("connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})"
                           "\n```";
        response.conversationId = "server-conv-1";
        response.messageId = "server-msg-1";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// Finds the juce::Viewport AIChatComponent adds as a direct child and returns its viewed
// component (messageList) — the parent of every rendered MessageBubble. MessageBubble itself is a
// private nested type, but its base juce::Component* children (labels, buttons) are inspectable
// without knowing the concrete type.
static juce::Component* findMessageList(synth::AIChatComponent& chatComponent) {
    for (auto* child : chatComponent.getChildren()) {
        if (auto* viewport = dynamic_cast<juce::Viewport*>(child))
            return viewport->getViewedComponent();
    }
    return nullptr;
}

// Depth-first search for a descendant of the given kind (Label or TextButton) whose text matches.
template <typename ComponentType>
ComponentType* findDescendantWithText(juce::Component* root, const juce::String& text) {
    if (root == nullptr)
        return nullptr;
    for (auto* child : root->getChildren()) {
        if (auto* match = dynamic_cast<ComponentType*>(child)) {
            if constexpr (std::is_same_v<ComponentType, juce::Label>) {
                if (match->getText() == text)
                    return match;
            } else if constexpr (std::is_same_v<ComponentType, juce::TextButton>) {
                if (match->getButtonText() == text)
                    return match;
            }
        }
        if (auto* nested = findDescendantWithText<ComponentType>(child, text))
            return nested;
    }
    return nullptr;
}

class AIChatComponentTest : public ::testing::Test {
protected:
};
