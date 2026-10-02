// ChannelFlowDualIOPreferenceTests.cpp
//
// The "Split Left/Right jacks on new modules" preference (and its per-module overrides) must
// apply to the modules "+ Track" builds, not only to modules dropped from the library. The channel
// infrastructure the chain forces a shape on (Channel Strip, Master) is deliberately NOT affected.
// Shared ChannelFlowTest fixture and helpers live in ChannelFlowTestFixture.h.

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/MainComponent.h"
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

    // Infrastructure keeps the shape the chain forces on it.
    auto* strip = dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->getShape(), ChannelStripModule::Shape::Stereo);
    EXPECT_FALSE(strip->isBypassed());
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
