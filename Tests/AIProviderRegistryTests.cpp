#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIProviderRegistry.h"
#include <gtest/gtest.h>

namespace {

class StubProvider : public synth::AIProvider {
public:
    explicit StubProvider(juce::String name)
        : name(std::move(name)) {}

    juce::String getProviderName() const override { return name; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }

private:
    juce::String name;
};

} // namespace

TEST(AIProviderRegistryTest, RegisteredProviderIsConstructibleById) {
    synth::AIProviderRegistry registry;
    registry.registerProvider(
        {"stub", "Stub Provider", false, false, [](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
             return std::make_unique<StubProvider>("Stub Provider");
         }});

    auto provider = registry.create("stub", {});
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->getProviderName(), "Stub Provider");
}

TEST(AIProviderRegistryTest, UnknownIdFallsBackToFirstRegistered) {
    synth::AIProviderRegistry registry;
    registry.registerProvider({"first", "First Provider", false, false,
                               [](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
                                   return std::make_unique<StubProvider>("First Provider");
                               }});
    registry.registerProvider({"second", "Second Provider", false, false,
                               [](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
                                   return std::make_unique<StubProvider>("Second Provider");
                               }});

    auto provider = registry.create("does-not-exist", {});
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->getProviderName(), "First Provider");
}

TEST(AIProviderRegistryTest, ListAllReturnsEveryRegisteredDescriptor) {
    synth::AIProviderRegistry registry;
    registry.registerProvider({"first", "First Provider", false, false, nullptr});
    registry.registerProvider({"second", "Second Provider", true, true, nullptr});

    const auto& all = registry.listAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].id, "first");
    EXPECT_EQ(all[1].id, "second");
    EXPECT_TRUE(all[1].needsHost);
    EXPECT_TRUE(all[1].needsAuth);
}

TEST(AIProviderRegistryTest, PersistedIdIsIndependentOfDisplayName) {
    auto makeRegistry = [](const juce::String& displayName) {
        synth::AIProviderRegistry registry;
        registry.registerProvider({"ollama", displayName, true, false,
                                   [](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
                                       return std::make_unique<StubProvider>("Ollama");
                                   }});
        return registry;
    };

    // Simulates renaming the UI-facing label between versions — the persisted key is "ollama"
    // regardless of what displayName the descriptor carries.
    auto oldRegistry = makeRegistry("Ollama");
    auto newRegistry = makeRegistry("Ollama (local)");

    ASSERT_NE(oldRegistry.find("ollama"), nullptr);
    ASSERT_NE(newRegistry.find("ollama"), nullptr);
    EXPECT_EQ(oldRegistry.find("ollama")->id, newRegistry.find("ollama")->id);

    auto provider = newRegistry.create("ollama", {});
    ASSERT_NE(provider, nullptr);
}

TEST(AIProviderRegistryTest, CreateDefaultRegistersOllamaFirstAndRemoteHidden) {
    auto registry = synth::AIProviderRegistry::createDefault();

    const auto& all = registry.listAll();
    ASSERT_EQ(all.size(), 2u);

    // "ollama" must stay first: AIProviderRegistry::create() falls back to descriptors.front()
    // for an unknown/empty id, and that fallback must be unaffected by adding "remote".
    EXPECT_EQ(all[0].id, "ollama");
    EXPECT_FALSE(all[0].hidden);

    const auto* remote = registry.find("remote");
    ASSERT_NE(remote, nullptr);
    EXPECT_TRUE(remote->hidden);
}
