// HostedPluginModSlotTests.cpp
//
// Mod cables onto a hosted plugin's own parameters: a parameter claims a modulation slot (an input channel above
// the 16 audio ones), the audio thread writes base + CV into it every block, and the base is the resting value the
// card knob shows, the save keeps and anything else that turns the parameter moves.
//
// Runs against Tests/StubPluginInstance.h, like every hosted-plugin test: there is no real plugin to load in CI.

#include "../StubPluginInstance.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kCutoffChannel = HostedPluginModule::kModChannelBase;

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Stub Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0x5754424;
    description.deprecatedUid = 0x5754424;
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    return description;
}

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

std::vector<StubParamSpec> defaultParams() {
    return {{"cutoff", "Cutoff", 0.2f}, {"reso", "Resonance", 0.5f}, {"", "Legacy Drive", 0.4f}};
}

struct Fixture {
    StubBackend backend;
    std::unique_ptr<HostedPluginModule> module = std::make_unique<HostedPluginModule>();

    bool load(std::vector<StubParamSpec> params = defaultParams()) {
        backend.setFactory(
            [params] { return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 0x5754424, "VST3", params); });
        module->prepareToPlay(kSampleRate, kBlockSize);
        module->loadPlugin(stubDescription(), backend);
        return pumpUntil([&] { return module->hasInstance() && !module->isLoading(); });
    }

    // One block with `cv` on `channel` (every other input silent), the way the graph hands the node its CV.
    void render(int channel, float cv) {
        juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginChannels + HostedPluginModule::kMaxModSlots,
                                        kBlockSize);
        buffer.clear();
        if (channel >= 0)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), cv, kBlockSize);
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);
    }

    juce::AudioProcessorParameter& param(const juce::String& id) { return *module->findInstanceParameter(id); }
};

} // namespace

TEST(HostedPluginModSlotTest, BindingClaimsASlotThatIsAKnobOnlyModTarget) {
    Fixture f;
    ASSERT_TRUE(f.load());

    EXPECT_TRUE(f.module->getModulationTargets().empty());
    EXPECT_EQ(f.module->bindModSlot("cutoff"), kCutoffChannel);
    EXPECT_EQ(f.module->bindModSlot("cutoff"), kCutoffChannel) << "a parameter holds one slot";
    EXPECT_EQ(f.module->bindModSlot("reso"), kCutoffChannel + 1);

    const auto targets = f.module->getModulationTargets();
    ASSERT_EQ(targets.size(), 2u);
    EXPECT_EQ(targets[0].name, "Cutoff");
    EXPECT_EQ(targets[0].paramId, "cutoff");
    EXPECT_TRUE(targets[0].knobOnly);
    EXPECT_EQ(f.module->modChannelForParam("reso"), kCutoffChannel + 1);

    const auto port = f.module->mapInputChannel(kCutoffChannel);
    EXPECT_EQ(port.role, PortRole::ModCV);
    EXPECT_EQ(port.visibleJackIndex, kCutoffChannel) << "never shares a jack with the audio inputs";
    EXPECT_EQ(f.module->getVisibleInputPortCount(), 2) << "no gutter jacks appear";
}

TEST(HostedPluginModSlotTest, CvMovesTheParameterAroundItsRestingValue) {
    Fixture f;
    ASSERT_TRUE(f.load());
    ASSERT_EQ(f.module->bindModSlot("cutoff"), kCutoffChannel);

    f.render(kCutoffChannel, 0.3f);
    EXPECT_NEAR(f.param("cutoff").getValue(), 0.5f, 1e-5f);
    EXPECT_NEAR(*f.module->modSlotBase("cutoff"), 0.2f, 1e-5f) << "the base is not dragged along by the CV";

    f.render(kCutoffChannel, -0.5f);
    EXPECT_NEAR(f.param("cutoff").getValue(), 0.0f, 1e-5f) << "clamped at the bottom";

    f.render(kCutoffChannel, 0.0f);
    EXPECT_NEAR(f.param("cutoff").getValue(), 0.2f, 1e-5f);
    EXPECT_NEAR(f.param("reso").getValue(), 0.5f, 1e-6f) << "an unmodulated parameter is never written";
}

TEST(HostedPluginModSlotTest, TurningItElsewhereBecomesTheNewRestingValue) {
    Fixture f;
    ASSERT_TRUE(f.load());
    ASSERT_EQ(f.module->bindModSlot("cutoff"), kCutoffChannel);
    f.render(kCutoffChannel, 0.1f);
    ASSERT_NEAR(f.param("cutoff").getValue(), 0.3f, 1e-5f);

    f.param("cutoff").setValue(0.7f); // the plugin's own window, the card knob or an automation lane
    f.render(kCutoffChannel, 0.1f);

    EXPECT_NEAR(*f.module->modSlotBase("cutoff"), 0.7f, 1e-5f);
    EXPECT_NEAR(f.param("cutoff").getValue(), 0.8f, 1e-5f);
}

TEST(HostedPluginModSlotTest, ReleasingTheSlotPutsTheParameterBackAtRest) {
    Fixture f;
    ASSERT_TRUE(f.load());
    ASSERT_EQ(f.module->bindModSlot("cutoff"), kCutoffChannel);
    f.render(kCutoffChannel, 0.4f);
    ASSERT_NEAR(f.param("cutoff").getValue(), 0.6f, 1e-5f);

    f.module->releaseModSlot(kCutoffChannel);

    EXPECT_NEAR(f.param("cutoff").getValue(), 0.2f, 1e-5f);
    EXPECT_TRUE(f.module->getModulationTargets().empty());
    f.render(kCutoffChannel, 0.4f);
    EXPECT_NEAR(f.param("cutoff").getValue(), 0.2f, 1e-5f) << "a freed slot never writes again";
}

TEST(HostedPluginModSlotTest, LegacyParametersWithoutIdsCanBeModulated) {
    Fixture f;
    ASSERT_TRUE(f.load());
    ASSERT_EQ(f.module->bindModSlot("legacy:2"), kCutoffChannel);
    f.render(kCutoffChannel, 0.1f);
    EXPECT_NEAR(f.module->findInstanceParameterByIndex(2)->getValue(), 0.5f, 1e-5f);
}

TEST(HostedPluginModSlotTest, EverySlotTakenRefusesTheNext) {
    Fixture f;
    ASSERT_TRUE(f.load());
    for (int k = 0; k < HostedPluginModule::kMaxModSlots; ++k)
        ASSERT_EQ(f.module->bindModSlot("p" + juce::String(k)), kCutoffChannel + k);
    EXPECT_EQ(f.module->bindModSlot("one-too-many"), -1);
}

TEST(HostedPluginModSlotTest, SaveKeepsTheRestingValueNotTheModulatedOne) {
    Fixture f;
    ASSERT_TRUE(f.load());
    ASSERT_EQ(f.module->bindModSlot("reso"), kCutoffChannel);
    f.render(kCutoffChannel, 0.3f);
    ASSERT_NEAR(f.param("reso").getValue(), 0.8f, 1e-5f);

    const auto state = f.module->getExtraState();
    const auto* slots = state.getProperty("modSlots", {}).getArray();
    ASSERT_NE(slots, nullptr);
    ASSERT_EQ(slots->size(), 1);
    EXPECT_EQ((*slots)[0].getProperty("paramId", {}).toString(), "reso");
    EXPECT_NEAR((float)(*slots)[0].getProperty("base", 0.0), 0.5f, 1e-5f);

    Fixture reopened;
    reopened.backend.setFactory(
        [] { return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 0x5754424, "VST3", defaultParams()); });
    const synth::HostedPluginBackend::ScopedDefault scope(&reopened.backend);
    reopened.module->prepareToPlay(kSampleRate, kBlockSize);
    reopened.module->setExtraState(state);
    ASSERT_TRUE(pumpUntil([&] { return reopened.module->hasInstance(); }));
    reopened.param("reso").setValue(0.8f); // whatever the plugin's own state left it at

    reopened.render(kCutoffChannel, 0.0f);
    EXPECT_EQ(reopened.module->modChannelForParam("reso"), kCutoffChannel);
    EXPECT_NEAR(reopened.param("reso").getValue(), 0.5f, 1e-5f);
}

TEST(HostedPluginModSlotTest, AStateWithoutSlotsLoadsWithNone) {
    Fixture f;
    ASSERT_TRUE(f.load());
    auto state = f.module->getExtraState();
    ASSERT_FALSE(state.hasProperty("modSlots")) << "nothing is written while nothing is modulated";

    Fixture reopened;
    const synth::HostedPluginBackend::ScopedDefault scope(&reopened.backend);
    reopened.module->setExtraState(state);
    ASSERT_TRUE(pumpUntil([&] { return reopened.module->hasInstance(); }));
    EXPECT_TRUE(reopened.module->getModulationTargets().empty());
}

TEST(HostedPluginModSlotTest, ParameterMissingFromTheNewInstanceIsReported) {
    Fixture f;
    ASSERT_TRUE(f.load());
    ASSERT_EQ(f.module->bindModSlot("reso"), kCutoffChannel);
    const auto state = f.module->getExtraState();

    Fixture updated; // the plugin updated and dropped "reso"
    updated.backend.setFactory([] {
        return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 0x5754424, "VST3",
                                                    std::vector<StubParamSpec>{{"cutoff", "Cutoff", 0.2f}});
    });
    juce::StringArray missing;
    updated.module->onModSlotsUnresolved = [&missing](const juce::StringArray& ids) { missing = ids; };
    const synth::HostedPluginBackend::ScopedDefault scope(&updated.backend);
    updated.module->setExtraState(state);
    ASSERT_TRUE(pumpUntil([&] { return !missing.isEmpty(); }));
    EXPECT_EQ(missing, juce::StringArray("reso"));
}

TEST(HostedPluginModSlotTest, RestoringTheLivePluginsOwnStateDoesNotReloadIt) {
    Fixture f;
    ASSERT_TRUE(f.load());
    const auto before = f.module->getExtraState();
    const int loads = f.backend.createCount;

    ASSERT_EQ(f.module->bindModSlot("cutoff"), kCutoffChannel);
    f.render(kCutoffChannel, 0.3f);
    f.module->setExtraState(before); // undo the cable's step

    EXPECT_EQ(f.backend.createCount, loads) << "undo must not reload the plugin";
    EXPECT_TRUE(f.module->hasInstance());
    EXPECT_TRUE(f.module->getModulationTargets().empty());
}

// The engine frees a slot once no cable reaches its channel, whichever path removed the cable.
TEST(HostedPluginModSlotTest, RemovingTheLastCableFreesTheSlot) {
    AudioEngine engine;
    engine.initialise();
    StubBackend backend;
    backend.setFactory(
        [] { return std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 0x5754424, "VST3", defaultParams()); });
    auto owned = std::make_unique<HostedPluginModule>();
    auto* hosted = owned.get();
    auto& graph = engine.getGraph();
    auto hostedNode = graph.addNode(std::move(owned));
    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    ASSERT_NE(lfoNode, nullptr);
    hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return hosted->hasInstance(); }));

    const int channel = hosted->bindModSlot("cutoff");
    const auto atten = engine.addModRouting(lfoNode->nodeID, 0, hostedNode->nodeID, channel);
    ASSERT_NE(atten.uid, 0u);
    ASSERT_EQ(engine.getActiveModRoutings().size(), 1u);
    engine.releaseUnconnectedHostedModSlots();
    EXPECT_EQ(hosted->modChannelForParam("cutoff"), channel) << "a slot with a cable stays";

    engine.removeModRouting(atten);
    EXPECT_EQ(hosted->modChannelForParam("cutoff"), -1);
}
