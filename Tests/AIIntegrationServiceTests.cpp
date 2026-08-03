#include "AI/AIIntegrationService.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace synth {

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
            response.error.kind = AIErrorKind::Server;
            response.error.message = "Error";
        } else {
            response.success = true;
            response.content = mockResponse;
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
    juce::String getProviderName() const override { return "MockProvider"; }

    juce::String mockResponse = "{\"nodes\": [], \"connections\": []}";
    bool shouldFail = false;
    juce::String currentModel;
    std::vector<Message> lastConversation;
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
    juce::String getProviderName() const override { return "CancellableMockProvider"; }

    std::vector<uint64_t> cancelledIds;
    uint64_t lastRequestId = 0;

private:
    CompletionCallback pending;
    juce::String currentModel;
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

TEST_F(AIIntegrationServiceTest, InitialHistoryHasSystemPrompt) {
    EXPECT_FALSE(service->getHistory().empty());
    EXPECT_EQ(service->getHistory()[0].role, "system");
}

TEST_F(AIIntegrationServiceTest, SendMessageAddsToHistory) {
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    rawProvider->mockResponse = "AI Response";

    bool called = false;
    service->sendMessage("User msg", [&](const AIProvider::AIResponse&) { called = true; });

    EXPECT_TRUE(called);
    // History should have: system, user, assistant
    auto history = service->getHistory();
    EXPECT_EQ(history.size(), 3);
    EXPECT_EQ(history[1].role, "user");
    EXPECT_EQ(history[1].content, "User msg");
    EXPECT_EQ(history[2].role, "assistant");
    EXPECT_EQ(history[2].content, "AI Response");
}

TEST_F(AIIntegrationServiceTest, ClearHistoryPreservesSystemPrompt) {
    service->sendMessage("test", nullptr);
    service->clearHistory();

    EXPECT_EQ(service->getHistory().size(), 1);
    EXPECT_EQ(service->getHistory()[0].role, "system");
}

TEST_F(AIIntegrationServiceTest, ExtractJsonFromResponse) {
    // This tests a private method if we use friend or just test through applyPatch
    // Since it's private and we don't have friend access here easily,
    // we test it indirectly via applyPatch which is public.

    // Test extraction from backticks
    juce::String withBackticks = "Here is the patch: ```json\n{\"nodes\":[]}\n``` and some more text.";
    // applyPatch calls extractJsonFromResponse internally
    // We expect it to try and parse the JSON. If it fails, it returns false.
    // If it succeeds (empty nodes), it returns true.
    EXPECT_TRUE(service->applyPatch(withBackticks));
}

TEST_F(AIIntegrationServiceTest, ExtractJsonRaw) {
    juce::String raw = "{\"nodes\":[], \"connections\":[]}";
    EXPECT_TRUE(service->applyPatch(raw));
}

TEST_F(AIIntegrationServiceTest, GetPatchContext) {
    juce::String context = service->getPatchContext();
    EXPECT_TRUE(context.contains("nodes"));
    EXPECT_TRUE(context.contains("connections"));
}

TEST_F(AIIntegrationServiceTest, ModelManagement) {
    auto provider = std::make_unique<MockAIProvider>();
    service->setProvider(std::move(provider));

    service->setModel("test-model");
    EXPECT_EQ(service->getCurrentModel(), "test-model");
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_MergeMode_PreservesExisting) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a delta patch in merge mode
    juce::String deltaJson = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(deltaJson, true);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 2); // Original Oscillator + new Filter
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_DefaultReplace_ClearsGraph) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a full patch without merge mode (default)
    juce::String fullJson = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(fullJson);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 1); // Only the Filter from JSON, Oscillator cleared
}

TEST_F(AIIntegrationServiceTest, HistoryDoesNotRetainPatchContext) {
    graph->addNode(std::make_unique<OscillatorModule>());

    auto provider = std::make_unique<MockAIProvider>();
    service->setProvider(std::move(provider));

    bool called = false;
    service->sendMessage("Add a filter", [&](const AIProvider::AIResponse&) { called = true; }, true);

    EXPECT_TRUE(called);
    auto history = service->getHistory();
    ASSERT_GE(history.size(), 2u);
    EXPECT_EQ(history[1].role, "user");
    EXPECT_EQ(history[1].content, "Add a filter");
    EXPECT_FALSE(history[1].content.contains("Current patch state"));
}

TEST_F(AIIntegrationServiceTest, OutgoingRequestStillIncludesPatchContext) {
    graph->addNode(std::make_unique<OscillatorModule>());

    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Add a filter", nullptr, true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    EXPECT_TRUE(rawProvider->lastConversation.back().content.contains("Current patch state"));
    EXPECT_TRUE(rawProvider->lastConversation.back().content.contains("Add a filter"));
}

TEST_F(AIIntegrationServiceTest, HistoryIsTrimmedToCap) {
    auto provider = std::make_unique<MockAIProvider>();
    service->setProvider(std::move(provider));

    const int turnsToSend = AIIntegrationService::kMaxHistoryTurns + 5;
    for (int i = 0; i < turnsToSend; ++i)
        service->sendMessage("msg " + juce::String(i), nullptr);

    auto history = service->getHistory();
    EXPECT_LE(history.size(), 1u + static_cast<size_t>(AIIntegrationService::kMaxHistoryTurns) * 2u);
}

TEST_F(AIIntegrationServiceTest, SystemPromptSurvivesTrimming) {
    auto provider = std::make_unique<MockAIProvider>();
    service->setProvider(std::move(provider));

    const int turnsToSend = AIIntegrationService::kMaxHistoryTurns + 5;
    for (int i = 0; i < turnsToSend; ++i)
        service->sendMessage("msg " + juce::String(i), nullptr);

    ASSERT_FALSE(service->getHistory().empty());
    EXPECT_EQ(service->getHistory()[0].role, "system");
}

TEST_F(AIIntegrationServiceTest, InvalidJsonFiresNoListenerCallbacks) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch("not json at all");

    EXPECT_FALSE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

TEST_F(AIIntegrationServiceTest, StructurallyInvalidPatchFiresNoListenerCallbacks) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch("{\"nodes\": \"not-an-array\"}");

    EXPECT_FALSE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

TEST_F(AIIntegrationServiceTest, ValidPatchFiresBothCallbacksInOrder) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch("{\"nodes\":[], \"connections\":[]}");

    EXPECT_TRUE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 1);
    EXPECT_EQ(listener.appliedCount, 1);
    EXPECT_LT(listener.aboutToApplyOrder, listener.appliedOrder);

    service->removeListener(&listener);
}

// A cancelled request produced no assistant turn, so none may be recorded. Appending one would
// both put words in the model's mouth in the transcript and -- because chatHistory is replayed as
// context -- feed that invention back on every later message.
TEST_F(AIIntegrationServiceTest, CancelledRequestDoesNotAppendToHistory) {
    auto provider = std::make_unique<CancellableMockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    AIProvider::AIErrorKind reportedKind = AIProvider::AIErrorKind::None;
    int callCount = 0;
    const auto id = service->sendMessage("Make me a huge patch", [&](const AIProvider::AIResponse& response) {
        ++callCount;
        reportedKind = response.error.kind;
    });

    // The user's own turn is recorded on send, before any answer exists.
    ASSERT_EQ(service->getHistory().size(), 2u);

    service->cancelRequest(id);

    EXPECT_EQ(callCount, 1) << "the caller must still be told exactly once that its request ended";
    EXPECT_EQ(reportedKind, AIProvider::AIErrorKind::Cancelled);
    ASSERT_EQ(rawProvider->cancelledIds.size(), 1u) << "the service did not forward the cancel to the provider";
    EXPECT_EQ(rawProvider->cancelledIds[0], id.value);

    // Still just system + user: the user did say their part and it stays, but nothing was added on
    // the assistant's behalf.
    ASSERT_EQ(service->getHistory().size(), 2u) << "a cancelled request added an assistant turn to the history";
    EXPECT_EQ(service->getHistory()[0].role, "system");
    EXPECT_EQ(service->getHistory()[1].role, "user");
}

// The counterpart to the above: the "don't append" rule must be specific to cancellation and must
// not have quietly broken the normal path.
TEST_F(AIIntegrationServiceTest, CompletedRequestStillAppendsToHistory) {
    auto provider = std::make_unique<CancellableMockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Hello", nullptr);
    rawProvider->completeWith("Hi there");

    ASSERT_EQ(service->getHistory().size(), 3u);
    EXPECT_EQ(service->getHistory()[2].role, "assistant");
    EXPECT_EQ(service->getHistory()[2].content, "Hi there");
}

// A stale handle is the normal case in the UI (a response and a Cancel click cross), so cancelling
// something already finished must be inert rather than merely survivable.
TEST_F(AIIntegrationServiceTest, CancelAfterCompletionIsInert) {
    auto provider = std::make_unique<CancellableMockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    int callCount = 0;
    const auto id = service->sendMessage("Hello", [&](const AIProvider::AIResponse&) { ++callCount; });
    rawProvider->completeWith("Hi there");
    ASSERT_EQ(callCount, 1);

    service->cancelRequest(id);

    EXPECT_EQ(callCount, 1) << "cancelling a completed request fired a second callback";
    EXPECT_EQ(service->getHistory().size(), 3u) << "cancelling a completed request disturbed the history";
}

// Without a provider there is nothing to cancel; sendMessage() reports the failure inline and must
// hand back the reserved handle rather than something a later cancel could act on.
TEST_F(AIIntegrationServiceTest, SendMessageWithoutProviderReturnsInvalidRequestId) {
    bool called = false;
    const auto id = service->sendMessage("Hello", [&](const AIProvider::AIResponse& response) {
        called = true;
        EXPECT_FALSE(response.success);
    });

    EXPECT_TRUE(called);
    EXPECT_EQ(id.value, 0u);

    service->cancelRequest(id); // must not crash
}

} // namespace synth
