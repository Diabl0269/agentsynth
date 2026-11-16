// ChannelFlowDefaultFilterTests.cpp
//
// The default Oscillator or Wavetable instrument track carries one Filter, bypassed, between the VCA and the
// Gate. Built through the menu and through the AI's addInstrumentTrack op (the same builder), mono and poly.

#include "../../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/FilterModule.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

juce::AudioProcessorParameterWithID* findParam(juce::AudioProcessor& processor, const juce::String& id) {
    for (auto* param : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param); withId && withId->paramID == id)
            return withId;
    return nullptr;
}

int countMembersOfType(juce::AudioProcessorGraph& graph, const synth::Macro& macro, ModuleType type) {
    int count = 0;
    for (const auto& uuid : macro.members)
        if (isModuleOfTypeCFT(nodeForUuidCFT(graph, uuid), type))
            ++count;
    return count;
}

// Checks Oscillator/Wavetable -> ADSR/VCA -> Filter(bypassed) -> Gate, both legs, for the one track in `mc`.
// A collapsed poly VCA has no right jack, so only there the VCA -> Filter right cable is absent.
void expectFilterBetweenVcaAndGate(MainComponent& mc, bool poly, bool splitJacks) {
    auto& graph = mc.getAudioEngine().getGraph();
    const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
    ASSERT_EQ(countMembersOfType(graph, macro, ModuleType::Filter), 1) << (poly ? "poly" : "mono");
    auto* filter = findMacroMemberOfTypeCFT(graph, macro, ModuleType::Filter);
    auto* vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    auto* gate = findMacroMemberOfTypeCFT(graph, macro, ModuleType::Gate);
    ASSERT_NE(filter, nullptr);
    ASSERT_NE(vca, nullptr);
    ASSERT_NE(gate, nullptr);

    auto* module = dynamic_cast<FilterModule*>(filter->getProcessor());
    ASSERT_NE(module, nullptr);
    EXPECT_TRUE(module->isBypassed()) << "off until the user turns it on";
    EXPECT_EQ(juce::String(module->getNodeUuid()), filter->properties["uuid"].toString())
        << "the node uuid is mirrored";
    EXPECT_FALSE(*dynamic_cast<juce::AudioParameterBool*>(findParam(*module, "poly")))
        << "the VCA already summed the voices";

    EXPECT_TRUE(graph.isConnected({{vca->nodeID, 0}, {filter->nodeID, 0}}));
    if (splitJacks || !poly)
        EXPECT_TRUE(
            graph.isConnected({{vca->nodeID, VCAModule::kRightBase}, {filter->nodeID, FilterModule::kRightBase}}));
    EXPECT_FALSE(graph.isConnected({{vca->nodeID, 0}, {filter->nodeID, 1}})) << "ch1 is the Cutoff CV";
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 0}, {gate->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, FilterModule::kRightBase}, {gate->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{vca->nodeID, 0}, {gate->nodeID, 0}})) << "the Filter sits between them";
}

struct Levels {
    float left = 0.0f;
    float right = 0.0f;
};

// Renders blocks through the real engine with a note held and returns the output RMS of the last four.
Levels renderLevel(AudioEngine& engine, TimelineMidiSourceModule& trackIn, int blocks = 40) {
    synth::test::FakeAudioIODevice fake{0, 2};
    engine.audioDeviceAboutToStart(&fake);
    trackIn.pushAuditionNote(60, 100, true);
    const int n = synth::test::kFakeDeviceBlockSize;
    std::vector<float> left((size_t)n), right((size_t)n);
    float* outs[] = {left.data(), right.data()};
    Levels sum;
    for (int b = 0; b < blocks; ++b) {
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
        if (b >= blocks - 4)
            for (int i = 0; i < n; ++i) {
                sum.left += left[(size_t)i] * left[(size_t)i];
                sum.right += right[(size_t)i] * right[(size_t)i];
            }
    }
    engine.audioDeviceStopped();
    return {std::sqrt(sum.left / (4.0f * (float)n)), std::sqrt(sum.right / (4.0f * (float)n))};
}

} // namespace

// Regression test for FRO700: a new Oscillator or Wavetable track had no Filter; now it has one, bypassed.
TEST_F(ChannelFlowTest, NewOscillatorAndWavetableTracksGetOneBypassedFilterBetweenVcaAndGate) {
    for (const bool splitJacks : {true, false})
        for (const juce::String instrument : {"Oscillator", "Wavetable"})
            for (const bool poly : {false, true}) {
                SCOPED_TRACE(splitJacks ? "split jacks" : "collapsed jacks");
                MainComponent mc(std::make_unique<MockProviderCFT>());
                mc.setSize(1600, 900);
                mc.getAudioEngine().suspendDeviceCallback();
                mc.getGraphEditor().setDefaultDualIOForNewModules(splitJacks);
                addInstrumentTrack(mc, instrument, poly);
                ASSERT_EQ(mc.getGraphEditor().getMacros().size(), 1) << instrument;
                expectFilterBetweenVcaAndGate(mc, poly, splitJacks);
            }
}

// Regression test for FRO700: the Filter is a macro member, so it moves, folds and deletes with its track.
TEST_F(ChannelFlowTest, TheDefaultFilterIsAMemberOfTheTrackMacro) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    addInstrumentTrack(mc, "Oscillator");
    auto& graph = mc.getAudioEngine().getGraph();
    const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
    EXPECT_EQ(countMembersOfType(graph, macro, ModuleType::Filter), 1);
}

// Regression test for FRO700: a Sampler track is untouched.
TEST_F(ChannelFlowTest, ASamplerTrackGetsNoDefaultFilter) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    addInstrumentTrack(mc, "Sampler");
    auto& graph = mc.getAudioEngine().getGraph();
    const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
    EXPECT_EQ(countMembersOfType(graph, macro, ModuleType::Filter), 0);
}

// Regression test for FRO700: with the Filter bypassed the track sounds exactly like the chain without it, and
// turning it on changes the sound (so the stage is really in the path).
TEST_F(ChannelFlowTest, ABypassedDefaultFilterLeavesTheRenderUnchanged) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);
    addInstrumentTrack(mc, "Oscillator");
    auto& graph = mc.getAudioEngine().getGraph();
    const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
    auto* filter = findMacroMemberOfTypeCFT(graph, macro, ModuleType::Filter);
    auto* vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    auto* gate = findMacroMemberOfTypeCFT(graph, macro, ModuleType::Gate);
    auto* trackInNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::TimelineMidiSource);
    ASSERT_NE(filter, nullptr);
    ASSERT_NE(vca, nullptr);
    ASSERT_NE(gate, nullptr);
    ASSERT_NE(trackInNode, nullptr);
    auto* trackIn = dynamic_cast<TimelineMidiSourceModule*>(trackInNode->getProcessor());
    ASSERT_NE(trackIn, nullptr);
    auto* module = dynamic_cast<FilterModule*>(filter->getProcessor());
    ASSERT_NE(module, nullptr);
    ASSERT_TRUE(module->isBypassed());

    auto& engine = mc.getAudioEngine();
    const auto withBypassedFilter = renderLevel(engine, *trackIn);
    ASSERT_GT(withBypassedFilter.left, 0.05f);

    // On with a very low cutoff: the level must fall, so the Filter is in the path on both legs.
    module->setBypassed(false);
    *dynamic_cast<juce::AudioParameterFloat*>(findParam(*module, "cutoff")) = 30.0f;
    const auto filtered = renderLevel(engine, *trackIn);
    EXPECT_LT(filtered.left, withBypassedFilter.left * 0.5f);
    EXPECT_LT(filtered.right, withBypassedFilter.right * 0.5f);

    // The old chain: the Filter's cables replaced by VCA -> Gate. Same level as the bypassed Filter.
    module->setBypassed(true);
    graph.removeNode(filter->nodeID);
    graph.addConnection({{vca->nodeID, 0}, {gate->nodeID, 0}});
    graph.addConnection({{vca->nodeID, VCAModule::kRightBase}, {gate->nodeID, 1}});
    const auto withoutFilter = renderLevel(engine, *trackIn);
    EXPECT_NEAR(withBypassedFilter.left, withoutFilter.left, withoutFilter.left * 0.02f);
    EXPECT_NEAR(withBypassedFilter.right, withoutFilter.right, withoutFilter.right * 0.02f);
}

// Regression test for FRO700: an AI-created track gets the same chain as the menu's (one builder behind both).
TEST_F(ChannelFlowTest, AnAiCreatedOscillatorTrackGetsTheSameBypassedFilter) {
    for (const bool poly : {false, true}) {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.getGraphEditor().setDefaultDualIOForNewModules(true);
        const juce::var envelope =
            juce::JSON::parse(juce::String("{\"timelineOps\": [{\"op\": \"addInstrumentTrack\", \"name\": \"Lead\", ") +
                              "\"instrument\": \"Oscillator\", \"poly\": " + (poly ? "true" : "false") + "}]}");
        const auto applied = mc.getAiServiceForTest().applyTimelineOps(envelope);
        ASSERT_TRUE(applied.ok) << applied.message;
        ASSERT_EQ(mc.getGraphEditor().getMacros().size(), 1);
        expectFilterBetweenVcaAndGate(mc, poly, /*splitJacks=*/true);
    }
}
