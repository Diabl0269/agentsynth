// AuthClient::deleteAccount (DELETE /v1/account) and AuthClient::submitExitSurvey (POST /v1/exit-survey).
#include "AuthClientTestHelpers.h"

TEST(AuthClientTest, SubmitExitSurveySendsKindReasonsAndComment) {
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
        return makeStatus(201, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result =
        client.submitExitSurvey("tok", "cancel", {"too_expensive", "bugs"}, "Too many crashes", kNeverCancelled);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(capturedMethod, juce::String("POST"));
    EXPECT_EQ(capturedUrl, kHost + "/v1/exit-survey");
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer tok"));

    const auto bodyVar = juce::JSON::parse(capturedBody);
    auto* body = bodyVar.getDynamicObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->getProperty("kind").toString(), juce::String("cancel"));
    auto* reasons = body->getProperty("reasons").getArray();
    ASSERT_NE(reasons, nullptr);
    ASSERT_EQ(reasons->size(), 2);
    EXPECT_EQ((*reasons)[0].toString(), juce::String("too_expensive"));
    EXPECT_EQ((*reasons)[1].toString(), juce::String("bugs"));
    EXPECT_EQ(body->getProperty("comment").toString(), juce::String("Too many crashes"));
}

TEST(AuthClientTest, SubmitExitSurveyOmitsCommentWhenEmptyAndKeepsAnEmptyReasonList) {
    juce::String capturedBody;
    auto performer = [&](const juce::String&, const juce::String&, const juce::StringPairArray&,
                         const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedBody = body;
        return makeStatus(201, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    ASSERT_TRUE(client.submitExitSurvey("tok", "delete", {}, "", kNeverCancelled).ok);

    const auto bodyVar = juce::JSON::parse(capturedBody);
    auto* body = bodyVar.getDynamicObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->getProperty("kind").toString(), juce::String("delete"));
    EXPECT_FALSE(body->hasProperty("comment"));
    ASSERT_NE(body->getProperty("reasons").getArray(), nullptr);
    EXPECT_EQ(body->getProperty("reasons").getArray()->size(), 0);
}

TEST(AuthClientTest, SubmitExitSurveyReportsAServerRefusalAndATransportFailure) {
    synth::AuthClient refused{kHost, kClientId,
                              [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                 const juce::String&, int, const std::atomic<bool>&) { return makeStatus(500, ""); }};
    EXPECT_FALSE(refused.submitExitSurvey("tok", "cancel", {}, "", kNeverCancelled).ok);

    synth::AuthClient offline{kHost, kClientId,
                              [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                 const juce::String&, int,
                                 const std::atomic<bool>&) { return makeTransportFailure(); }};
    const auto result = offline.submitExitSurvey("tok", "cancel", {}, "", kNeverCancelled);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}

TEST(AuthClientTest, DeleteAccountSendsAuthenticatedDeleteAndTreats204AsSuccess) {
    juce::String capturedMethod;
    juce::String capturedUrl;
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                         const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        capturedMethod = method;
        capturedUrl = url;
        capturedHeaders = headers;
        return makeStatus(204, "");
    };

    synth::AuthClient client{kHost, kClientId, performer};
    const auto result = client.deleteAccount("tok", kNeverCancelled);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.httpStatus, 204);
    EXPECT_EQ(capturedMethod, juce::String("DELETE"));
    EXPECT_EQ(capturedUrl, kHost + "/v1/account");
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer tok"));
}

TEST(AuthClientTest, DeleteAccountParsesTheSubscriptionActiveRefusal) {
    synth::AuthClient client{
        kHost, kClientId,
        [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&, int,
           const std::atomic<bool>&) {
            return makeStatus(
                409, R"({"error":{"code":"SUBSCRIPTION_ACTIVE","message":"Cancel your subscription first."}})");
        }};

    const auto result = client.deleteAccount("tok", kNeverCancelled);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.isSubscriptionActive());
    EXPECT_EQ(result.errorMessage, juce::String("Cancel your subscription first."));
}

TEST(AuthClientTest, DeleteAccountKeepsAnotherServerErrorAndFlagsA401) {
    synth::AuthClient other{
        kHost, kClientId,
        [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&, int,
           const std::atomic<bool>&) { return makeStatus(500, R"({"error":{"code":"INTERNAL","message":"Boom"}})"); }};
    const auto failed = other.deleteAccount("tok", kNeverCancelled);
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.isSubscriptionActive());
    EXPECT_EQ(failed.errorMessage, juce::String("Boom"));

    synth::AuthClient expired{kHost, kClientId,
                              [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                 const juce::String&, int, const std::atomic<bool>&) { return makeStatus(401, ""); }};
    EXPECT_TRUE(expired.deleteAccount("tok", kNeverCancelled).isUnauthorised());
}

TEST(AuthClientTest, DeleteAccountReportsATransportFailureWithNoStatus) {
    synth::AuthClient client{kHost, kClientId,
                             [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                const juce::String&, int, const std::atomic<bool>&) { return makeTransportFailure(); }};
    const auto result = client.deleteAccount("tok", kNeverCancelled);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.httpStatus, 0);
    EXPECT_TRUE(result.transportError.isNotEmpty());
}
