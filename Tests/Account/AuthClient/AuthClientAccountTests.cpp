// Account/entitlement/preferences: fetchMe, fetchEntitlement, and the prompt-learning preference
// get/set pair (P6-7).
#include "AuthClientTestHelpers.h"

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

TEST(AuthClientTest, FetchPromptLearningPreferenceParsesOptedInTrue) {
    juce::String capturedMethod;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/prompt-learning");
        return makeStatus(200, R"({"opted_in":true,"opted_in_at":"2026-08-19T00:00:00.000Z"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchPromptLearningPreference("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.optedIn);
    EXPECT_EQ(result.optedInAt, juce::String("2026-08-19T00:00:00.000Z"));
    EXPECT_EQ(capturedMethod, juce::String("GET"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));
}

// Default/never-opted-in shape: opted_in false, opted_in_at null — this is the load-bearing
// "off by default" response a brand-new account gets.
TEST(AuthClientTest, FetchPromptLearningPreferenceDefaultIsOptedOutWithNullTimestamp) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, R"({"opted_in":false,"opted_in_at":null})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchPromptLearningPreference("access-token-123", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(result.optedIn);
    EXPECT_TRUE(result.optedInAt.isEmpty());
}

TEST(AuthClientTest, FetchPromptLearningPreferenceUnauthorized) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(401, R"({"error":"invalid_token"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchPromptLearningPreference("bad-token", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, FetchPromptLearningPreferenceTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.fetchPromptLearningPreference("access-token-123", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SetPromptLearningPreferenceSendsPutWithJsonBodyAndParsesResponse) {
    juce::String capturedMethod;
    juce::String capturedBody;
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedBody = body;
        capturedHeaders = headers;
        EXPECT_EQ(url, kHost + "/v1/prompt-learning");
        return makeStatus(200, R"({"opted_in":true,"opted_in_at":"2026-08-19T00:00:00.000Z"})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.setPromptLearningPreference("access-token-123", true, kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.optedIn);
    EXPECT_EQ(result.optedInAt, juce::String("2026-08-19T00:00:00.000Z"));
    EXPECT_EQ(capturedMethod, juce::String("PUT"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));

    const auto parsedBody = juce::JSON::parse(capturedBody);
    auto* bodyObj = parsedBody.getDynamicObject();
    ASSERT_NE(bodyObj, nullptr);
    EXPECT_TRUE(static_cast<bool>(bodyObj->getProperty("opted_in")));
}

// Revoking (true -> false) purges server-side samples, but that's entirely server-side; the
// client only needs to see the flipped opted_in/opted_in_at reflected back correctly.
TEST(AuthClientTest, SetPromptLearningPreferenceFalseParsesRevokedResponse) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(200, R"({"opted_in":false,"opted_in_at":null})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.setPromptLearningPreference("access-token-123", false, kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(result.optedIn);
    EXPECT_TRUE(result.optedInAt.isEmpty());
}

TEST(AuthClientTest, SetPromptLearningPreferenceTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.setPromptLearningPreference("access-token-123", true, kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}
