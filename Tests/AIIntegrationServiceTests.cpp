#include "AI/AIIntegrationService.h"
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

// setAuthToken() with a provider already installed forwards straight through to it.
TEST_F(AIIntegrationServiceTest, SetAuthTokenForwardsToInstalledProvider) {
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->setAuthToken("token-abc");

    EXPECT_EQ(rawProvider->lastAuthToken, "token-abc");
}

// REGRESSION LOCK: mirrors AIChatComponentTest.RefreshModelsSelectsModelWhenProviderInstalled-
// AfterConstruction's ordering — AccountService::onAccessTokenChanged can fire (and call
// AIIntegrationService::setAuthToken()) before MainComponent::initialiseCommon() installs the
// real provider. The token must not be lost: setProvider() has to re-push whatever was set
// earlier onto the provider it installs.
TEST_F(AIIntegrationServiceTest, SetAuthTokenBeforeProviderInstalledIsRePushedBySetProvider) {
    // No provider installed yet — setAuthToken() must still record the value.
    service->setAuthToken("token-xyz");

    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    EXPECT_TRUE(rawProvider->lastAuthToken.isEmpty());

    service->setProvider(std::move(provider));

    EXPECT_EQ(rawProvider->lastAuthToken, "token-xyz");
}

// P6-8: a successful response carrying a conversation id (the server persisted this exchange —
// Pro plan) must be captured and re-pushed to the provider immediately, so the NEXT sendMessage()
// call in this session continues the same server-side thread.
TEST_F(AIIntegrationServiceTest, ConversationIdCapturedFromResponseAndRePushedToProvider) {
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    rawProvider->mockConversationId = "conv-123";
    service->sendMessage("hi", [](const AIProvider::AIResponse&) {});

    EXPECT_TRUE(rawProvider->setConversationIdCalled);
    EXPECT_EQ(rawProvider->lastConversationId, "conv-123");
}

// The free-plan case: no conversationId on the response (mirrors RemoteProvider seeing no
// x-conversation-id header at all) must leave the provider untouched — never call
// setConversationId() with anything, empty or otherwise.
TEST_F(AIIntegrationServiceTest, EmptyConversationIdOnResponseDoesNotCallSetConversationId) {
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    // mockConversationId defaults to empty.
    service->sendMessage("hi", [](const AIProvider::AIResponse&) {});

    EXPECT_FALSE(rawProvider->setConversationIdCalled);
    EXPECT_TRUE(rawProvider->lastConversationId.isEmpty());
}

// setConversationId() before a provider is installed must not be lost — same re-push contract as
// SetAuthTokenBeforeProviderInstalledIsRePushedBySetProvider above.
TEST_F(AIIntegrationServiceTest, SetConversationIdBeforeProviderInstalledIsRePushedBySetProvider) {
    service->setConversationId("conv-xyz");

    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    EXPECT_FALSE(rawProvider->setConversationIdCalled);

    service->setProvider(std::move(provider));

    EXPECT_EQ(rawProvider->lastConversationId, "conv-xyz");
}

// P3-3 (anonymous trial): a mocked 402 TRIAL_EXHAUSTED response from the provider must reach the
// caller as a distinct AIErrorKind::TrialExhausted with the server's message intact — not
// collapsed into a generic failure. The actual HTTP-status-to-AIErrorKind mapping is RemoteProvider's
// job (see Tests/RemoteProviderTests.cpp's TrialExhaustedMapsToDistinctKindWithServerMessageIntact
// for that); this test locks down that AIIntegrationService::sendMessage() is a transparent
// pass-through and never rewrites error.kind/error.message on the way to the UI-facing callback.
TEST_F(AIIntegrationServiceTest, TrialExhaustedErrorPassesThroughWithServerMessageIntact) {
    auto provider = std::make_unique<MockAIProvider>();
    provider->shouldFail = true;
    provider->mockErrorKind = AIProvider::AIErrorKind::TrialExhausted;
    provider->mockErrorMessage = "Your free trial has been used up. Sign in with Google to continue.";
    service->setProvider(std::move(provider));

    AIProvider::AIResponse received;
    service->sendMessage("make a bass patch",
                         [&received](const AIProvider::AIResponse& response) { received = response; });

    EXPECT_FALSE(received.success);
    EXPECT_EQ(received.error.kind, AIProvider::AIErrorKind::TrialExhausted);
    EXPECT_EQ(received.error.message,
              juce::String("Your free trial has been used up. Sign in with Google to continue."));
}

// Same pass-through guarantee for the other new distinct kind (a service-wide capacity cap,
// unrelated to the caller's own trial/quota).
TEST_F(AIIntegrationServiceTest, ServiceCapacityExceededErrorPassesThroughWithServerMessageIntact) {
    auto provider = std::make_unique<MockAIProvider>();
    provider->shouldFail = true;
    provider->mockErrorKind = AIProvider::AIErrorKind::ServiceCapacityExceeded;
    provider->mockErrorMessage = "The service is at its daily capacity. Please try again later.";
    service->setProvider(std::move(provider));

    AIProvider::AIResponse received;
    service->sendMessage("make a bass patch",
                         [&received](const AIProvider::AIResponse& response) { received = response; });

    EXPECT_FALSE(received.success);
    EXPECT_EQ(received.error.kind, AIProvider::AIErrorKind::ServiceCapacityExceeded);
    EXPECT_EQ(received.error.message, juce::String("The service is at its daily capacity. Please try again later."));
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
    // Prefix match, not the exact string: the tail enumerates every module type that counts
    // as a signal source, so it changes whenever one is added. That already broke this
    // assertion once (#165 added Noise, fixed in #164) and again when Wavetable was added.
    // The prefix is the stable part and still pins the failure mode.
    EXPECT_TRUE(service->getLastPatchError().startsWith("Audio Output is not reachable from any"))
        << "actual: " << service->getLastPatchError().toStdString();
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
    // Prefix match, not the exact string: the tail enumerates every module type that counts
    // as a signal source, so it changes whenever one is added. That already broke this
    // assertion once (#165 added Noise, fixed in #164) and again when Wavetable was added.
    // The prefix is the stable part and still pins the failure mode.
    EXPECT_TRUE(service->getLastPatchError().startsWith("Audio Output is not reachable from any"))
        << "actual: " << service->getLastPatchError().toStdString();
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

// Regression: buildPatchAugmentedContent() used to silently return bare `text` when the live
// graph had zero nodes, giving the model no signal either way about whether a patch already
// exists. A fresh-session "create a bass patch" request needs an explicit "empty" marker instead,
// or the model has no way to distinguish "there is a patch but you weren't told about it" from
// "there is genuinely nothing yet".
TEST_F(AIIntegrationServiceTest, OutgoingRequestOnEmptyGraphStatesPatchIsEmpty) {
    // No nodes added to `graph` — deliberately left empty.
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Create a fat bass patch", nullptr, true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    const auto& content = rawProvider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("Current patch is empty"));
    EXPECT_TRUE(content.contains("Create a fat bass patch"));
    // Must not claim a patch state block that doesn't exist.
    EXPECT_FALSE(content.contains("Current patch state"));
}

// Regression: buildPatchAugmentedContent() used to embed graphToJSON() verbatim, including each
// node's "state" object. For a Sampler that is an absolute disk path; for a Hosted Plugin it would
// be the third-party plugin's opaque state blob. Neither is something the model can author (the
// trusted-only rule in AIStateMapper), so it must never leave the machine in the request payload.
TEST_F(AIIntegrationServiceTest, OutgoingRequestStripsNodeStateFromPatchContext) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ai-context-146.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> buffer(1, 512);
        buffer.clear();
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
        ASSERT_NE(writer, nullptr);
        stream.release(); // writer owns the stream now
        writer->writeFromAudioSampleBuffer(buffer, 0, 512);
    }

    auto* sampler = graph->addNode(std::make_unique<SamplerModule>())->getProcessor();
    ASSERT_TRUE(dynamic_cast<SamplerModule*>(sampler)->loadSampleFile(file));

    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    service->sendMessage("Add a filter", nullptr, true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    const auto& content = rawProvider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("Current patch state"));
    EXPECT_TRUE(content.contains("\"Sampler\""));
    EXPECT_FALSE(content.contains("\"state\""));
    EXPECT_FALSE(content.contains(file.getFullPathName()));
    EXPECT_FALSE(content.contains("ai-context-146.wav"));

    file.deleteFile();
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

#if SYNTH_ENABLE_TIMELINE
// ============================================================================
// Timeline tools toggle: the local model's automation/timeline authoring surface.
// ============================================================================

namespace {
// Records the schema each sendPrompt() call carried, so the schema-selection seam is observable.
class SchemaCapturingProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var& responseSchema,
                         std::function<void(const juce::String&)> = {}) override {
        lastSchema = responseSchema;
        AIResponse response;
        response.success = true;
        response.content = "ok";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"m"}, true);
    }
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    juce::String getProviderName() const override { return "SchemaCapturingProvider"; }

    juce::var lastSchema;
    juce::String model;
};

bool schemaOffersTimelineOps(const juce::var& schema) {
    auto* obj = schema.getDynamicObject();
    if (obj == nullptr)
        return false;
    auto* props = obj->getProperty("properties").getDynamicObject();
    return props != nullptr && props->hasProperty("timelineOps");
}
} // namespace

TEST_F(AIIntegrationServiceTest, TimelineToolsToggleGatesThePromptAndSchema) {
    // Baseline: no context, tools off — the system prompt is the pre-timeline one.
    const juce::String baseline = service->getHistory().front().content;
    EXPECT_FALSE(baseline.contains("timelineOps"));

    // Context alone doesn't enable anything (the preference is the switch)…
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    EXPECT_EQ(service->getHistory().front().content, baseline);

    // …and the switch alone needs the context (both are required).
    service->setTimelineContext(nullptr, nullptr);
    service->setTimelineToolsEnabled(true);
    EXPECT_FALSE(service->getHistory().front().content.contains("timelineOps"));

    // Both on: the prompt teaches the grammar, swapped IN PLACE (history size unchanged — a
    // mid-conversation toggle must not clear the chat).
    service->setTimelineContext(&doc, &transport);
    ASSERT_EQ(service->getHistory().size(), 1u);
    const juce::String enabled = service->getHistory().front().content;
    EXPECT_TRUE(enabled.contains("TIMELINE & AUTOMATION OPERATIONS"));
    EXPECT_TRUE(enabled.contains("writeLane"));
    EXPECT_TRUE(enabled.startsWith(baseline)) << "the timeline section is appended, never rewrites the patch prompt";

    // Off again: byte-identical to the baseline.
    service->setTimelineToolsEnabled(false);
    EXPECT_EQ(service->getHistory().front().content, baseline);
}

TEST_F(AIIntegrationServiceTest, TimelineToolsToggleSelectsTheExtendedSchema) {
    auto providerPtr = std::make_unique<SchemaCapturingProvider>();
    auto* provider = providerPtr.get();
    service->setProvider(std::move(providerPtr));

    service->sendMessage("make a bass", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);
    EXPECT_FALSE(schemaOffersTimelineOps(provider->lastSchema)) << "tools off: the plain patch schema";

    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);
    service->sendMessage("automate the cutoff", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);
    EXPECT_TRUE(schemaOffersTimelineOps(provider->lastSchema));
}

TEST_F(AIIntegrationServiceTest, AutomationTargetsSectionListsUuidBearingFloatParams) {
    // A node with a uuid and float params is addressable; one without a uuid is not.
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "test-uuid-1");
    // Deliberately uuid-less: graphToJSON (run first, for the patch-context section) mints one, so
    // by the time the targets section is built EVERY node is addressable — pinned below.
    auto anon = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(anon, nullptr);

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "T"); // non-empty so the arrangement section exists too
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    MockAIProvider* provider = nullptr;
    {
        auto p = std::make_unique<MockAIProvider>();
        provider = p.get();
        service->setProvider(std::move(p));
    }
    service->sendMessage("automate something", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);

    ASSERT_FALSE(provider->lastConversation.empty());
    const juce::String content = provider->lastConversation.back().content;
    ASSERT_TRUE(content.contains("## Automation targets"));
    // The uuid also (correctly) appears in the patch-state JSON above, so scope the assertions to
    // the targets section itself: two node lines — the explicit uuid AND the initially-uuid-less
    // twin, whose uuid graphToJSON minted while building the patch-context section.
    const juce::String targets = content.fromFirstOccurrenceOf("## Automation targets", false, false)
                                     .upToFirstOccurrenceOf("User request:", false, false);
    EXPECT_TRUE(targets.contains("\"test-uuid-1\"")) << "the addressable node's uuid is listed";
    EXPECT_TRUE(targets.contains("[")) << "params carry their raw ranges";
    EXPECT_EQ(juce::StringArray::fromLines(targets.trim()).size() - 1, 2)
        << "both oscillators are listed — graphToJSON ensures every live node carries a uuid";

    // Toggle off: the targets section disappears from the outgoing request entirely.
    service->setTimelineToolsEnabled(false);
    service->sendMessage("automate something", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);
    EXPECT_FALSE(provider->lastConversation.back().content.contains("## Automation targets"));
}

// ============================================================================
// Arrange mode: sendArrangeMessage → the hosted timeline.generate capability.
// ============================================================================

namespace {
// Records what sendCapabilityRequest() was handed, so the routing and the request body are both
// observable. Also counts sendPrompt() calls: an arrange request must never fall through to the
// conversation path.
class CapabilityCapturingProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema, std::function<void(const juce::String&)> = {}) override {
        ++sendPromptCalls;
        lastConversation = conversation;
        lastPromptSchema = responseSchema;
        AIResponse response;
        response.success = true;
        response.content = mockResponse;
        response.conversationId = mockConversationId;
        if (callback)
            callback(response);
        return {};
    }

    RequestId sendCapabilityRequest(const juce::String& capability, const juce::var& body,
                                    CompletionCallback callback) override {
        ++capabilityCalls;
        lastCapability = capability;
        lastBody = body;
        AIResponse response;
        response.success = true;
        response.content = mockResponse;
        response.conversationId = mockConversationId;
        if (callback)
            callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    juce::String getProviderName() const override { return "CapabilityCapturingProvider"; }
    void setConversationId(const juce::String& id) override { lastConversationId = id; }
    bool isHosted() const override { return hosted; }

    bool hosted = true; // flip to false to stand in for a local (Ollama-shaped) provider
    int sendPromptCalls = 0;
    int capabilityCalls = 0;
    juce::String lastCapability;
    juce::var lastBody;
    std::vector<Message> lastConversation;
    juce::var lastPromptSchema;
    juce::String mockResponse = R"({"timelineOps":[]})";
    juce::String mockConversationId;
    juce::String lastConversationId;
    juce::String model;
};

// Hosted, but WITHOUT a sendCapabilityRequest override — stands in for a hosted provider the
// AIProvider base-class default must protect (a typed failure, never a crash or a silent drop).
class HostedNoCapabilityProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    juce::String getProviderName() const override { return "HostedNoCapabilityProvider"; }
    bool isHosted() const override { return true; }

    juce::String model;
};
} // namespace

TEST_F(AIIntegrationServiceTest, ArrangeRequestRoutesToTimelineGenerateWithStructuredBody) {
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "arrange-uuid-1");

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "Melody");
    doc.addTrack(TrackKind::Automation, "Sweep");
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    auto providerPtr = std::make_unique<CapabilityCapturingProvider>();
    auto* provider = providerPtr.get();
    service->setProvider(std::move(providerPtr));

    bool called = false;
    service->sendArrangeMessage("build a 16 bar arrangement", [&](const AIProvider::AIResponse&) { called = true; });

    EXPECT_TRUE(called);
    EXPECT_EQ(provider->capabilityCalls, 1);
    EXPECT_EQ(provider->sendPromptCalls, 0) << "arrange mode never falls through to the conversation path";
    EXPECT_EQ(provider->lastCapability, juce::String("timeline.generate"));

    const juce::var& body = provider->lastBody;
    ASSERT_TRUE(body.isObject());

    // userPrompt is the RAW text — timeline.generate composes its own context sections
    // server-side, so the patch path's pre-wrapping must never leak in here.
    EXPECT_EQ(body["userPrompt"].toString(), juce::String("build a 16 bar arrangement"));
    EXPECT_FALSE(body["userPrompt"].toString().contains("Current patch state"));
    EXPECT_FALSE(body["userPrompt"].toString().contains("User request:"));

    EXPECT_TRUE(body["arrangementContext"].isString());
    EXPECT_TRUE(body["arrangementContext"].toString().isNotEmpty()) << "a non-empty doc summarises to something";

    ASSERT_TRUE(body["paramTargets"].isArray());
    ASSERT_GT(body["paramTargets"].getArray()->size(), 0) << "the uuid-bearing Oscillator's float params are offered";
    const juce::var target = (*body["paramTargets"].getArray())[0];
    EXPECT_EQ(target["nodeUuid"].toString(), juce::String("arrange-uuid-1"));
    EXPECT_TRUE(target["nodeName"].toString().isNotEmpty());
    EXPECT_TRUE(target["paramId"].toString().isNotEmpty());
    // Real numeric range + default, in the parameter's own units — presence and type, not values
    // (those belong to the module's own tests).
    EXPECT_TRUE(target["min"].isDouble() || target["min"].isInt());
    EXPECT_TRUE(target["max"].isDouble() || target["max"].isInt());
    EXPECT_TRUE(target["default"].isDouble() || target["default"].isInt());

    ASSERT_TRUE(body["availableTracks"].isArray());
    ASSERT_EQ(body["availableTracks"].getArray()->size(), 2);
    const juce::var track0 = (*body["availableTracks"].getArray())[0];
    const juce::var track1 = (*body["availableTracks"].getArray())[1];
    EXPECT_EQ(track0["name"].toString(), juce::String("Melody"));
    EXPECT_EQ(track0["kind"].toString(), juce::String("midi"));
    EXPECT_EQ(static_cast<int>(track0["index"]), 0);
    EXPECT_EQ(track1["name"].toString(), juce::String("Sweep"));
    EXPECT_EQ(track1["kind"].toString(), juce::String("automation"));
    EXPECT_EQ(static_cast<int>(track1["index"]), 1);

    // productName is the PROVIDER's field (RemoteProvider adds it) — the service must not
    // duplicate it into the caller-authored half.
    EXPECT_FALSE(body.hasProperty("productName"));
}

TEST_F(AIIntegrationServiceTest, ArrangeRequestParamTargetsAreCappedAtServerMax) {
    // Enough uuid-bearing nodes that the flat float-param count exceeds the cap.
    int paramsPerNode = 0;
    {
        auto probe = graph->addNode(std::make_unique<OscillatorModule>());
        ASSERT_NE(probe, nullptr);
        probe->properties.set("uuid", "probe-uuid");
        for (auto* p : probe->getProcessor()->getParameters())
            if (dynamic_cast<juce::AudioParameterFloat*>(p) != nullptr)
                ++paramsPerNode;
    }
    ASSERT_GT(paramsPerNode, 0);

    const int nodesNeeded = AIIntegrationService::kMaxRemoteParamTargets / paramsPerNode + 1;
    for (int i = 0; i < nodesNeeded; ++i) {
        auto node = graph->addNode(std::make_unique<OscillatorModule>());
        ASSERT_NE(node, nullptr);
        node->properties.set("uuid", "bulk-uuid-" + juce::String(i));
    }

    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    const juce::var body = service->buildArrangeRequestBody("automate everything");
    ASSERT_TRUE(body["paramTargets"].isArray());
    EXPECT_EQ(body["paramTargets"].getArray()->size(), AIIntegrationService::kMaxRemoteParamTargets)
        << "a longer list would be rejected by the server's input schema before any model saw it";
}

TEST_F(AIIntegrationServiceTest, ArrangeRequestOnEmptyTimelineSaysSoExplicitly) {
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    const juce::var body = service->buildArrangeRequestBody("start an arrangement");

    // The schema requires both keys but allows them empty — "a caller with nothing to say should
    // say so explicitly rather than have the field quietly go missing".
    ASSERT_TRUE(body.hasProperty("arrangementContext"));
    EXPECT_EQ(body["arrangementContext"].toString(), juce::String());
    ASSERT_TRUE(body["availableTracks"].isArray());
    EXPECT_EQ(body["availableTracks"].getArray()->size(), 0);
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageSharesHistoryAndConversationIdContractWithSendMessage) {
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    auto providerPtr = std::make_unique<CapabilityCapturingProvider>();
    auto* provider = providerPtr.get();
    provider->mockResponse = R"({"timelineOps":[{"op":"addTrack","kind":"midi","name":"Bass"}]})";
    provider->mockConversationId = "conv-arrange-1";
    service->setProvider(std::move(providerPtr));

    service->sendArrangeMessage("add a bass line", [](const AIProvider::AIResponse&) {});

    // Same bookkeeping as sendMessage(): user turn (raw text), assistant turn (envelope JSON),
    // and the Pro-plan conversation id captured and re-pushed to the provider.
    const auto& history = service->getHistory();
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[1].role, "user");
    EXPECT_EQ(history[1].content, "add a bass line");
    EXPECT_EQ(history[2].role, "assistant");
    EXPECT_EQ(history[2].content, provider->mockResponse);
    EXPECT_EQ(provider->lastConversationId, juce::String("conv-arrange-1"));
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageWithoutProviderFailsWithTypedError) {
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    AIProvider::AIResponse captured;
    bool called = false;
    service->sendArrangeMessage("anything", [&](const AIProvider::AIResponse& r) {
        captured = r;
        called = true;
    });

    ASSERT_TRUE(called);
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.error.kind, AIProvider::AIErrorKind::Schema);
    EXPECT_EQ(captured.error.message, juce::String("Error: No AI provider selected."));
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageOnHostedProviderWithoutCapabilitySupportFailsTyped) {
    // HostedNoCapabilityProvider does not override sendCapabilityRequest — the AIProvider
    // base-class default must deliver a typed failure, never crash or silently drop the callback.
    // (A NON-hosted provider never reaches that default: it routes to the sendPrompt transport,
    // pinned by the local-transport test below.)
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);
    service->setProvider(std::make_unique<HostedNoCapabilityProvider>());

    AIProvider::AIResponse captured;
    bool called = false;
    service->sendArrangeMessage("anything", [&](const AIProvider::AIResponse& r) {
        captured = r;
        called = true;
    });

    ASSERT_TRUE(called);
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.error.kind, AIProvider::AIErrorKind::Schema);
    EXPECT_TRUE(captured.error.message.contains("does not support capability requests"));
    EXPECT_TRUE(captured.error.message.contains("timeline.generate"));
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageOnLocalProviderComposesPromptWithEnvelopeOnlySchema) {
    // The parity rule's other half: a LOCAL provider serves the same arrange intent through
    // sendPrompt — the structured fields composed into the message (mirroring the server's own
    // section layout) plus an envelope-ONLY response schema, never the capability endpoint.
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "local-arrange-uuid");

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "Melody");
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    auto providerPtr = std::make_unique<CapabilityCapturingProvider>();
    auto* provider = providerPtr.get();
    provider->hosted = false;
    service->setProvider(std::move(providerPtr));

    bool called = false;
    service->sendArrangeMessage("automate the cutoff over 8 bars",
                                [&](const AIProvider::AIResponse&) { called = true; });

    EXPECT_TRUE(called);
    EXPECT_EQ(provider->capabilityCalls, 0) << "a local provider must never be asked for a capability endpoint";
    EXPECT_EQ(provider->sendPromptCalls, 1);

    // The outgoing message carries the SAME fields the hosted body would, section by section,
    // ending with the raw prompt + the envelope steering line.
    ASSERT_FALSE(provider->lastConversation.empty());
    const juce::String content = provider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("Arrangement context:"));
    EXPECT_TRUE(content.contains("Project tracks:"));
    EXPECT_TRUE(content.contains("\"Melody\""));
    EXPECT_TRUE(content.contains("Automation targets:"));
    EXPECT_TRUE(content.contains("\"local-arrange-uuid\""));
    EXPECT_TRUE(content.contains("automate the cutoff over 8 bars"));
    EXPECT_TRUE(content.contains("timelineOps"));

    // Envelope-only contract: timelineOps present AND required; no patch grammar in sight.
    auto* schemaObj = provider->lastPromptSchema.getDynamicObject();
    ASSERT_NE(schemaObj, nullptr);
    auto* props = schemaObj->getProperty("properties").getDynamicObject();
    ASSERT_NE(props, nullptr);
    EXPECT_TRUE(props->hasProperty("timelineOps"));
    EXPECT_FALSE(props->hasProperty("nodes"));
    ASSERT_TRUE(schemaObj->getProperty("required").isArray());
    EXPECT_TRUE(schemaObj->getProperty("required").getArray()->contains(juce::var("timelineOps")));

    // History keeps the RAW user text — the composed arrange context exists only on the wire,
    // exactly like sendMessage()'s patch-context splice.
    const auto& history = service->getHistory();
    ASSERT_GE(history.size(), 2u);
    EXPECT_EQ(history[1].content, "automate the cutoff over 8 bars");
}
#endif // SYNTH_ENABLE_TIMELINE

} // namespace synth
