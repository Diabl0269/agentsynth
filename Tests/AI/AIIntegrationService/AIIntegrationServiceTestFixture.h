// Shared fixture: the AIIntegrationServiceTest test-class, its mock providers, and the minimal
// valid patch constant used across every AIIntegrationService*Tests.cpp topic file.
#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/PatchEval.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace synth {

// Smallest patch that clears the structural gate applyPatch() now runs (an Audio Output reachable
// from an Oscillator) — used by tests whose actual subject is something else (JSON extraction,
// callback ordering, replace-clears-graph) but that still need `applyPatch()` to succeed.
constexpr const char* kMinimalValidPatch = R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],)"
                                           R"("connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})";

class MockAIProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema,
                         std::function<void(const juce::String&)> onDelta = {}) override {
        juce::ignoreUnused(responseSchema, onDelta);
        lastConversation = conversation;
        AIResponse response;
        if (shouldFail) {
            response.success = false;
            response.error.kind = mockErrorKind;
            response.error.message = mockErrorMessage;
        } else {
            response.success = true;
            response.content = mockResponse;
            response.conversationId = mockConversationId;
        }
        if (callback)
            callback(response);
        return {};
    }

    void cancel(RequestId) override {}

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (shouldFail)
            callback({}, false);
        else
            callback({"model1", "model2"}, true);
    }

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

    // Tracks what AIIntegrationService::setRequestTimeoutMs()/setProvider() forwarded, mirroring
    // lastAuthToken below — used by the setProvider()-re-push regression test.
    void setRequestTimeoutMs(int timeoutMs) override {
        requestTimeoutMs = timeoutMs;
        setRequestTimeoutMsCalled = true;
    }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

    juce::String getProviderName() const override { return "MockProvider"; }

    // Overrides the AIProvider default no-op so tests can observe what
    // AIIntegrationService::setAuthToken()/setProvider() forwarded.
    void setAuthToken(const juce::String& token) override { lastAuthToken = token; }

    // Same purpose as setAuthToken() above, for AIIntegrationService::setConversationId()'s
    // capture-and-repush contract. setConversationIdCalled is tracked separately from
    // lastConversationId so a test can distinguish "never called" from "called with empty".
    void setConversationId(const juce::String& id) override {
        setConversationIdCalled = true;
        lastConversationId = id;
    }

    juce::String mockResponse = "{\"nodes\": [], \"connections\": []}";
    // Set on a successful mock AIResponse, mirroring RemoteProvider surfacing the
    // x-conversation-id response header. Empty (the default) matches the free-plan case: no
    // header at all.
    juce::String mockConversationId;
    bool shouldFail = false;
    // Configurable so tests can exercise any AIErrorKind (e.g. TrialExhausted) through
    // AIIntegrationService without needing a real RemoteProvider/HTTP mock — see
    // TrialExhaustedErrorPassesThroughWithServerMessageIntact below.
    AIErrorKind mockErrorKind = AIErrorKind::Server;
    juce::String mockErrorMessage = "Error";
    juce::String currentModel;
    juce::String lastAuthToken;
    bool setConversationIdCalled = false;
    juce::String lastConversationId;
    std::vector<Message> lastConversation;
    int requestTimeoutMs = 240000;
    bool setRequestTimeoutMsCalled = false;
};

// Holds the request instead of answering it, so a test can decide how it ends. cancel() resolves
// it the way a real provider does: one callback, kind Cancelled.
class CancellableMockAIProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        pending = std::move(callback);
        return RequestId{++lastRequestId};
    }

    void cancel(RequestId requestId) override {
        cancelledIds.push_back(requestId.value);
        if (!pending)
            return;

        auto callback = std::move(pending);
        pending = nullptr;

        AIResponse response;
        response.success = false;
        response.error.kind = AIErrorKind::Cancelled;
        response.error.message = "Request cancelled.";
        callback(response);
    }

    /** Resolves the held request normally, for the "a real answer still lands in history" half of
        the comparison. */
    void completeWith(const juce::String& content) {
        if (!pending)
            return;

        auto callback = std::move(pending);
        pending = nullptr;

        AIResponse response;
        response.success = true;
        response.content = content;
        callback(response);
    }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"model1"}, true);
    }

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    juce::String getProviderName() const override { return "CancellableMockProvider"; }

    std::vector<uint64_t> cancelledIds;
    uint64_t lastRequestId = 0;

private:
    CompletionCallback pending;
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

class CountingListener : public AIIntegrationService::Listener {
public:
    int aboutToApplyCount = 0;
    int appliedCount = 0;
    int aboutToApplyOrder = -1;
    int appliedOrder = -1;
    int* sharedCallCounter = nullptr;

    void aiPatchAboutToApply() override {
        ++aboutToApplyCount;
        if (sharedCallCounter)
            aboutToApplyOrder = (*sharedCallCounter)++;
    }

    void aiPatchApplied() override {
        ++appliedCount;
        if (sharedCallCounter)
            appliedOrder = (*sharedCallCounter)++;
    }
};

class AIIntegrationServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_unique<juce::AudioProcessorGraph>();
        service = std::make_unique<AIIntegrationService>(*graph);
    }

    std::unique_ptr<juce::AudioProcessorGraph> graph;
    std::unique_ptr<AIIntegrationService> service;
};

} // namespace synth
