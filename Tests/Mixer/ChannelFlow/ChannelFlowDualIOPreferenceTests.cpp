// ChannelFlowDualIOPreferenceTests.cpp
//
// The "Split Left/Right jacks on new modules" preference (and its per-module overrides) must
// apply to the modules "Add Track" builds, not only to modules dropped from the library. The input SHAPE
// the chain forces on the Channel Strip (Stereo) is not affected; its Left/Right jacks follow the preference.
// Shared ChannelFlowTest fixture and helpers live in ChannelFlowTestFixture.h.

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <map>

namespace {

// The "dualIO" parameter's current value, or -1 when the processor has none.
int dualIOOf(juce::AudioProcessor& processor) {
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            return p->getValue() > 0.5f ? 1 : 0;
    return -1;
}

int dualIOOfNode(juce::AudioProcessorGraph& graph, ModuleType type) {
    auto* node = findNodeOfTypeCFT(graph, type);
    return node == nullptr ? -2 : dualIOOf(*node->getProcessor());
}

} // namespace

TEST_F(ChannelFlowTest, AudioTrackInsertsFollowSplitJacksPreferenceOn) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);

    addAudioTrack(mc);

    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Gate), 1);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Compressor), 1);

    // The strip keeps the shape the chain forces on it, and its jacks follow the preference like the inserts'.
    auto* strip = dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->getShape(), ChannelStripModule::Shape::Stereo);
    EXPECT_FALSE(strip->isBypassed());
    EXPECT_EQ(dualIOOf(*strip), 1);
    EXPECT_EQ(strip->getVisibleInputPortCount(), 2);
}

TEST_F(ChannelFlowTest, AudioTrackInsertsFollowSplitJacksPreferenceOff) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(false);

    addAudioTrack(mc);

    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Gate), 0);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::ParametricEQ), 0);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Compressor), 0);

    auto* strip = dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->getShape(), ChannelStripModule::Shape::Stereo);
    EXPECT_EQ(dualIOOf(*strip), 0) << "the Channel Strip follows the preference";
    EXPECT_EQ(strip->getVisibleInputPortCount(), 1);
    EXPECT_EQ(strip->getInputPortLabel(0), "Audio");

    // The Compressor's two legs still reach the strip's two raw legs: the one jack owns both.
    auto* compressor = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    EXPECT_TRUE(graph.isConnected({{compressor->nodeID, 0}, {stripNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{compressor->nodeID, 1}, {stripNode->nodeID, ChannelStripModule::kRightBase}}));
}

TEST_F(ChannelFlowTest, ChannelStripRespectsPerModuleOverride) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);
    mc.getGraphEditor().setDualIOPerModuleOverrides({{"Channel Strip", false}});

    addAudioTrack(mc);

    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::ChannelStrip), 0) << "the per-module override reaches the strip";
}

TEST_F(ChannelFlowTest, BusStripFollowsSplitJacksPreference) {
    for (const bool pref : {true, false}) {
        AudioEngine engine;
        GraphEditor editor(engine);
        editor.setDefaultDualIOForNewModules(pref);
        const synth::DefaultChannelLayout layout{{0, 0},
                                                 {300, 0},
                                                 {600, 0},
                                                 {900, 0},
                                                 {1200, 0},
                                                 [&editor](juce::AudioProcessor& p, const juce::String& type) {
                                                     editor.applyDualIODefaultTo(p, type); }};

        const auto bus = synth::buildBusChannel(engine.getGraph(), layout);

        ASSERT_NE(bus.strip, nullptr);
        EXPECT_EQ(dualIOOf(*bus.strip->getProcessor()), pref ? 1 : 0) << "pref=" << pref;
    }
}

TEST_F(ChannelFlowTest, AudioTrackInsertsRespectPerModuleOverride) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);
    mc.getGraphEditor().setDualIOPerModuleOverrides({{"Compressor", false}});

    addAudioTrack(mc);

    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Gate), 1);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(dualIOOfNode(graph, ModuleType::Compressor), 0) << "the per-module override wins over the global default";
}

TEST_F(ChannelFlowTest, InstrumentTrackModulesFollowSplitJacksPreference) {
    for (const bool pref : {true, false}) {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        auto& graph = mc.getAudioEngine().getGraph();
        mc.getGraphEditor().setDefaultDualIOForNewModules(pref);

        addInstrumentTrack(mc, "Oscillator");

        const int expected = pref ? 1 : 0;
        EXPECT_EQ(dualIOOfNode(graph, ModuleType::Oscillator), expected) << "pref=" << pref;
        EXPECT_EQ(dualIOOfNode(graph, ModuleType::Gate), expected) << "pref=" << pref;
        EXPECT_EQ(dualIOOfNode(graph, ModuleType::ParametricEQ), expected) << "pref=" << pref;
        EXPECT_EQ(dualIOOfNode(graph, ModuleType::Compressor), expected) << "pref=" << pref;

        auto* strip =
            dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
        ASSERT_NE(strip, nullptr);
        EXPECT_EQ(strip->getShape(), ChannelStripModule::Shape::Stereo) << "pref=" << pref;
    }
}
