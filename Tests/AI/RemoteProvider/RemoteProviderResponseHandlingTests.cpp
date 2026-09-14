// HTTP status -> AIErrorKind mapping, and success/malformed response bodies.
#include "RemoteProviderTestFixture.h"

struct StatusMappingCase {
    int httpStatus;
    synth::AIProvider::AIErrorKind expectedKind;
};

class RemoteProviderStatusMappingTest : public ::testing::TestWithParam<StatusMappingCase> {};

TEST_P(RemoteProviderStatusMappingTest, MapsStatusToExpectedErrorKind) {
    const auto testCase = GetParam();

    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(testCase.httpStatus, "{}");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, testCase.expectedKind) << "for HTTP " << testCase.httpStatus;
}

INSTANTIATE_TEST_SUITE_P(HttpStatuses, RemoteProviderStatusMappingTest,
                         ::testing::Values(StatusMappingCase{401, synth::AIProvider::AIErrorKind::Auth},
                                           StatusMappingCase{403, synth::AIProvider::AIErrorKind::Auth},
                                           StatusMappingCase{402, synth::AIProvider::AIErrorKind::Quota},
                                           StatusMappingCase{400, synth::AIProvider::AIErrorKind::Schema},
                                           StatusMappingCase{404, synth::AIProvider::AIErrorKind::Schema},
                                           StatusMappingCase{500, synth::AIProvider::AIErrorKind::Server},
                                           StatusMappingCase{502, synth::AIProvider::AIErrorKind::Server},
                                           StatusMappingCase{418, synth::AIProvider::AIErrorKind::Server}));

TEST_F(RemoteProviderTest, MapsTooManyRequestsToRateLimitAndReadsRetryAfter) {
    juce::StringPairArray headers;
    headers.set("Retry-After", "45");

    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(429, "{}", headers);
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::RateLimit);
    EXPECT_EQ(result.error.retryAfterSeconds, 45);
}

// P4-3's monthly quota (enforce-quota.ts) answers 429 QUOTA_EXCEEDED — this must map to Quota
// (the P4-4 upgrade-bubble UI keys off this kind), not the generic RateLimit above.
TEST_F(RemoteProviderTest, MapsQuotaExceeded429ToQuotaError) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(429, R"({"error":{"code":"QUOTA_EXCEEDED",)"
                               R"("message":"Your monthly request quota is used up. Upgrading to Pro raises it."}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Quota);
    EXPECT_EQ(result.error.message, juce::String("Your monthly request quota is used up. Upgrading to Pro raises it."));
}

TEST_F(RemoteProviderTest, SuccessReturnsDataReserializedAsJsonText) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess(R"({"data":{"nodes":[{"id":1,"type":"Oscillator"}]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    juce::var parsedContent = juce::JSON::parse(result.content);
    ASSERT_TRUE(parsedContent.isObject());
    auto* nodes = parsedContent.getProperty("nodes", juce::var()).getArray();
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->size(), 1);
    EXPECT_EQ((*nodes)[0].getProperty("type", juce::var()).toString(), juce::String("Oscillator"));
}

// The full round trip a single-session conversation relies on: a response's x-conversation-id
// header is surfaced on AIResponse::conversationId (RemoteProvider's half of the contract —
// AIIntegrationServiceTests.cpp covers the capture-and-repush half on top of this).
TEST_F(RemoteProviderTest, ConversationIdHeaderCapturedFromResponseIntoAIResponse) {
    juce::StringPairArray responseHeaders;
    responseHeaders.set("x-conversation-id", "conv-server-issued");

    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(200, R"({"data":{}})", responseHeaders);
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.conversationId, juce::String("conv-server-issued"));
}

// P6-9's sibling to the test above: a response's x-message-id header is surfaced on
// AIResponse::messageId, present under the exact same server-side condition (Pro plan,
// persistence succeeded) as x-conversation-id.
TEST_F(RemoteProviderTest, MessageIdHeaderCapturedFromResponseIntoAIResponse) {
    juce::StringPairArray responseHeaders;
    responseHeaders.set("x-conversation-id", "conv-server-issued");
    responseHeaders.set("x-message-id", "msg-server-issued");

    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(200, R"({"data":{}})", responseHeaders);
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.conversationId, juce::String("conv-server-issued"));
    EXPECT_EQ(result.messageId, juce::String("msg-server-issued"));
}

// The free-plan case (P6-8): the server sends no header at all when it didn't persist. Must not
// be confused with an empty-but-present header — both collapse to an empty conversationId, which
// is exactly the "nothing to resend" state AIIntegrationService's capture gate checks for.
TEST_F(RemoteProviderTest, MissingConversationIdHeaderLeavesAIResponseFieldEmpty) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess(R"({"data":{}})"); // no headers set at all
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.conversationId.isEmpty());
    EXPECT_TRUE(result.messageId.isEmpty());
}

TEST_F(RemoteProviderTest, UnparseableSuccessBodyMapsToSchema) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess("not json at all");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
}

TEST_F(RemoteProviderTest, SuccessBodyWithNoDataKeyMapsToSchema) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess(R"({"foo":1})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
}

TEST_F(RemoteProviderTest, ErrorBodyMessageIsAppendedToDeliveredError) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(502, R"({"error":{"code":"GENERATION_FAILED","message":"model timed out mid-generation"}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Server);
    EXPECT_TRUE(result.error.message.contains("model timed out mid-generation"));
}

TEST_F(RemoteProviderTest, TrialExhaustedMapsToDistinctKindWithServerMessageIntact) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(402, R"({"error":{"code":"TRIAL_EXHAUSTED",)"
                               R"("message":"Your free trial has been used up. Sign in with Google to continue."}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::TrialExhausted);
    EXPECT_EQ(result.error.message, juce::String("Your free trial has been used up. Sign in with Google to continue."));
}

// A 402 with no TRIAL_EXHAUSTED code (e.g. a signed-in paid account genuinely out of quota) must
// keep the pre-existing generic Quota mapping — this is a REGRESSION LOCK on the existing
// StatusMappingCase{402, Quota} behavior now that 402 has a second branch.
TEST_F(RemoteProviderTest, NonTrialFourOhTwoStaysGenericQuota) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(402, "{}");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Quota);
}

TEST_F(RemoteProviderTest, ServiceCapacityExceededMapsToDistinctKindWithServerMessageIntact) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(503, R"({"error":{"code":"SERVICE_CAPACITY_EXCEEDED",)"
                               R"("message":"The service is at its daily capacity. Please try again later."}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::ServiceCapacityExceeded);
    EXPECT_EQ(result.error.message, juce::String("The service is at its daily capacity. Please try again later."));
}

// A 503 with no SERVICE_CAPACITY_EXCEEDED code must fall through to the generic Server mapping,
// same as any other unrecognized 5xx.
TEST_F(RemoteProviderTest, NonCapacityFiveOhThreeFallsBackToGenericServer) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(503, "{}");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Server);
}
