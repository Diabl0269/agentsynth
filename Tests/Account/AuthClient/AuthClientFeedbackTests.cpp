// submitMessageFeedback (P6-9), submitGeneralFeedback (P6-16), and the fire-and-forget
// revoke/logout pair.
#include "AuthClientTestHelpers.h"

TEST(AuthClientTest, SubmitMessageFeedbackSendsPostWithCorrectPathHeadersAndBody) {
    juce::String capturedMethod;
    juce::String capturedUrl;
    juce::StringPairArray capturedHeaders;
    juce::String capturedBody;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedUrl = url;
        capturedHeaders = headers;
        capturedBody = body;
        return makeStatus(200, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result =
        client.submitMessageFeedback("access-token-123", "conv-1", "msg-1", "up", "great patch", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(capturedMethod, juce::String("POST"));
    EXPECT_EQ(capturedUrl, kHost + "/v1/conversations/conv-1/messages/msg-1/feedback");
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));

    const auto parsedBody = juce::JSON::parse(capturedBody);
    auto* bodyObj = parsedBody.getDynamicObject();
    ASSERT_NE(bodyObj, nullptr);
    EXPECT_EQ(bodyObj->getProperty("rating").toString(), juce::String("up"));
    EXPECT_EQ(bodyObj->getProperty("comment").toString(), juce::String("great patch"));
}

TEST(AuthClientTest, SubmitMessageFeedbackOmitsCommentFieldWhenEmpty) {
    juce::String capturedBody;
    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(200, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result =
        client.submitMessageFeedback("access-token-123", "conv-1", "msg-1", "down", "", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    const auto parsedBody = juce::JSON::parse(capturedBody);
    auto* bodyObj = parsedBody.getDynamicObject();
    ASSERT_NE(bodyObj, nullptr);
    EXPECT_EQ(bodyObj->getProperty("rating").toString(), juce::String("down"));
    EXPECT_FALSE(bodyObj->hasProperty("comment"));
}

TEST(AuthClientTest, SubmitMessageFeedbackNotFound) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(404, R"({"error":{"code":"NOT_FOUND"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result =
        client.submitMessageFeedback("access-token-123", "no-such-conv", "no-such-msg", "up", "", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SubmitMessageFeedbackForbiddenWhenNotPro) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(403, R"({"error":{"code":"FORBIDDEN"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.submitMessageFeedback("access-token-123", "conv-1", "msg-1", "up", "", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SubmitMessageFeedbackInvalidRating) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(400, R"({"error":{"code":"BAD_REQUEST"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result =
        client.submitMessageFeedback("access-token-123", "conv-1", "msg-1", "sideways", "", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SubmitMessageFeedbackTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.submitMessageFeedback("access-token-123", "conv-1", "msg-1", "up", "", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SubmitGeneralFeedbackSendsPostWithCorrectPathHeadersAndBody) {
    juce::String capturedMethod;
    juce::String capturedUrl;
    juce::StringPairArray capturedHeaders;
    juce::String capturedBody;

    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedUrl = url;
        capturedHeaders = headers;
        capturedBody = body;
        return makeStatus(200, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.submitGeneralFeedback("access-token-123", "bug", "the thing broke", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(capturedMethod, juce::String("POST"));
    EXPECT_EQ(capturedUrl, kHost + "/v1/feedback");
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer access-token-123"));

    const auto parsedBody = juce::JSON::parse(capturedBody);
    auto* bodyObj = parsedBody.getDynamicObject();
    ASSERT_NE(bodyObj, nullptr);
    EXPECT_EQ(bodyObj->getProperty("category").toString(), juce::String("bug"));
    EXPECT_EQ(bodyObj->getProperty("text").toString(), juce::String("the thing broke"));
}

TEST(AuthClientTest, SubmitGeneralFeedbackNon200Status) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        return makeStatus(400, R"({"error":{"code":"BAD_REQUEST"}})");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.submitGeneralFeedback("access-token-123", "other", "hello", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SubmitGeneralFeedbackTransportFailure) {
    auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                        int,
                        const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return makeTransportFailure(); };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.submitGeneralFeedback("access-token-123", "feature", "add this", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, SubmitGeneralFeedbackWithEmptyAccessTokenSendsDeviceIdInsteadOfAuthorization) {
    juce::StringPairArray capturedHeaders;

    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedHeaders = headers;
        return makeStatus(200, "");
    };

    // Non-empty device id, empty access token: an anonymous (signed-out) submission (P6-17).
    synth::AuthClient client{kHost, kClientId, performer, "device-abc-123"};
    const auto result = client.submitGeneralFeedback("", "bug", "anonymous report", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(capturedHeaders.getValue("X-Device-Id", ""), juce::String("device-abc-123"));
    EXPECT_TRUE(capturedHeaders.getValue("Authorization", "").isEmpty());
}

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
