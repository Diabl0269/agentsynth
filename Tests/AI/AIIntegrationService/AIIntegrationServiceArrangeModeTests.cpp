// Arrange mode: sendArrangeMessage -> the hosted timeline.generate capability (and its local-
// provider fallback through sendPrompt with an envelope-only schema).
#include "AIIntegrationServiceTestFixture.h"

namespace synth {

namespace {
// Records what sendCapabilityRequest() was handed, so the routing and the request body are both
// observable. Also counts sendPrompt() calls: an arrange request must never fall through to the
// conversation path.
class CapabilityCapturingProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema, std::function<void(const juce::String&)> = {}) override {
        ++sendPromptCalls;
        lastConversation = conversation;
        lastPromptSchema = responseSchema;
        AIResponse response;
        response.success = true;
        response.content = mockResponse;
        response.conversationId = mockConversationId;
        if (callback)
            callback(response);
        return {};
    }

    RequestId sendCapabilityRequest(const juce::String& capability, const juce::var& body,
                                    CompletionCallback callback) override {
        ++capabilityCalls;
        lastCapability = capability;
        lastBody = body;
        AIResponse response;
        response.success = true;
        response.content = mockResponse;
        response.conversationId = mockConversationId;
        if (callback)
            callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    juce::String getProviderName() const override { return "CapabilityCapturingProvider"; }
    void setConversationId(const juce::String& id) override { lastConversationId = id; }
    bool isHosted() const override { return hosted; }

    bool hosted = true; // flip to false to stand in for a local (Ollama-shaped) provider
    int sendPromptCalls = 0;
    int capabilityCalls = 0;
    juce::String lastCapability;
    juce::var lastBody;
    std::vector<Message> lastConversation;
    juce::var lastPromptSchema;
    juce::String mockResponse = R"({"timelineOps":[]})";
    juce::String mockConversationId;
    juce::String lastConversationId;
    juce::String model;
    int requestTimeoutMs = 240000;
};

// Hosted, but WITHOUT a sendCapabilityRequest override — stands in for a hosted provider the
// AIProvider base-class default must protect (a typed failure, never a crash or a silent drop).
class HostedNoCapabilityProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    juce::String getProviderName() const override { return "HostedNoCapabilityProvider"; }
    bool isHosted() const override { return true; }

    juce::String model;
    int requestTimeoutMs = 240000;
};
} // namespace

TEST_F(AIIntegrationServiceTest, ArrangeRequestRoutesToTimelineGenerateWithStructuredBody) {
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "arrange-uuid-1");

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "Melody");
    doc.addTrack(TrackKind::Automation, "Sweep");
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    auto providerPtr = std::make_unique<CapabilityCapturingProvider>();
    auto* provider = providerPtr.get();
    service->setProvider(std::move(providerPtr));

    bool called = false;
    service->sendArrangeMessage("build a 16 bar arrangement", [&](const AIProvider::AIResponse&) { called = true; });

    EXPECT_TRUE(called);
    EXPECT_EQ(provider->capabilityCalls, 1);
    EXPECT_EQ(provider->sendPromptCalls, 0) << "arrange mode never falls through to the conversation path";
    EXPECT_EQ(provider->lastCapability, juce::String("timeline.generate"));

    const juce::var& body = provider->lastBody;
    ASSERT_TRUE(body.isObject());

    // userPrompt is the RAW text — timeline.generate composes its own context sections
    // server-side, so the patch path's pre-wrapping must never leak in here.
    EXPECT_EQ(body["userPrompt"].toString(), juce::String("build a 16 bar arrangement"));
    EXPECT_FALSE(body["userPrompt"].toString().contains("Current patch state"));
    EXPECT_FALSE(body["userPrompt"].toString().contains("User request:"));

    EXPECT_TRUE(body["arrangementContext"].isString());
    EXPECT_TRUE(body["arrangementContext"].toString().isNotEmpty()) << "a non-empty doc summarises to something";

    ASSERT_TRUE(body["paramTargets"].isArray());
    ASSERT_GT(body["paramTargets"].getArray()->size(), 0) << "the uuid-bearing Oscillator's float params are offered";
    const juce::var target = (*body["paramTargets"].getArray())[0];
    EXPECT_EQ(target["nodeUuid"].toString(), juce::String("arrange-uuid-1"));
    EXPECT_TRUE(target["nodeName"].toString().isNotEmpty());
    EXPECT_TRUE(target["paramId"].toString().isNotEmpty());
    // Real numeric range + default, in the parameter's own units — presence and type, not values
    // (those belong to the module's own tests).
    EXPECT_TRUE(target["min"].isDouble() || target["min"].isInt());
    EXPECT_TRUE(target["max"].isDouble() || target["max"].isInt());
    EXPECT_TRUE(target["default"].isDouble() || target["default"].isInt());

    ASSERT_TRUE(body["availableTracks"].isArray());
    ASSERT_EQ(body["availableTracks"].getArray()->size(), 2);
    const juce::var track0 = (*body["availableTracks"].getArray())[0];
    const juce::var track1 = (*body["availableTracks"].getArray())[1];
    EXPECT_EQ(track0["name"].toString(), juce::String("Melody"));
    EXPECT_EQ(track0["kind"].toString(), juce::String("midi"));
    EXPECT_EQ(static_cast<int>(track0["index"]), 0);
    EXPECT_EQ(track1["name"].toString(), juce::String("Sweep"));
    EXPECT_EQ(track1["kind"].toString(), juce::String("automation"));
    EXPECT_EQ(static_cast<int>(track1["index"]), 1);

    // productName is the PROVIDER's field (RemoteProvider adds it) — the service must not
    // duplicate it into the caller-authored half.
    EXPECT_FALSE(body.hasProperty("productName"));
}

TEST_F(AIIntegrationServiceTest, ArrangeRequestParamTargetsAreCappedAtServerMax) {
    // Enough uuid-bearing nodes that the flat float-param count exceeds the cap.
    int paramsPerNode = 0;
    {
        auto probe = graph->addNode(std::make_unique<OscillatorModule>());
        ASSERT_NE(probe, nullptr);
        probe->properties.set("uuid", "probe-uuid");
        for (auto* p : probe->getProcessor()->getParameters())
            if (dynamic_cast<juce::AudioParameterFloat*>(p) != nullptr)
                ++paramsPerNode;
    }
    ASSERT_GT(paramsPerNode, 0);

    const int nodesNeeded = AIIntegrationService::kMaxRemoteParamTargets / paramsPerNode + 1;
    for (int i = 0; i < nodesNeeded; ++i) {
        auto node = graph->addNode(std::make_unique<OscillatorModule>());
        ASSERT_NE(node, nullptr);
        node->properties.set("uuid", "bulk-uuid-" + juce::String(i));
    }

    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    const juce::var body = service->buildArrangeRequestBody("automate everything");
    ASSERT_TRUE(body["paramTargets"].isArray());
    EXPECT_EQ(body["paramTargets"].getArray()->size(), AIIntegrationService::kMaxRemoteParamTargets)
        << "a longer list would be rejected by the server's input schema before any model saw it";
}

TEST_F(AIIntegrationServiceTest, ArrangeRequestOnEmptyTimelineSaysSoExplicitly) {
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    const juce::var body = service->buildArrangeRequestBody("start an arrangement");

    // The schema requires both keys but allows them empty — "a caller with nothing to say should
    // say so explicitly rather than have the field quietly go missing".
    ASSERT_TRUE(body.hasProperty("arrangementContext"));
    EXPECT_EQ(body["arrangementContext"].toString(), juce::String());
    ASSERT_TRUE(body["availableTracks"].isArray());
    EXPECT_EQ(body["availableTracks"].getArray()->size(), 0);
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageSharesHistoryAndConversationIdContractWithSendMessage) {
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    auto providerPtr = std::make_unique<CapabilityCapturingProvider>();
    auto* provider = providerPtr.get();
    provider->mockResponse = R"({"timelineOps":[{"op":"addTrack","kind":"midi","name":"Bass"}]})";
    provider->mockConversationId = "conv-arrange-1";
    service->setProvider(std::move(providerPtr));

    service->sendArrangeMessage("add a bass line", [](const AIProvider::AIResponse&) {});

    // Same bookkeeping as sendMessage(): user turn (raw text), assistant turn (envelope JSON),
    // and the Pro-plan conversation id captured and re-pushed to the provider.
    const auto& history = service->getHistory();
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[1].role, "user");
    EXPECT_EQ(history[1].content, "add a bass line");
    EXPECT_EQ(history[2].role, "assistant");
    EXPECT_EQ(history[2].content, provider->mockResponse);
    EXPECT_EQ(provider->lastConversationId, juce::String("conv-arrange-1"));
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageWithoutProviderFailsWithTypedError) {
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    AIProvider::AIResponse captured;
    bool called = false;
    service->sendArrangeMessage("anything", [&](const AIProvider::AIResponse& r) {
        captured = r;
        called = true;
    });

    ASSERT_TRUE(called);
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.error.kind, AIProvider::AIErrorKind::Schema);
    EXPECT_EQ(captured.error.message, juce::String("Error: No AI provider selected."));
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageOnHostedProviderWithoutCapabilitySupportFailsTyped) {
    // HostedNoCapabilityProvider does not override sendCapabilityRequest — the AIProvider
    // base-class default must deliver a typed failure, never crash or silently drop the callback.
    // (A NON-hosted provider never reaches that default: it routes to the sendPrompt transport,
    // pinned by the local-transport test below.)
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);
    service->setProvider(std::make_unique<HostedNoCapabilityProvider>());

    AIProvider::AIResponse captured;
    bool called = false;
    service->sendArrangeMessage("anything", [&](const AIProvider::AIResponse& r) {
        captured = r;
        called = true;
    });

    ASSERT_TRUE(called);
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.error.kind, AIProvider::AIErrorKind::Schema);
    EXPECT_TRUE(captured.error.message.contains("does not support capability requests"));
    EXPECT_TRUE(captured.error.message.contains("timeline.generate"));
}

TEST_F(AIIntegrationServiceTest, ArrangeMessageOnLocalProviderComposesPromptWithEnvelopeOnlySchema) {
    // The parity rule's other half: a LOCAL provider serves the same arrange intent through
    // sendPrompt — the structured fields composed into the message (mirroring the server's own
    // section layout) plus an envelope-ONLY response schema, never the capability endpoint.
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "local-arrange-uuid");

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "Melody");
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    auto providerPtr = std::make_unique<CapabilityCapturingProvider>();
    auto* provider = providerPtr.get();
    provider->hosted = false;
    service->setProvider(std::move(providerPtr));

    bool called = false;
    service->sendArrangeMessage("automate the cutoff over 8 bars",
                                [&](const AIProvider::AIResponse&) { called = true; });

    EXPECT_TRUE(called);
    EXPECT_EQ(provider->capabilityCalls, 0) << "a local provider must never be asked for a capability endpoint";
    EXPECT_EQ(provider->sendPromptCalls, 1);

    // The outgoing message carries the SAME fields the hosted body would, section by section,
    // ending with the raw prompt + the envelope steering line.
    ASSERT_FALSE(provider->lastConversation.empty());
    const juce::String content = provider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("Arrangement context:"));
    EXPECT_TRUE(content.contains("Project tracks:"));
    EXPECT_TRUE(content.contains("\"Melody\""));
    EXPECT_TRUE(content.contains("Automation targets:"));
    EXPECT_TRUE(content.contains("\"local-arrange-uuid\""));
    EXPECT_TRUE(content.contains("automate the cutoff over 8 bars"));
    EXPECT_TRUE(content.contains("timelineOps"));

    // Envelope-only contract: timelineOps present AND required; no patch grammar in sight.
    auto* schemaObj = provider->lastPromptSchema.getDynamicObject();
    ASSERT_NE(schemaObj, nullptr);
    auto* props = schemaObj->getProperty("properties").getDynamicObject();
    ASSERT_NE(props, nullptr);
    EXPECT_TRUE(props->hasProperty("timelineOps"));
    EXPECT_FALSE(props->hasProperty("nodes"));
    ASSERT_TRUE(schemaObj->getProperty("required").isArray());
    EXPECT_TRUE(schemaObj->getProperty("required").getArray()->contains(juce::var("timelineOps")));

    // History keeps the RAW user text — the composed arrange context exists only on the wire,
    // exactly like sendMessage()'s patch-context splice.
    const auto& history = service->getHistory();
    ASSERT_GE(history.size(), 2u);
    EXPECT_EQ(history[1].content, "automate the cutoff over 8 bars");
}

} // namespace synth
