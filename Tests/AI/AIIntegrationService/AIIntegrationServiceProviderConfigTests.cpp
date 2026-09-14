// Provider install/swap and the auth-token/timeout/conversation-id re-push contract.
#include "AIIntegrationServiceTestFixture.h"

namespace synth {

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

// REGRESSION LOCK for the request-timeout footgun class described in CLAUDE.md's "AI model
// discovery ordering" invariant: setProvider() must re-apply the last-configured timeout to
// whatever provider it installs, exactly like the auth-token/conversation-id re-push above —
// otherwise a provider swap would silently fall back to that provider's own hardcoded default
// (e.g. going from a configured 10-minute timeout back to 4 minutes), reintroducing the
// UI-watchdog/provider-timeout drift the single configurable value exists to prevent.
TEST_F(AIIntegrationServiceTest, ConfiguredRequestTimeoutSurvivesProviderSwap) {
    auto firstProvider = std::make_unique<MockAIProvider>();
    auto* rawFirstProvider = firstProvider.get();
    service->setProvider(std::move(firstProvider));

    // First provider ends up with the value the FIRST setProvider() call re-pushed
    // (AIIntegrationService's own default, 240000) — pinned here so the assertion below is
    // clearly about the SECOND provider inheriting the CONFIGURED value, not just any value.
    ASSERT_TRUE(rawFirstProvider->setRequestTimeoutMsCalled);
    EXPECT_EQ(rawFirstProvider->requestTimeoutMs, 240000);

    service->setRequestTimeoutMs(600000); // 10 minutes
    EXPECT_EQ(rawFirstProvider->requestTimeoutMs, 600000);
    EXPECT_EQ(service->getRequestTimeoutMs(), 600000);

    // Swap providers (e.g. Settings -> switching from Ollama to the hosted Remote provider).
    auto secondProvider = std::make_unique<MockAIProvider>();
    auto* rawSecondProvider = secondProvider.get();
    EXPECT_FALSE(rawSecondProvider->setRequestTimeoutMsCalled) << "sanity: a fresh mock starts unconfigured";

    service->setProvider(std::move(secondProvider));

    EXPECT_TRUE(rawSecondProvider->setRequestTimeoutMsCalled);
    EXPECT_EQ(rawSecondProvider->requestTimeoutMs, 600000)
        << "the newly installed provider must inherit the PREVIOUSLY CONFIGURED timeout, not its "
           "own hardcoded default";
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
