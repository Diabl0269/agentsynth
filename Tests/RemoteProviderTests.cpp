#include "../Source/AI/RemoteProvider.h"
#include "../Source/Branding.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>

namespace {

struct MockCompletionCallback {
    std::promise<std::pair<juce::String, bool>> promise;

    void operator()(const juce::StringArray& models, bool success) {
        promise.set_value({models.joinIntoString("|"), success});
    }

    std::pair<juce::String, bool> getResult() { return promise.get_future().get(); }
};

struct MockPromptCallback {
    std::promise<synth::AIProvider::AIResponse> promise;

    void operator()(const synth::AIProvider::AIResponse& response) { promise.set_value(response); }

    synth::AIProvider::AIResponse getResult() { return promise.get_future().get(); }
};

// Bounded-wait latch for "did the callback fire?" assertions. See OllamaProviderTests.cpp for the
// identical rationale: every wait has a timeout so a lost request fails the test instead of
// hanging it.
class CallbackLatch {
public:
    void fire() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            fired = true;
        }
        cv.notify_all();
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this] { return fired; });
    }

    bool hasFired() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return fired;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool fired = false;
};

constexpr std::chrono::milliseconds kCallbackTimeout{10000};

const juce::String kMockHost = "http://mock-host:8787";

// A non-void response schema, matching what AIIntegrationService::sendMessage() passes for a
// structured patch request (AIStateMapper::getPatchSchema()). Content is irrelevant to
// RemoteProvider — it never sends "format" the way OllamaProvider does — only isVoid() matters.
juce::var makeSchema() { return juce::var(new juce::DynamicObject()); }

synth::RemoteProvider::HttpResult makeSuccess(const juce::String& body) {
    synth::RemoteProvider::HttpResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

synth::RemoteProvider::HttpResult makeStatus(int status, const juce::String& body = "{}",
                                             const juce::StringPairArray& headers = {}) {
    synth::RemoteProvider::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    result.headers = headers;
    return result;
}

} // namespace

class RemoteProviderTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Nothing persistent between tests: each test constructs its own provider.
    }
};

// ============================================================================
// Request shape
// ============================================================================

TEST_F(RemoteProviderTest, RequestShapePlainTextConversation) {
    juce::String capturedUrl;
    juce::StringPairArray capturedHeaders;
    juce::String capturedBody;

    auto performer = [&](const juce::String& url, const juce::StringPairArray& headers, const juce::String& jsonBody,
                         int, const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedUrl = url;
        capturedHeaders = headers;
        capturedBody = jsonBody;
        return makeSuccess(R"({"data":{"nodes":[]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Make a bass patch"}};
    MockPromptCallback callback;
    provider.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(capturedUrl, kMockHost + "/v1/capability/patch.generate");
    EXPECT_TRUE(capturedHeaders.containsKey("Content-Type"));
    EXPECT_EQ(capturedHeaders.getValue("Content-Type", ""), juce::String("application/json"));
    EXPECT_FALSE(capturedHeaders.containsKey("Authorization"));

    juce::var parsedBody = juce::JSON::parse(capturedBody);
    ASSERT_TRUE(parsedBody.isObject());
    auto* obj = parsedBody.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->getProperty("productName").toString(), juce::String(synth::branding::kProductName));
    EXPECT_EQ(obj->getProperty("userPrompt").toString(), juce::String("Make a bass patch"));
    EXPECT_FALSE(obj->hasProperty("currentPatch"));
    EXPECT_FALSE(obj->hasProperty("promptVersion"));
}

// The design hinges on this: AIIntegrationService::buildPatchAugmentedContent() wraps the current
// graph into the last message as "Current patch state:...User request: ...", and that wrapper
// must pass through into userPrompt completely unchanged, never re-parsed or re-split.
TEST_F(RemoteProviderTest, RequestShapePassesAugmentedContentThroughUnchanged) {
    const juce::String wrapped =
        "Current patch state:\n```json\n{\"nodes\":[{\"id\":1}]}\n```\n\nUser request: add a filter";

    juce::String capturedBody;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String& jsonBody, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedBody = jsonBody;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    std::vector<synth::AIProvider::Message> conversation = {{"user", wrapped}};
    MockPromptCallback callback;
    provider.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    juce::var parsedBody = juce::JSON::parse(capturedBody);
    auto* obj = parsedBody.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->getProperty("userPrompt").toString(), wrapped);
    EXPECT_FALSE(obj->hasProperty("currentPatch"));
}

TEST_F(RemoteProviderTest, AuthorizationHeaderOnlySentWhenTokenSet) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);
    provider.setAuthToken("secret-token-123");

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    callback.getResult();
    provider.stopThread(5000);

    EXPECT_TRUE(capturedHeaders.containsKey("Authorization"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer secret-token-123"));
}

TEST_F(RemoteProviderTest, DeviceIdHeaderSentWhenConfiguredRegardlessOfAuthToken) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{}})");
    };

    // No setAuthToken() call: X-Device-Id must still be sent — it is an anonymous free-trial
    // signal when signed out, not something gated on having a bearer token.
    synth::RemoteProvider provider{kMockHost, performer, "device-uuid-1234"};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    callback.getResult();
    provider.stopThread(5000);

    EXPECT_EQ(capturedHeaders.getValue("X-Device-Id", ""), juce::String("device-uuid-1234"));
}

TEST_F(RemoteProviderTest, DeviceIdHeaderOmittedWhenNotConfigured) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(capturedHeaders.containsKey("X-Device-Id"));
}

TEST_F(RemoteProviderTest, ConversationIdHeaderOnlySentWhenSet) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    // No setConversationId() call: nothing to continue, so nothing should be sent — mirrors
    // AuthorizationHeaderOnlySentWhenTokenSet's premise for the auth header.
    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(capturedHeaders.containsKey("x-conversation-id"));
}

TEST_F(RemoteProviderTest, ConversationIdHeaderSentWhenSet) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);
    provider.setConversationId("conv-abc-123");

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    callback.getResult();
    provider.stopThread(5000);

    EXPECT_TRUE(capturedHeaders.containsKey("x-conversation-id"));
    EXPECT_EQ(capturedHeaders.getValue("x-conversation-id", ""), juce::String("conv-abc-123"));
}

// ============================================================================
// Fail-fast: no network call
// ============================================================================

TEST_F(RemoteProviderTest, VoidResponseSchemaFailsFastWithoutHittingNetwork) {
    bool performerInvoked = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerInvoked = true;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    // No schema argument: defaults to juce::var() (void), exactly what AIIntegrationService::
    // sendMessage() passes for a plain conversational turn.
    provider.sendPrompt({{"user", "hello"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerInvoked);
}

TEST_F(RemoteProviderTest, EmptyConversationFailsFastWithoutHittingNetwork) {
    bool performerInvoked = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerInvoked = true;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt({}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerInvoked);
}

TEST_F(RemoteProviderTest, BlankLastMessageFailsFastWithoutHittingNetwork) {
    bool performerInvoked = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerInvoked = true;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "   "}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerInvoked);
}

// ============================================================================
// HTTP status -> AIErrorKind mapping
// ============================================================================

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

// ============================================================================
// Success / malformed bodies
// ============================================================================

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

// ============================================================================
// Transport-level failures
// ============================================================================

TEST_F(RemoteProviderTest, TransportFailureMapsToNetwork) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        synth::RemoteProvider::HttpResult result;
        result.transportFailed = true;
        result.errorMessage = "Could not resolve host";
        return result;
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Network);
}

TEST_F(RemoteProviderTest, TimeoutMapsToTimeout) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        synth::RemoteProvider::HttpResult result;
        result.timedOut = true;
        result.errorMessage = "timed out";
        return result;
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Timeout);
}

// ============================================================================
// Cancellation
// ============================================================================

// A request still sitting in pendingRequests (never dequeued) when cancel() is called must be
// delivered Cancelled promptly, and the fake performer must never be invoked for it at all.
TEST_F(RemoteProviderTest, CancellingQueuedRequestDeliversCancelledWithoutInvokingPerformer) {
    auto firstEntered = std::make_shared<CallbackLatch>();
    auto firstGate = std::make_shared<CallbackLatch>(); // released to let the first request finish
    std::atomic<int> performerCallCount{0};

    auto performer = [firstEntered, firstGate,
                      &performerCallCount](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                                           const std::atomic<bool>& cancelled) -> synth::RemoteProvider::HttpResult {
        const int callIndex = performerCallCount.fetch_add(1);
        if (callIndex == 0) {
            firstEntered->fire();
            // Poll like a real curl progress callback would, but this request is never cancelled
            // in this test — it is released explicitly once the assertions below are done.
            int elapsedMs = 0;
            while (!firstGate->hasFired() && !cancelled.load() && elapsedMs < 15000) {
                juce::Thread::sleep(5);
                elapsedMs += 5;
            }
            return makeSuccess(R"({"data":{}})");
        }
        // Must never be reached: request 2 is cancelled while still queued, before the worker
        // ever pops it.
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    auto firstDone = std::make_shared<CallbackLatch>();
    provider.sendPrompt(
        {{"user", "first (long)"}}, [firstDone](const synth::AIProvider::AIResponse&) { firstDone->fire(); },
        makeSchema());
    ASSERT_TRUE(firstEntered->waitFor(kCallbackTimeout)) << "the worker never started the first request";

    std::atomic<int> secondCallCount{0};
    std::atomic<synth::AIProvider::AIErrorKind> secondKind{synth::AIProvider::AIErrorKind::None};
    auto secondDone = std::make_shared<CallbackLatch>();
    const auto secondId = provider.sendPrompt(
        {{"user", "second (should be cancelled while queued)"}},
        [&secondCallCount, &secondKind, secondDone](const synth::AIProvider::AIResponse& r) {
            secondCallCount.fetch_add(1);
            secondKind.store(r.error.kind);
            secondDone->fire();
        },
        makeSchema());

    provider.cancel(secondId);

    ASSERT_TRUE(secondDone->waitFor(kCallbackTimeout)) << "cancelling a queued request left the caller hanging";
    EXPECT_EQ(secondCallCount.load(), 1);
    EXPECT_EQ(secondKind.load(), synth::AIProvider::AIErrorKind::Cancelled);
    EXPECT_EQ(performerCallCount.load(), 1) << "the fake performer must not be invoked for a request cancelled "
                                               "while it was still queued";

    firstGate->fire();
    ASSERT_TRUE(firstDone->waitFor(kCallbackTimeout)) << "the first request never completed";

    provider.stopThread(5000);
}

// A request whose fake performer is currently "in flight" must notice cancellation via the
// `cancelled` atomic it was handed — exactly what CURLOPT_XFERINFOFUNCTION does for real curl —
// and the provider must deliver Cancelled once the performer returns.
TEST_F(RemoteProviderTest, CancellingInFlightRequestDeliversCancelledOnceNoticed) {
    auto entered = std::make_shared<CallbackLatch>();

    auto performer = [entered](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                               const std::atomic<bool>& cancelled) -> synth::RemoteProvider::HttpResult {
        entered->fire();
        int elapsedMs = 0;
        while (!cancelled.load() && elapsedMs < 15000) {
            juce::Thread::sleep(5);
            elapsedMs += 5;
        }
        synth::RemoteProvider::HttpResult result;
        result.transportFailed = true; // simulates CURLE_ABORTED_BY_CALLBACK; irrelevant to the
                                       // caller, which checks `cancelled` before looking at this.
        result.errorMessage = "aborted";
        return result;
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    std::atomic<int> callCount{0};
    std::atomic<synth::AIProvider::AIErrorKind> kind{synth::AIProvider::AIErrorKind::None};
    auto done = std::make_shared<CallbackLatch>();

    const auto id = provider.sendPrompt(
        {{"user", "long generation"}},
        [&callCount, &kind, done](const synth::AIProvider::AIResponse& r) {
            callCount.fetch_add(1);
            kind.store(r.error.kind);
            done->fire();
        },
        makeSchema());

    ASSERT_TRUE(entered->waitFor(kCallbackTimeout)) << "the worker never reached the fake in-flight performer";

    provider.cancel(id);

    ASSERT_TRUE(done->waitFor(kCallbackTimeout)) << "cancel() left the caller hanging with no callback";
    EXPECT_EQ(callCount.load(), 1);
    EXPECT_EQ(kind.load(), synth::AIProvider::AIErrorKind::Cancelled);

    provider.stopThread(5000);
}

// ============================================================================
// Misc interface conformance
// ============================================================================

TEST_F(RemoteProviderTest, ProviderNameIsRemote) {
    synth::RemoteProvider provider{kMockHost};
    EXPECT_EQ(provider.getProviderName(), juce::String("Remote"));
}

TEST_F(RemoteProviderTest, FetchAvailableModelsReturnsEmptyListWithSuccess) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        ADD_FAILURE() << "fetchAvailableModels must not hit the network";
        return {};
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockCompletionCallback callback;
    provider.fetchAvailableModels(
        [&callback](const juce::StringArray& models, bool success) { callback(models, success); });

    auto result = callback.getResult();
    EXPECT_TRUE(std::get<1>(result));
    EXPECT_TRUE(std::get<0>(result).isEmpty());
}

TEST_F(RemoteProviderTest, SetAndGetCurrentModelRoundTripsCosmetically) {
    synth::RemoteProvider provider{kMockHost};
    provider.setModel("whatever-label");
    EXPECT_EQ(provider.getCurrentModel(), juce::String("whatever-label"));
}

// ============================================================================
// Capability requests (sendCapabilityRequest — timeline.generate / arrange mode)
// ============================================================================

namespace {

// The body AIIntegrationService::buildArrangeRequestBody() would author — every field the
// timeline.generate input schema wants except productName, which the PROVIDER must add.
juce::var makeCapabilityBody(const juce::String& userPrompt = "automate the filter cutoff") {
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("userPrompt", userPrompt);
    body->setProperty("arrangementContext", "120 BPM, 4/4");
    juce::Array<juce::var> targets;
    juce::DynamicObject::Ptr target = new juce::DynamicObject();
    target->setProperty("nodeUuid", "uuid-1");
    target->setProperty("nodeName", "Filter");
    target->setProperty("paramId", "cutoff");
    target->setProperty("min", 20.0);
    target->setProperty("max", 20000.0);
    target->setProperty("default", 1000.0);
    targets.add(juce::var(target.get()));
    body->setProperty("paramTargets", targets);
    body->setProperty("availableTracks", juce::Array<juce::var>{});
    return juce::var(body.get());
}

} // namespace

TEST_F(RemoteProviderTest, CapabilityRequestHitsNamedEndpointWithCallerBodyPlusProductName) {
    juce::String capturedUrl;
    juce::String capturedBody;

    auto performer = [&](const juce::String& url, const juce::StringPairArray&, const juce::String& jsonBody, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedUrl = url;
        capturedBody = jsonBody;
        return makeSuccess(R"({"data":{"timelineOps":[]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendCapabilityRequest("timeline.generate", makeCapabilityBody(),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(capturedUrl, kMockHost + "/v1/capability/timeline.generate");

    juce::var parsedBody = juce::JSON::parse(capturedBody);
    ASSERT_TRUE(parsedBody.isObject());
    auto* obj = parsedBody.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    // The provider adds productName; every caller-authored field passes through verbatim.
    EXPECT_EQ(obj->getProperty("productName").toString(), juce::String(synth::branding::kProductName));
    EXPECT_EQ(obj->getProperty("userPrompt").toString(), juce::String("automate the filter cutoff"));
    EXPECT_EQ(obj->getProperty("arrangementContext").toString(), juce::String("120 BPM, 4/4"));
    ASSERT_TRUE(obj->getProperty("paramTargets").isArray());
    ASSERT_EQ(obj->getProperty("paramTargets").getArray()->size(), 1);
    const juce::var target = (*obj->getProperty("paramTargets").getArray())[0];
    EXPECT_EQ(target["nodeUuid"].toString(), juce::String("uuid-1"));
    EXPECT_EQ(target["paramId"].toString(), juce::String("cutoff"));
    EXPECT_EQ(static_cast<double>(target["default"]), 1000.0);
    ASSERT_TRUE(obj->getProperty("availableTracks").isArray());
    EXPECT_EQ(obj->getProperty("availableTracks").getArray()->size(), 0);
}

TEST_F(RemoteProviderTest, CapabilityRequestCarriesSameIdentityHeadersAsPatchPath) {
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{"timelineOps":[]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer, "device-abc"};
    provider.setTestMode(true);
    provider.setAuthToken("token-123");
    provider.setConversationId("conv-9");

    MockPromptCallback callback;
    provider.sendCapabilityRequest("timeline.generate", makeCapabilityBody(),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(capturedHeaders.getValue("Content-Type", ""), juce::String("application/json"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer token-123"));
    EXPECT_EQ(capturedHeaders.getValue("X-Device-Id", ""), juce::String("device-abc"));
    EXPECT_EQ(capturedHeaders.getValue("x-conversation-id", ""), juce::String("conv-9"));
}

TEST_F(RemoteProviderTest, CapabilityRequestWithoutUserPromptFailsFastWithoutHittingNetwork) {
    bool performerCalled = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerCalled = true;
        return makeSuccess("{}");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    juce::DynamicObject::Ptr noPrompt = new juce::DynamicObject();
    noPrompt->setProperty("arrangementContext", "");

    MockPromptCallback callback;
    provider.sendCapabilityRequest("timeline.generate", juce::var(noPrompt.get()),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerCalled);
}

TEST_F(RemoteProviderTest, CapabilityRequestWithEmptyCapabilityNameFailsFastWithoutHittingNetwork) {
    bool performerCalled = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerCalled = true;
        return makeSuccess("{}");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendCapabilityRequest("", makeCapabilityBody(),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerCalled);
}

// Entitlement enforcement must be byte-identical across capabilities: the SAME status→kind
// mapping serves patch.generate and timeline.generate (one mapping in processRequest()), and the
// server's own message survives verbatim — the UI renders it, so a canned client string here
// would hide what the server actually said.
TEST_F(RemoteProviderTest, CapabilityQuotaExceededMapsToQuotaWithServerMessageIntact) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(429, R"({"error":{"code":"QUOTA_EXCEEDED",)"
                               R"("message":"Your monthly request quota is used up. Upgrading to Pro raises it."}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendCapabilityRequest("timeline.generate", makeCapabilityBody(),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Quota);
    EXPECT_EQ(result.error.message, juce::String("Your monthly request quota is used up. Upgrading to Pro raises it."));
}

TEST_F(RemoteProviderTest, CapabilityTrialExhaustedMapsToDistinctKindWithServerMessageIntact) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(402, R"({"error":{"code":"TRIAL_EXHAUSTED",)"
                               R"("message":"Your free trial is used up. Sign in to keep going."}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendCapabilityRequest("timeline.generate", makeCapabilityBody(),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::TrialExhausted);
    EXPECT_EQ(result.error.message, juce::String("Your free trial is used up. Sign in to keep going."));
}

// The gateway answers every capability the same way ({"data": <output>}); for timeline.generate
// the data is the timelineOps envelope, and the reserialized envelope is exactly what
// AIIntegrationService::extractTimelineOps() consumes downstream.
TEST_F(RemoteProviderTest, CapabilitySuccessReturnsEnvelopeReserializedAsJsonText) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess(R"({"data":{"timelineOps":[{"op":"addTrack","kind":"midi","name":"Bass"}]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendCapabilityRequest("timeline.generate", makeCapabilityBody(),
                                   [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    const juce::var parsed = juce::JSON::parse(result.content);
    ASSERT_TRUE(parsed.isObject());
    ASSERT_TRUE(parsed["timelineOps"].isArray());
    EXPECT_EQ(parsed["timelineOps"].getArray()->size(), 1);
    EXPECT_EQ((*parsed["timelineOps"].getArray())[0]["op"].toString(), juce::String("addTrack"));
}
