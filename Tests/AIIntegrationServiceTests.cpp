#include "AI/AIIntegrationService.h"
#include "AI/PatchEval.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

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
    juce::String withBackticks =
        "Here is the patch: ```json\n" + juce::String(kMinimalValidPatch) + "\n``` and some more text.";
    // applyPatch calls extractJsonFromResponse internally
    // We expect it to try and parse the JSON. If it fails, it returns false.
    // If it succeeds, it returns true.
    EXPECT_TRUE(service->applyPatch(withBackticks));
}

TEST_F(AIIntegrationServiceTest, ExtractJsonRaw) { EXPECT_TRUE(service->applyPatch(kMinimalValidPatch)); }

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

// --- Structural gate: applyPatch() now rejects a schema-valid patch that doesn't produce a
// usable signal path (Source/AI/PatchEval.h), the same bar Tools/AIEvalHarness measures models
// against. See the "Structural gate" comment in AIIntegrationService::applyPatch() for the
// regression-vs-absolute distinction between replace and merge mode. ---

TEST_F(AIIntegrationServiceTest, ReplaceModeWithNoAudioOutputIsRejectedStructurally) {
    bool success = service->applyPatch(R"({"nodes":[],"connections":[]})");

    EXPECT_FALSE(success);
    EXPECT_EQ(service->getLastPatchError(), "no Audio Output node in the patch");
    EXPECT_EQ(graph->getNumNodes(), 0) << "a structurally rejected patch must not touch the live graph";
}

TEST_F(AIIntegrationServiceTest, ReplaceModeNotReachingOutputIsRejectedStructurally) {
    // Audio Output exists but nothing feeds it — no Oscillator anywhere in the patch.
    bool success = service->applyPatch(R"({"nodes":[{"id":1,"type":"Audio Output"}],"connections":[]})");

    EXPECT_FALSE(success);
    EXPECT_EQ(service->getLastPatchError(), "Audio Output is not reachable from any Oscillator");
}

TEST_F(AIIntegrationServiceTest, StructuralRejectionFiresNoListenerCallbacks) {
    int callCounter = 0;
    CountingListener listener;
    listener.sharedCallCounter = &callCounter;
    service->addListener(&listener);

    bool success = service->applyPatch(R"({"nodes":[],"connections":[]})");

    EXPECT_FALSE(success);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

TEST_F(AIIntegrationServiceTest, MergeRegressionGate_PreExistingGapIsNotBlamedOnTheDelta) {
    // The live graph already has no Oscillator (just a bare Audio Output) — a merge delta that
    // doesn't fix that, but doesn't make it worse either, must not be rejected for a gap it never
    // caused (e.g. the user's own half-built canvas).
    graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_EQ(graph->getNumNodes(), 1);

    juce::String delta = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(delta, /*mergeMode=*/true);

    EXPECT_TRUE(success) << service->getLastPatchError();
    EXPECT_EQ(graph->getNumNodes(), 2);
}

TEST_F(AIIntegrationServiceTest, MergeRegressionGate_RejectsADeltaThatBreaksAWorkingChain) {
    // Live graph: a complete Oscillator(1) -> Audio Output(2) chain. Ids are preserved by the
    // trusted+clearExisting apply path (mirrors undo/redo's own snapshot replay — see the
    // "Preserve node identity" comment in AIStateMapper::applyJSONToGraph), so "remove":[1] below
    // reliably targets the Oscillator.
    //
    // prepareGraphForPatchEval() matters here beyond evaluation: without it the "Audio Output"
    // node reports zero channels and the seed connection below silently no-ops, so the "before"
    // state would already read as unreachable regardless of this test's fixture. A real live
    // graph is always configured this way by AudioEngine before AIIntegrationService ever runs.
    prepareGraphForPatchEval(*graph);
    juce::var seed = juce::JSON::parse(juce::String(kMinimalValidPatch));
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(seed, *graph, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(graph->getNumNodes(), 2);

    // "Remove the Oscillator" without reconnecting anything: the chain regresses.
    bool success = service->applyPatch(R"({"mode":"merge","remove":[1],"nodes":[],"connections":[]})",
                                       /*mergeMode=*/true);

    EXPECT_FALSE(success);
    EXPECT_EQ(service->getLastPatchError(), "Audio Output is not reachable from any Oscillator");
    EXPECT_EQ(graph->getNumNodes(), 2) << "a structurally rejected merge must not touch the live graph";
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_DefaultReplace_ClearsGraph) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a full patch without merge mode (default)
    bool success = service->applyPatch(kMinimalValidPatch);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 2); // Only kMinimalValidPatch's 2 nodes; the old Oscillator is gone
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

    bool success = service->applyPatch(kMinimalValidPatch);

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

// --- P2-9: worked few-shot examples in the system prompt ---
//
// The JSON below must stay in sync with the examples embedded in
// AIIntegrationService::initSystemPrompt() (Source/AI/AIIntegrationService.cpp) — these tests exist to
// catch a hand-authoring mistake (a dangling node, a bad param) that eyeballing the prompt string would
// miss. They are necessary but not sufficient: the actual proof this feature works is a
// Tools/AIEvalHarness pass-rate delta, documented in the PR, not asserted here.

namespace {

constexpr const char* kWorkedExample1 = R"({
  "nodes": [
    { "id": 101, "type": "Oscillator", "params": { "waveform": "Saw", "octave": -1 } },
    { "id": 102, "type": "Filter", "params": { "cutoff": 800.0, "resonance": 0.4 } },
    { "id": 103, "type": "VCA" },
    { "id": 104, "type": "Filter Env", "params": { "attack": 0.01, "decay": 0.3, "sustain": 0.2, "release": 0.2 } },
    { "id": 105, "type": "Amp Env", "params": { "attack": 0.01, "decay": 0.15, "sustain": 0.7, "release": 0.2 } },
    { "id": 106, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 101, "srcPort": 0, "dst": 102, "dstPort": 0 },
    { "src": 102, "srcPort": 0, "dst": 103, "dstPort": 0 },
    { "src": 103, "srcPort": 0, "dst": 106, "dstPort": 0 }
  ],
  "modulations": [
    { "source": 104, "dest": 102, "destPort": 1, "amount": 0.6 },
    { "source": 105, "dest": 103, "destPort": 1, "amount": 1.0 }
  ]
})";

constexpr const char* kWorkedExample2 = R"({
  "nodes": [
    { "id": 201, "type": "Oscillator", "params": { "waveform": "Square", "octave": 1 } },
    { "id": 202, "type": "Filter", "params": { "cutoff": 3000.0, "resonance": 0.3 } },
    { "id": 203, "type": "VCA" },
    { "id": 204, "type": "Filter Env", "params": { "attack": 0.01, "decay": 0.2, "sustain": 0.0, "release": 0.1 } },
    { "id": 205, "type": "Amp Env", "params": { "attack": 0.01, "decay": 0.15, "sustain": 0.0, "release": 0.1 } },
    { "id": 206, "type": "LFO", "params": { "mode": false, "rateHz": 5.0, "level": 1.0 } },
    { "id": 207, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 201, "srcPort": 0, "dst": 202, "dstPort": 0 },
    { "src": 202, "srcPort": 0, "dst": 203, "dstPort": 0 },
    { "src": 203, "srcPort": 0, "dst": 207, "dstPort": 0 }
  ],
  "modulations": [
    { "source": 204, "dest": 202, "destPort": 1, "amount": 0.7 },
    { "source": 205, "dest": 203, "destPort": 1, "amount": 1.0 },
    { "source": 206, "dest": 201, "destPort": 4, "amount": 1.0 }
  ]
})";

constexpr const char* kWorkedExample3 = R"({
  "nodes": [
    { "id": 301, "type": "Oscillator", "params": { "waveform": "Saw" } },
    { "id": 302, "type": "Filter", "params": { "cutoff": 1500.0 } },
    { "id": 303, "type": "VCA" },
    { "id": 304, "type": "Distortion", "params": { "drive": 3.0, "mix": 0.3 } },
    { "id": 305, "type": "Chorus", "params": { "rate": 0.6, "depth": 0.4, "mix": 0.5 } },
    { "id": 306, "type": "Reverb", "params": { "roomSize": 0.7, "wet": 0.4 } },
    { "id": 307, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 301, "srcPort": 0, "dst": 302, "dstPort": 0 },
    { "src": 302, "srcPort": 0, "dst": 303, "dstPort": 0 },
    { "src": 303, "srcPort": 0, "dst": 304, "dstPort": 0 },
    { "src": 304, "srcPort": 0, "dst": 305, "dstPort": 0 },
    { "src": 305, "srcPort": 0, "dst": 306, "dstPort": 0 },
    { "src": 306, "srcPort": 0, "dst": 307, "dstPort": 0 }
  ]
})";

constexpr const char* kWorkedExample4Seed = R"({
  "nodes": [
    { "id": 401, "type": "Oscillator", "params": { "waveform": "Saw" } },
    { "id": 402, "type": "Filter", "params": { "cutoff": 1200.0 } },
    { "id": 403, "type": "VCA" },
    { "id": 404, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 401, "srcPort": 0, "dst": 402, "dstPort": 0 },
    { "src": 402, "srcPort": 0, "dst": 403, "dstPort": 0 },
    { "src": 403, "srcPort": 0, "dst": 404, "dstPort": 0 }
  ]
})";

constexpr const char* kWorkedExample4Delta =
    R"({"mode": "merge", "nodes": [{"id": 9401, "type": "Oscillator", "params": {"waveform": "Saw", "coarse": 7}}], )"
    R"("connections": [{"src": 9401, "srcPort": 0, "dst": 402, "dstPort": 0}]})";

constexpr const char* kWorkedExample5Seed = R"({
  "nodes": [
    { "id": 501, "type": "Oscillator", "params": { "waveform": "Square" } },
    { "id": 502, "type": "Distortion" },
    { "id": 503, "type": "Filter", "params": { "cutoff": 2500.0 } },
    { "id": 504, "type": "VCA" },
    { "id": 505, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 501, "srcPort": 0, "dst": 502, "dstPort": 0 },
    { "src": 502, "srcPort": 0, "dst": 503, "dstPort": 0 },
    { "src": 503, "srcPort": 0, "dst": 504, "dstPort": 0 },
    { "src": 504, "srcPort": 0, "dst": 505, "dstPort": 0 }
  ]
})";

constexpr const char* kWorkedExample5Delta =
    R"({"mode": "merge", "remove": [502], "nodes": [], )"
    R"("connections": [{"src": 501, "srcPort": 0, "dst": 503, "dstPort": 0}]})";

PatchEvalResult applyAndEvaluate(const char* json) {
    juce::AudioProcessorGraph graph;
    prepareGraphForPatchEval(graph);
    juce::var parsed = juce::JSON::parse(juce::String(json));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(parsed, graph, /*clearExisting=*/true, /*trusted=*/true));
    return evaluatePatch(graph);
}

PatchEvalResult applyMergeAndEvaluate(const char* seedJson, const char* deltaJson) {
    juce::AudioProcessorGraph graph;
    prepareGraphForPatchEval(graph);
    juce::var seed = juce::JSON::parse(juce::String(seedJson));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(seed, graph, /*clearExisting=*/true, /*trusted=*/true));
    juce::var delta = juce::JSON::parse(juce::String(deltaJson));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(delta, graph, /*clearExisting=*/false, /*trusted=*/true));
    return evaluatePatch(graph);
}

} // namespace

TEST_F(AIIntegrationServiceTest, SystemPromptContainsWorkedExamples) {
    auto prompt = service->getHistory()[0].content;
    EXPECT_TRUE(prompt.contains("WORKED EXAMPLES"));
    EXPECT_TRUE(prompt.contains("Patch together a growling, punchy analog-style bass line"));
    EXPECT_TRUE(prompt.contains("Design a snappy square-wave pluck lead"));
    EXPECT_TRUE(prompt.contains("Chain a saw-wave source through drive, chorus, and reverb"));
    EXPECT_TRUE(prompt.contains("Stack a second oscillator a fifth above the existing one"));
    EXPECT_TRUE(prompt.contains("Take out the distortion"));
}

TEST_F(AIIntegrationServiceTest, WorkedExamplePatchesAreStructurallyValid) {
    for (const char* json : {kWorkedExample1, kWorkedExample2, kWorkedExample3}) {
        auto result = applyAndEvaluate(json);
        EXPECT_TRUE(result.passed()) << "example failed: " << result.detail;
    }

    auto merge4 = applyMergeAndEvaluate(kWorkedExample4Seed, kWorkedExample4Delta);
    EXPECT_TRUE(merge4.passed()) << "example 4 failed: " << merge4.detail;

    auto merge5 = applyMergeAndEvaluate(kWorkedExample5Seed, kWorkedExample5Delta);
    EXPECT_TRUE(merge5.passed()) << "example 5 failed: " << merge5.detail;
}

// Reusing an AIEvalHarness eval prompt as a few-shot example would be train/test contamination and
// invalidate the P2-8 measurement — see the P2-9 task card. This is a manual copy of the 40 scenario
// prompts from Tools/AIEvalHarness/Main.cpp's scenarios(); if that list changes, re-sync it here.
TEST_F(AIIntegrationServiceTest, WorkedExamplePromptsDoNotOverlapEvalScenarios) {
    static const std::vector<juce::String> kEvalScenarioPrompts = {
        "Create a fat analog bass patch",
        "Make a short plucky lead sound",
        "Build a warm evolving pad with a slow filter sweep",
        "Create an acid bassline with a resonant filter and a sequencer",
        "Design a bell-like FM tone",
        "Make a dark drone with reverb",
        "Create a bright synth stab with a fast envelope",
        "Build a simple organ patch with multiple oscillators",
        "Make a white noise sweep riser",
        "Create a polyphonic keys patch with chorus",
        "Build a patch with compression and limiting on the output",
        "Create a swirling flanger effect on a saw wave",
        "Build a polyphonic pad using Poly MIDI and a voice mixer",
        "Make a sequenced arpeggio with a poly sequencer",
        "Create a pumping bass sound with heavy compression",
        "Build a lead patch driven by external MIDI input",
        "Create a thick unison lead with two oscillators",
        "Design an ambient wash with chorus, phaser, and reverb",
        "Make a percussive pluck with a fast filter envelope",
        "Build a vintage organ patch with a slow chorus and a flanger",
        "Add reverb to the end of the chain",
        "Add an LFO modulating the filter cutoff",
        "Make it brighter",
        "Add a delay after the filter",
        "Change the oscillator to a square wave",
        "Add an amp envelope controlling the VCA",
        "Remove the filter from the patch",
        "Add some distortion for grit",
        "Detune the oscillator slightly for width",
        "Add chorus and then a delay at the end",
        "Make the attack slower",
        "Increase the filter resonance",
        "Add a compressor for punch",
        "Add a limiter at the very end",
        "Add a flanger for movement",
        "Add an LFO for subtle pitch vibrato",
        "Add a filter envelope controlling the cutoff",
        "Make it quieter",
        "Remove the VCA",
        "Add an LFO modulating the cutoff at reduced depth",
    };

    static const std::vector<juce::String> kFewShotPrompts = {
        "Patch together a growling, punchy analog-style bass line",
        "Design a snappy square-wave pluck lead",
        "Chain a saw-wave source through drive, chorus, and reverb for a wide, driven texture",
        "Stack a second oscillator a fifth above the existing one, feeding into the same filter",
        "Take out the distortion - it's too harsh - and connect straight through instead",
    };

    for (const auto& fewShot : kFewShotPrompts) {
        for (const auto& evalPrompt : kEvalScenarioPrompts) {
            EXPECT_FALSE(fewShot.equalsIgnoreCase(evalPrompt))
                << "few-shot prompt exactly matches an eval scenario: " << fewShot;
            EXPECT_FALSE(fewShot.containsIgnoreCase(evalPrompt))
                << "few-shot prompt contains an eval scenario verbatim: " << fewShot;
        }
    }
}

} // namespace synth
