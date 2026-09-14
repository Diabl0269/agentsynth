// Chat-history bookkeeping: system-prompt presence, trimming, clearing, and the cancel-vs-complete
// append contract.
#include "AIIntegrationServiceTestFixture.h"

namespace synth {

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

} // namespace synth
