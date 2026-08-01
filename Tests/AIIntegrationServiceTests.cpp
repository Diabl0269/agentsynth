#include "AI/AIIntegrationService.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace synth {

class MockAIProvider : public AIProvider {
public:
    void sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                    const juce::var& responseSchema) override {
        juce::ignoreUnused(conversation, responseSchema);
        if (shouldFail) {
            callback("Error", false);
        } else {
            callback(mockResponse, true);
        }
    }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (shouldFail)
            callback({}, false);
        else
            callback({"model1", "model2"}, true);
    }

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    juce::String getProviderName() const override { return "MockProvider"; }

    juce::String mockResponse = "{\"nodes\": [], \"connections\": []}";
    bool shouldFail = false;
    juce::String currentModel;
};

class AIIntegrationServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_unique<juce::AudioProcessorGraph>();
        service = std::make_unique<AIIntegrationService>(*graph);
    }

    std::unique_ptr<juce::AudioProcessorGraph> graph;
    std::unique_ptr<AIIntegrationService> service;
};

TEST_F(AIIntegrationServiceTest, InitialHistoryHasSystemPrompt) {
    EXPECT_FALSE(service->getHistory().empty());
    EXPECT_EQ(service->getHistory()[0].role, "system");
}

TEST_F(AIIntegrationServiceTest, SendMessageAddsToHistory) {
    auto provider = std::make_unique<MockAIProvider>();
    auto* rawProvider = provider.get();
    service->setProvider(std::move(provider));

    rawProvider->mockResponse = "AI Response";

    bool called = false;
    service->sendMessage("User msg", [&](const juce::String&, bool) { called = true; });

    EXPECT_TRUE(called);
    // History should have: system, user, assistant
    auto history = service->getHistory();
    EXPECT_EQ(history.size(), 3);
    EXPECT_EQ(history[1].role, "user");
    EXPECT_EQ(history[1].content, "User msg");
    EXPECT_EQ(history[2].role, "assistant");
    EXPECT_EQ(history[2].content, "AI Response");
}

TEST_F(AIIntegrationServiceTest, ClearHistoryPreservesSystemPrompt) {
    service->sendMessage("test", nullptr);
    service->clearHistory();

    EXPECT_EQ(service->getHistory().size(), 1);
    EXPECT_EQ(service->getHistory()[0].role, "system");
}

TEST_F(AIIntegrationServiceTest, ExtractJsonFromResponse) {
    // This tests a private method if we use friend or just test through applyPatch
    // Since it's private and we don't have friend access here easily,
    // we test it indirectly via applyPatch which is public.

    // Test extraction from backticks
    juce::String withBackticks = "Here is the patch: ```json\n{\"nodes\":[]}\n``` and some more text.";
    // applyPatch calls extractJsonFromResponse internally
    // We expect it to try and parse the JSON. If it fails, it returns false.
    // If it succeeds (empty nodes), it returns true.
    EXPECT_TRUE(service->applyPatch(withBackticks));
}

TEST_F(AIIntegrationServiceTest, ExtractJsonRaw) {
    juce::String raw = "{\"nodes\":[], \"connections\":[]}";
    EXPECT_TRUE(service->applyPatch(raw));
}

TEST_F(AIIntegrationServiceTest, GetPatchContext) {
    juce::String context = service->getPatchContext();
    EXPECT_TRUE(context.contains("nodes"));
    EXPECT_TRUE(context.contains("connections"));
}

TEST_F(AIIntegrationServiceTest, ModelManagement) {
    auto provider = std::make_unique<MockAIProvider>();
    service->setProvider(std::move(provider));

    service->setModel("test-model");
    EXPECT_EQ(service->getCurrentModel(), "test-model");
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_MergeMode_PreservesExisting) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a delta patch in merge mode
    juce::String deltaJson = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(deltaJson, true);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 2); // Original Oscillator + new Filter
}

TEST_F(AIIntegrationServiceTest, ApplyPatch_DefaultReplace_ClearsGraph) {
    // Add an existing node to the graph
    graph->addNode(std::make_unique<OscillatorModule>());
    ASSERT_EQ(graph->getNumNodes(), 1);

    // Apply a full patch without merge mode (default)
    juce::String fullJson = "{\"nodes\":[{\"id\":100,\"type\":\"Filter\",\"params\":{}}],\"connections\":[]}";
    bool success = service->applyPatch(fullJson);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph->getNumNodes(), 1); // Only the Filter from JSON, Oscillator cleared
}

} // namespace synth
