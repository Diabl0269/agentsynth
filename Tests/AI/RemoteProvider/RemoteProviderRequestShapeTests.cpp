// Request shape: the outgoing patch.generate body/headers, and the fail-fast checks that never
// touch the network.
#include "RemoteProviderTestFixture.h"

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
    EXPECT_EQ((int)obj->getProperty("promptVersion"), 3)
        << "the server prompt that keeps Sequencers on Follow Song Key";
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

    synth::RemoteProvider provider{kOfficialHost, performer};
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
    synth::RemoteProvider provider{kOfficialHost, performer, "device-uuid-1234"};
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

namespace {
struct CapturedSend {
    int calls = 0;
    juce::StringPairArray headers;
    synth::AIProvider::AIResponse response;
};

CapturedSend sendOnce(const juce::String& host, const juce::String& token, const juce::String& deviceId) {
    CapturedSend captured;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        ++captured.calls;
        captured.headers = headers;
        return makeSuccess(R"({"data":{}})");
    };
    synth::RemoteProvider provider{host, performer, deviceId};
    provider.setTestMode(true);
    provider.setAuthToken(token);
    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    captured.response = callback.getResult();
    provider.stopThread(5000);
    return captured;
}
} // namespace

// Regression test for FRO2121: a custom server address received the account token and the device id.
TEST_F(RemoteProviderTest, CustomHttpsServerGetsNoTokenAndNoDeviceId) {
    const auto sent = sendOnce(kMockHost, "secret-token-123", "device-1");
    ASSERT_EQ(sent.calls, 1);
    EXPECT_FALSE(sent.headers.containsKey("Authorization"));
    EXPECT_FALSE(sent.headers.containsKey("X-Device-Id"));
}

TEST_F(RemoteProviderTest, LoopbackServerGetsNoTokenAndNoDeviceId) {
    const auto sent = sendOnce("http://localhost:8787", "secret-token-123", "device-1");
    ASSERT_EQ(sent.calls, 1);
    EXPECT_FALSE(sent.headers.containsKey("Authorization"));
    EXPECT_FALSE(sent.headers.containsKey("X-Device-Id"));
}

TEST_F(RemoteProviderTest, OfficialServerGetsTokenAndDeviceId) {
    const auto sent = sendOnce(kOfficialHost, "secret-token-123", "device-1");
    ASSERT_EQ(sent.calls, 1);
    EXPECT_EQ(sent.headers.getValue("Authorization", ""), juce::String("Bearer secret-token-123"));
    EXPECT_EQ(sent.headers.getValue("X-Device-Id", ""), juce::String("device-1"));
}

TEST_F(RemoteProviderTest, PlainHttpNonLoopbackServerIsRefusedWithoutANetworkCall) {
    const auto sent = sendOnce("http://evil.example:8787", "secret-token-123", "device-1");
    EXPECT_EQ(sent.calls, 0);
    EXPECT_FALSE(sent.response.success);
    EXPECT_EQ(sent.response.error.kind, synth::AIProvider::AIErrorKind::Network);
}
