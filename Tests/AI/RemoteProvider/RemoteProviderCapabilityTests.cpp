// Capability requests (sendCapabilityRequest — timeline.generate / arrange mode)
#include "RemoteProviderTestFixture.h"

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
