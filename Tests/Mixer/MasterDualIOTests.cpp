// Master's Dual I/O (inputs only): split = the four Mix/Direct jacks (the default, so a project
// saved before the toggle opens unchanged); collapsed = "Mix" and "Direct", each one stereo jack
// over a contiguous raw pair. Also pins spliceMasterNode's look-through of macro ports and its
// onNewModule hook.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/MasterModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

void setDualIO(juce::AudioProcessor& processor, bool dual) {
    for (auto* param : processor.getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param); p != nullptr && p->paramID == "dualIO")
            p->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

} // namespace

TEST(MasterDualIO, DefaultsToSplitWithTheFourOriginalJacks) {
    MasterModule master;
    ASSERT_TRUE(master.hasDualIOParameter());
    EXPECT_TRUE(master.isDualIO());
    EXPECT_EQ(master.getVisibleInputPortCount(), 4);
    EXPECT_EQ(master.getInputPortLabel(0), "Mix L");
    EXPECT_EQ(master.getInputPortLabel(1), "Mix R");
    EXPECT_EQ(master.getInputPortLabel(2), "Direct L");
    EXPECT_EQ(master.getInputPortLabel(3), "Direct R");
    EXPECT_EQ(master.getVisibleOutputPortCount(), 2);
}

TEST(MasterDualIO, CollapsedShowsMixAndDirectAsStereoJacks) {
    MasterModule master;
    setDualIO(master, false);
    ASSERT_FALSE(master.isDualIO());
    EXPECT_EQ(master.getVisibleInputPortCount(), 2);
    EXPECT_EQ(master.getInputPortLabel(0), "Mix");
    EXPECT_EQ(master.getInputPortLabel(1), "Direct");

    const auto mix = master.getJackTargets(0, true);
    ASSERT_EQ(mix.size(), 1u);
    EXPECT_EQ(mix[0].rawHeadChannel, MasterModule::kMixLeft);
    EXPECT_EQ(mix[0].voiceSpan, 2);
    const auto direct = master.getJackTargets(1, true);
    ASSERT_EQ(direct.size(), 1u);
    EXPECT_EQ(direct[0].rawHeadChannel, MasterModule::kDirectLeft);
    EXPECT_EQ(direct[0].voiceSpan, 2);

    // Outputs are never collapsed.
    EXPECT_EQ(master.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(master.getOutputPortLabel(0), "Left");
    EXPECT_EQ(master.getOutputPortLabel(1), "Right");
}

TEST(MasterDualIO, ASavedMasterWithoutTheParamLoadsSplit) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    auto master = graph.addNode(synth::AIStateMapper::createModule("Master"));
    ASSERT_NE(master, nullptr);
    setDualIO(*master->getProcessor(), false); // saved collapsed, then the param stripped below

    auto json = synth::AIStateMapper::graphToJSON(graph);
    int stripped = 0;
    if (auto* nodes = json.getProperty("nodes", {}).getArray())
        for (auto& node : *nodes)
            if (node.getProperty("type", {}).toString() == "Master")
                if (auto* params = node.getProperty("params", {}).getDynamicObject()) {
                    params->removeProperty("dualIO");
                    ++stripped;
                }
    ASSERT_EQ(stripped, 1);

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    auto* loaded = dynamic_cast<MasterModule*>(synth::findMasterNode(graph)->getProcessor());
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->isDualIO()) << "a project saved before the toggle existed must keep its four jacks";
    EXPECT_EQ(loaded->getVisibleInputPortCount(), 4);
}

TEST(MasterDualIO, SpliceClassifiesMixThroughAMacroPort) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto out = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    auto strip = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"));
    auto outlet = graph.addNode(synth::AIStateMapper::createModule("Macro Out"));
    auto osc = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    ASSERT_TRUE(out != nullptr && strip != nullptr && outlet != nullptr && osc != nullptr);
    auto* port = dynamic_cast<MacroOutletModule*>(outlet->getProcessor());
    ASSERT_NE(port, nullptr);
    port->setPortShape(MacroPortShape::StereoCollapsed, 1);

    // Strip -> outlet (stereo) -> Audio Output; an unrelated Oscillator -> Audio Output as well.
    ASSERT_TRUE(graph.addConnection({{strip->nodeID, 0}, {outlet->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{strip->nodeID, ChannelStripModule::kRightBase}, {outlet->nodeID, 1}}));
    ASSERT_TRUE(graph.addConnection({{outlet->nodeID, 0}, {out->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{outlet->nodeID, 1}, {out->nodeID, 1}}));
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 0}}));

    auto* master = synth::spliceMasterNode(graph, {0, 0});
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected({{outlet->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected({{outlet->nodeID, 1}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_FALSE(graph.isConnected({{outlet->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}}));
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}}));
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
}

TEST(MasterDualIO, SpliceRunsTheNewModuleHookOnTheFreshMaster) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    ASSERT_NE(graph.addNode(synth::AIStateMapper::createModule("Audio Output")), nullptr);

    juce::String hookedType;
    int calls = 0;
    auto* master = synth::spliceMasterNode(graph, {0, 0}, [&](juce::AudioProcessor& p, const juce::String& type) {
        ++calls;
        hookedType = type;
        setDualIO(p, false);
    });
    ASSERT_NE(master, nullptr);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(hookedType, "Master");
    auto* module = dynamic_cast<MasterModule*>(master->getProcessor());
    ASSERT_NE(module, nullptr);
    EXPECT_FALSE(module->isDualIO());
    EXPECT_EQ(module->getVisibleInputPortCount(), 2);

    // An existing Master is returned untouched and the hook is not run again.
    EXPECT_EQ(synth::spliceMasterNode(graph, {0, 0}, [&](juce::AudioProcessor&, const juce::String&) { ++calls; }),
              master);
    EXPECT_EQ(calls, 1);
}
