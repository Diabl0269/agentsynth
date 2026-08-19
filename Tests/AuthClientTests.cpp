#include "../Source/AI/AuthClient.h"
#include <atomic>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <map>

namespace {

const juce::String kHost = "http://mock-host:8787";
const juce::String kClientId = "synth-desktop";
const juce::String kDeviceId = "device-uuid-1234";

synth::AuthClient::HttpResult makeStatus(int status, const juce::String& body,
                                         const juce::StringPairArray& headers = {}) {
    synth::AuthClient::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    result.headers = headers;
    return result;
}

synth::AuthClient::HttpResult makeTransportFailure(const juce::String& message = "Could not resolve host") {
    synth::AuthClient::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = message;
    return result;
}

std::atomic<bool> kNeverCancelled{false};

// Parses a application/x-www-form-urlencoded body into a key -> decoded-value map, for asserting
// on the exact fields a request sent.
std::map<juce::String, juce::String> parseForm(const juce::String& body) {
    std::map<juce::String, juce::String> result;
    for (const auto& pair : juce::StringArray::fromTokens(body, "&", "")) {
        const int eq = pair.indexOfChar('=');
        if (eq < 0)
            continue;
        const auto key = pair.substring(0, eq);
        const auto value = juce::URL::removeEscapeChars(pair.substring(eq + 1));
        result[key] = value;
    }
    return result;
}

} // namespace

// ============================================================================
// requestDeviceCode
// ============================================================================

TEST(AuthClientTest, RequestDeviceCodeHappyPathParsesEveryField) {
    juce::String capturedMethod, capturedUrl, capturedBody;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedUrl = url;
        capturedHeaders = headers;
        capturedBody = body;
        return makeStatus(200, R"({
            "device_code": "devcode-abc",
            "user_code": "ABCD-1234",
            "verification_uri": "https://example.com/activate",
            "verification_uri_complete": "https://example.com/activate?user_code=ABCD-1234",
            "expires_in": 900,
            "interval": 5
        })");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.requestDeviceCode(kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.deviceCode, juce::String("devcode-abc"));
    EXPECT_EQ(result.userCode, juce::String("ABCD-1234"));
    EXPECT_EQ(result.verificationUri, juce::String("https://example.com/activate"));
    EXPECT_EQ(result.verificationUriComplete, juce::String("https://example.com/activate?user_code=ABCD-1234"));
    EXPECT_EQ(result.expiresIn, 900);
    EXPECT_EQ(result.interval, 5);
    EXPECT_TRUE(result.transportError.isEmpty());

    EXPECT_EQ(capturedMethod, juce::String("POST"));
    EXPECT_EQ(capturedUrl, kHost + "/v1/auth/device/code");
    EXPECT_EQ(capturedHeaders.getValue("Content-Type", ""), juce::String("application/x-www-form-urlencoded"));
    EXPECT_EQ(parseForm(capturedBody)["client_id"], kClientId);
}

TEST(AuthClientTest, RequestDeviceCodeIncludesDeviceIdWhenConfigured) {
    juce::String capturedBody;

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(200, R"({
            "device_code": "devcode-abc",
            "user_code": "ABCD-1234",
            "verification_uri": "https://example.com/activate",
            "verification_uri_complete": "https://example.com/activate?user_code=ABCD-1234",
            "expires_in": 900,
            "interval": 5
        })");
    };

    synth::AuthClient client{kHost, kClientId, performer, kDeviceId};
    const auto result = client.requestDeviceCode(kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(parseForm(capturedBody)["device_id"], kDeviceId);
}

TEST(AuthClientTest, RequestDeviceCodeOmitsDeviceIdWhenNotConfigured) {
    juce::String capturedBody;

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(200, R"({
            "device_code": "devcode-abc",
            "user_code": "ABCD-1234",
            "verification_uri": "https://example.com/activate",
            "verification_uri_complete": "https://example.com/activate?user_code=ABCD-1234",
            "expires_in": 900,
            "interval": 5
        })");
    };

    // 3-arg constructor: deviceId defaults to empty, so the field must be omitted entirely
    // (not sent as "device_id=") rather than a parallel mechanism being required to opt out.
    synth::AuthClient client{kHost, kClientId, performer};
    client.requestDeviceCode(kNeverCancelled);

    EXPECT_EQ(parseForm(capturedBody).count("device_id"), 0u);
}

TEST(AuthClientTest, RequestDeviceCodeTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.requestDeviceCode(kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

// ============================================================================
// pollDeviceToken
// ============================================================================

TEST(AuthClientTest, PollDeviceTokenSuccessParsesEveryFieldAndSendsCorrectBody) {
    juce::String capturedUrl, capturedBody;

    auto performer = [&](const juce::String&, const juce::String& url, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedUrl = url;
        capturedBody = body;
        return makeStatus(200, R"({
            "access_token": "access-xyz",
            "token_type": "Bearer",
            "expires_in": 3600,
            "refresh_token": "refresh-xyz"
        })");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.pollDeviceToken("devcode-abc", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.errorCode.isEmpty());
    EXPECT_EQ(result.accessToken, juce::String("access-xyz"));
    EXPECT_EQ(result.expiresIn, 3600);
    EXPECT_EQ(result.refreshToken, juce::String("refresh-xyz"));

    EXPECT_EQ(capturedUrl, kHost + "/v1/auth/token");
    const auto form = parseForm(capturedBody);
    EXPECT_EQ(form.at("grant_type"), juce::String("urn:ietf:params:oauth:grant-type:device_code"));
    EXPECT_EQ(form.at("device_code"), juce::String("devcode-abc"));
    EXPECT_EQ(form.at("client_id"), kClientId);
}

TEST(AuthClientTest, PollDeviceTokenIncludesDeviceIdWhenConfigured) {
    juce::String capturedBody;

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(200, R"({"access_token":"a","token_type":"Bearer","expires_in":60,"refresh_token":"r"})");
    };

    synth::AuthClient client{kHost, kClientId, performer, kDeviceId};
    client.pollDeviceToken("devcode-abc", kNeverCancelled);

    EXPECT_EQ(parseForm(capturedBody)["device_id"], kDeviceId);
}

struct PollErrorCase {
    juce::String errorCode;
};

class AuthClientPollErrorTest : public ::testing::TestWithParam<PollErrorCase> {};

TEST_P(AuthClientPollErrorTest, MapsErrorCodeAndDescription) {
    const auto testCase = GetParam();

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                         int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("error", testCase.errorCode);
        obj->setProperty("error_description", "description for " + testCase.errorCode);
        return makeStatus(400, juce::JSON::toString(juce::var(obj.get())));
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.pollDeviceToken("devcode-abc", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isEmpty()) << "a well-formed 400 body must not be reported as a transport error";
    EXPECT_EQ(result.errorCode, testCase.errorCode);
    EXPECT_EQ(result.errorDescription, "description for " + testCase.errorCode);
}

INSTANTIATE_TEST_SUITE_P(ErrorCodes, AuthClientPollErrorTest,
                         ::testing::Values(PollErrorCase{"authorization_pending"}, PollErrorCase{"slow_down"},
                                           PollErrorCase{"expired_token"}, PollErrorCase{"access_denied"},
                                           PollErrorCase{"invalid_grant"}, PollErrorCase{"invalid_request"}));

TEST(AuthClientTest, PollDeviceTokenTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeTransportFailure("timed out");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.pollDeviceToken("devcode-abc", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.errorCode.isEmpty());
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

// ============================================================================
// refreshToken
// ============================================================================

TEST(AuthClientTest, RefreshTokenSuccessSendsCorrectBody) {
    juce::String capturedBody;

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(200, R"({"access_token":"a2","token_type":"Bearer","expires_in":60,"refresh_token":"r2"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.refreshToken("old-refresh-token", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.accessToken, juce::String("a2"));
    EXPECT_EQ(result.refreshToken, juce::String("r2"));
    EXPECT_EQ(result.expiresIn, 60);

    const auto form = parseForm(capturedBody);
    EXPECT_EQ(form.at("grant_type"), juce::String("refresh_token"));
    EXPECT_EQ(form.at("refresh_token"), juce::String("old-refresh-token"));
    EXPECT_EQ(form.at("client_id"), kClientId);
}

TEST(AuthClientTest, RefreshTokenIncludesDeviceIdWhenConfigured) {
    juce::String capturedBody;

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(200, R"({"access_token":"a2","token_type":"Bearer","expires_in":60,"refresh_token":"r2"})");
    };

    synth::AuthClient client{kHost, kClientId, performer, kDeviceId};
    client.refreshToken("old-refresh-token", kNeverCancelled);

    EXPECT_EQ(parseForm(capturedBody)["device_id"], kDeviceId);
}

TEST(AuthClientTest, RefreshTokenInvalidGrant) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(400, R"({"error":"invalid_grant","error_description":"refresh token revoked"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.refreshToken("dead-token", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.errorCode, juce::String("invalid_grant"));
    EXPECT_EQ(result.errorDescription, juce::String("refresh token revoked"));
}

// ============================================================================
// fetchMe
// ============================================================================

TEST(AuthClientTest, FetchMeSuccessParsesFieldsAndSendsBearerHeader) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/auth/me");
        return makeStatus(
            200, R"({"id":"user-1","email":"jane@example.com","display_name":"Jane","created_at":"2024-01-01"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchMe("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.id, juce::String("user-1"));
    EXPECT_EQ(result.email, juce::String("jane@example.com"));
    EXPECT_EQ(result.displayName, juce::String("Jane"));

    EXPECT_EQ(capturedMethod, juce::String("GET"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

TEST(AuthClientTest, FetchMeNullEmailAndDisplayNameBecomeEmptyStrings) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, R"({"id":"user-2","email":null,"display_name":null,"created_at":"2024-01-01"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchMe("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.id, juce::String("user-2"));
    EXPECT_TRUE(result.email.isEmpty());
    EXPECT_TRUE(result.displayName.isEmpty());
}

TEST(AuthClientTest, FetchMeUnauthorized) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(401, R"({"error":"invalid_token"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchMe("bad-token", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

// ============================================================================
// fetchEntitlement
// ============================================================================

TEST(AuthClientTest, FetchEntitlementSuccessParsesFieldsIncludingUsage) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/entitlement");
        return makeStatus(200, R"({"plan":"pro","status":"active","period_end":"2026-09-11T09:14:00.000Z",)"
                               R"("cancel_at_period_end":false,"limits":{"monthly_requests":10000},)"
                               R"("usage":{"requests_used":743,"period_start":"2026-08-01"},)"
                               R"("token":"eyJ...","expires_at":"2026-08-18T09:14:00.000Z",)"
                               R"("refresh_after":"2026-08-11T10:14:00.000Z"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchEntitlement("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.plan, juce::String("pro"));
    EXPECT_EQ(result.status, juce::String("active"));
    EXPECT_EQ(result.periodEndIso, juce::String("2026-09-11T09:14:00.000Z"));
    EXPECT_FALSE(result.cancelAtPeriodEnd);
    EXPECT_EQ(result.monthlyRequestLimit, 10000);
    EXPECT_EQ(result.requestsUsed, 743);
    EXPECT_EQ(result.usagePeriodStartIso, juce::String("2026-08-01"));

    EXPECT_EQ(capturedMethod, juce::String("GET"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

// Older/pre-P4-4 server: no `usage` key at all. The fetch must still succeed — the client
// degrades to showing 0 used rather than failing the whole entitlement fetch.
TEST(AuthClientTest, FetchEntitlementMissingUsageDefaultsToZero) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, R"({"plan":"free","status":"active","period_end":null,)"
                               R"("cancel_at_period_end":false,"limits":{"monthly_requests":1000}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchEntitlement("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.plan, juce::String("free"));
    EXPECT_EQ(result.requestsUsed, 0);
    EXPECT_TRUE(result.usagePeriodStartIso.isEmpty());
}

TEST(AuthClientTest, FetchEntitlementUnauthorized) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(401, R"({"error":{"code":"UNAUTHENTICATED"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchEntitlement("bad-token", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, FetchEntitlementTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchEntitlement("access-token-123", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

// ============================================================================
// listConversations / getConversation / deleteConversation / deleteAllConversations (P6-8)
// ============================================================================

TEST(AuthClientTest, ListConversationsSuccessParsesConversationsAndDeletionScheduledAt) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/conversations");
        return makeStatus(200, R"({"data":{"conversations":[)"
                               R"({"id":"conv-1","title":"Bass patch","createdAt":"2026-08-01T00:00:00.000Z",)"
                               R"("updatedAt":"2026-08-02T00:00:00.000Z"},)"
                               R"({"id":"conv-2","title":null,"createdAt":"2026-08-03T00:00:00.000Z",)"
                               R"("updatedAt":"2026-08-03T00:00:00.000Z"})"
                               R"(],"deletionScheduledAt":"2026-09-01T00:00:00.000Z"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.listConversations("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.conversations.size(), 2u);
    EXPECT_EQ(result.conversations[0].id, juce::String("conv-1"));
    EXPECT_EQ(result.conversations[0].title, juce::String("Bass patch"));
    EXPECT_EQ(result.conversations[0].createdAt, juce::String("2026-08-01T00:00:00.000Z"));
    EXPECT_EQ(result.conversations[0].updatedAt, juce::String("2026-08-02T00:00:00.000Z"));
    EXPECT_EQ(result.conversations[1].id, juce::String("conv-2"));
    EXPECT_TRUE(result.conversations[1].title.isEmpty()); // null -> ""
    EXPECT_EQ(result.deletionScheduledAt, juce::String("2026-09-01T00:00:00.000Z"));

    EXPECT_EQ(capturedMethod, juce::String("GET"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

TEST(AuthClientTest, ListConversationsNullDeletionScheduledAtBecomesEmptyString) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, R"({"data":{"conversations":[],"deletionScheduledAt":null}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.listConversations("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.conversations.empty());
    EXPECT_TRUE(result.deletionScheduledAt.isEmpty());
}

TEST(AuthClientTest, ListConversationsUnauthorized) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(401, R"({"error":{"code":"UNAUTHENTICATED"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.listConversations("bad-token", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, ListConversationsMalformedJson) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, "not json");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.listConversations("access-token-123", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, ListConversationsTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.listConversations("access-token-123", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, GetConversationSuccessParsesSummaryAndMessages) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/conversations/conv-1");
        return makeStatus(200, R"({"data":{"conversation":{"id":"conv-1","title":"Bass patch",)"
                               R"("createdAt":"2026-08-01T00:00:00.000Z","updatedAt":"2026-08-02T00:00:00.000Z",)"
                               R"("messages":[{"role":"user","content":"make a bass patch",)"
                               R"("createdAt":"2026-08-01T00:00:01.000Z"},)"
                               R"({"role":"assistant","content":"{}","createdAt":"2026-08-01T00:00:02.000Z"}]}}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.getConversation("access-token-123", "conv-1", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.id, juce::String("conv-1"));
    EXPECT_EQ(result.title, juce::String("Bass patch"));
    EXPECT_EQ(result.createdAt, juce::String("2026-08-01T00:00:00.000Z"));
    EXPECT_EQ(result.updatedAt, juce::String("2026-08-02T00:00:00.000Z"));
    ASSERT_EQ(result.messages.size(), 2u);
    EXPECT_EQ(result.messages[0].role, juce::String("user"));
    EXPECT_EQ(result.messages[0].content, juce::String("make a bass patch"));
    EXPECT_EQ(result.messages[0].createdAt, juce::String("2026-08-01T00:00:01.000Z"));
    EXPECT_EQ(result.messages[1].role, juce::String("assistant"));

    EXPECT_EQ(capturedMethod, juce::String("GET"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

TEST(AuthClientTest, GetConversationNotFound) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(404, R"({"error":{"code":"NOT_FOUND"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.getConversation("access-token-123", "no-such-id", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, GetConversationMalformedJson) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, "not json");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.getConversation("access-token-123", "conv-1", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, GetConversationTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.getConversation("access-token-123", "conv-1", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, DeleteConversationSendsDeleteMethodAndBearerHeader) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/conversations/conv-1");
        return makeStatus(200, R"({"data":{"deleted":true}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteConversation("access-token-123", "conv-1", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.deleted);
    EXPECT_EQ(capturedMethod, juce::String("DELETE"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

TEST(AuthClientTest, DeleteConversationMalformedJson) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, "not json");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteConversation("access-token-123", "conv-1", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, DeleteConversationNotFound) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(404, R"({"error":{"code":"NOT_FOUND"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteConversation("access-token-123", "no-such-id", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, DeleteConversationTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteConversation("access-token-123", "conv-1", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, DeleteAllConversationsSendsDeleteMethodAndParsesDeletedCount) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/conversations");
        return makeStatus(200, R"({"data":{"deletedCount":7}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteAllConversations("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.deletedCount, 7);
    EXPECT_EQ(capturedMethod, juce::String("DELETE"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

TEST(AuthClientTest, DeleteAllConversationsMalformedJson) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, "not json");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteAllConversations("access-token-123", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, DeleteAllConversationsTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteAllConversations("access-token-123", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

// ============================================================================
// revoke / logout — fire-and-forget
// ============================================================================

TEST(AuthClientTest, RevokeSendsTokenAndReturnsTrueOn200) {
    juce::String capturedBody;
    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        EXPECT_EQ(method, juce::String("POST"));
        EXPECT_EQ(url, kHost + "/v1/auth/revoke");
        capturedBody = body;
        return makeStatus(200, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    EXPECT_TRUE(client.revoke("some-token", kNeverCancelled));
    EXPECT_EQ(parseForm(capturedBody)["token"], juce::String("some-token"));
}

TEST(AuthClientTest, LogoutSendsBearerHeaderAndReturnsTrueOn200) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        EXPECT_EQ(method, juce::String("POST"));
        EXPECT_EQ(url, kHost + "/v1/auth/logout");
        capturedHeaders = headers;
        return makeStatus(200, R"({"revoked":1})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    EXPECT_TRUE(client.logout("access-token-123", kNeverCancelled));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}
