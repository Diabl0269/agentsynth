// Timeline tools toggle: the local model's automation/timeline authoring surface.
#include "AIIntegrationServiceTestFixture.h"

namespace synth {

namespace {
// Records the schema each sendPrompt() call carried, so the schema-selection seam is observable.
class SchemaCapturingProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var& responseSchema,
                         std::function<void(const juce::String&)> = {}) override {
        lastSchema = responseSchema;
        AIResponse response;
        response.success = true;
        response.content = "ok";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"m"}, true);
    }
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    juce::String getProviderName() const override { return "SchemaCapturingProvider"; }

    juce::var lastSchema;
    juce::String model;
    int requestTimeoutMs = 240000;
};

bool schemaOffersTimelineOps(const juce::var& schema) {
    auto* obj = schema.getDynamicObject();
    if (obj == nullptr)
        return false;
    auto* props = obj->getProperty("properties").getDynamicObject();
    return props != nullptr && props->hasProperty("timelineOps");
}
} // namespace

TEST_F(AIIntegrationServiceTest, TimelineToolsToggleGatesThePromptAndSchema) {
    // Baseline: no context, tools off — the system prompt is the pre-timeline one.
    const juce::String baseline = service->getHistory().front().content;
    EXPECT_FALSE(baseline.contains("timelineOps"));

    // Context alone doesn't enable anything (the preference is the switch)…
    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    EXPECT_EQ(service->getHistory().front().content, baseline);

    // …and the switch alone needs the context (both are required).
    service->setTimelineContext(nullptr, nullptr);
    service->setTimelineToolsEnabled(true);
    EXPECT_FALSE(service->getHistory().front().content.contains("timelineOps"));

    // Both on: the prompt teaches the grammar, swapped IN PLACE (history size unchanged — a
    // mid-conversation toggle must not clear the chat).
    service->setTimelineContext(&doc, &transport);
    ASSERT_EQ(service->getHistory().size(), 1u);
    const juce::String enabled = service->getHistory().front().content;
    EXPECT_TRUE(enabled.contains("TIMELINE & AUTOMATION OPERATIONS"));
    EXPECT_TRUE(enabled.contains("writeLane"));
    EXPECT_TRUE(enabled.startsWith(baseline)) << "the timeline section is appended, never rewrites the patch prompt";

    // Off again: byte-identical to the baseline.
    service->setTimelineToolsEnabled(false);
    EXPECT_EQ(service->getHistory().front().content, baseline);
}

TEST_F(AIIntegrationServiceTest, TimelineToolsToggleSelectsTheExtendedSchema) {
    auto providerPtr = std::make_unique<SchemaCapturingProvider>();
    auto* provider = providerPtr.get();
    service->setProvider(std::move(providerPtr));

    service->sendMessage("make a bass", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);
    EXPECT_FALSE(schemaOffersTimelineOps(provider->lastSchema)) << "tools off: the plain patch schema";

    TimelineDoc doc;
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);
    service->sendMessage("automate the cutoff", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);
    EXPECT_TRUE(schemaOffersTimelineOps(provider->lastSchema));
}

TEST_F(AIIntegrationServiceTest, AutomationTargetsSectionListsUuidBearingFloatParams) {
    // A node with a uuid and float params is addressable; one without a uuid is not.
    auto node = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    node->properties.set("uuid", "test-uuid-1");
    // Deliberately uuid-less: graphToJSON (run first, for the patch-context section) mints one, so
    // by the time the targets section is built EVERY node is addressable — pinned below.
    auto anon = graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(anon, nullptr);

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "T"); // non-empty so the arrangement section exists too
    TransportService transport;
    service->setTimelineContext(&doc, &transport);
    service->setTimelineToolsEnabled(true);

    MockAIProvider* provider = nullptr;
    {
        auto p = std::make_unique<MockAIProvider>();
        provider = p.get();
        service->setProvider(std::move(p));
    }
    service->sendMessage("automate something", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);

    ASSERT_FALSE(provider->lastConversation.empty());
    const juce::String content = provider->lastConversation.back().content;
    ASSERT_TRUE(content.contains("## Automation targets"));
    // The uuid also (correctly) appears in the patch-state JSON above, so scope the assertions to
    // the targets section itself: two node lines — the explicit uuid AND the initially-uuid-less
    // twin, whose uuid graphToJSON minted while building the patch-context section.
    const juce::String targets = content.fromFirstOccurrenceOf("## Automation targets", false, false)
                                     .upToFirstOccurrenceOf("User request:", false, false);
    EXPECT_TRUE(targets.contains("\"test-uuid-1\"")) << "the addressable node's uuid is listed";
    EXPECT_TRUE(targets.contains("[")) << "params carry their raw ranges";
    EXPECT_EQ(juce::StringArray::fromLines(targets.trim()).size() - 1, 2)
        << "both oscillators are listed — graphToJSON ensures every live node carries a uuid";

    // Toggle off: the targets section disappears from the outgoing request entirely.
    service->setTimelineToolsEnabled(false);
    service->sendMessage("automate something", [](const AIProvider::AIResponse&) {}, /*useStructuredOutput=*/true);
    EXPECT_FALSE(provider->lastConversation.back().content.contains("## Automation targets"));
}

} // namespace synth
