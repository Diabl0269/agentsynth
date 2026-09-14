// Device-code auth flow: requestDeviceCode, pollDeviceToken, and refreshToken.
#include "AuthClientTestHelpers.h"

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
