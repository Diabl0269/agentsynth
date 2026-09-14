// listConversations / getConversation / deleteConversation / deleteAllConversations (P6-8)
#include "AuthClientTestHelpers.h"

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
