// MixerSendFlowTests.cpp -- FRO15 (P9-9, docs/mixer/sends-and-buses.md): the Core send/bus flows
// (synth::addSend / removeSend / retargetSend / findSendTarget / enumerateSendTargets) and
// synth::buildBusChannel. Headless: a bare graph, no engine rendering and no UI.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
constexpr int kRight = ChannelStripModule::kRightBase;

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

bool sendIsWired(juce::AudioProcessorGraph& graph, NodeID source, int slot, NodeID target) {
    return graph.isConnected({{source, ChannelStripModule::sendLeftChannel(slot)}, {target, 0}}) &&
           graph.isConnected({{source, ChannelStripModule::sendRightChannel(slot)}, {target, kRight}});
}

struct Rig {
    juce::AudioProcessorGraph graph;
    NodeID output, master, sourceA, sourceB, bus;

    Rig() {
        graph.setPlayConfigDetails(2, 2, 48000.0, 64);
        output = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        sourceA = addFactoryNode(graph, "Channel Strip");
        sourceB = addFactoryNode(graph, "Channel Strip");
        bus = addFactoryNode(graph, "Channel Strip");
        stripAt(graph, bus)->setIsBus(true);
        for (auto strip : {sourceA, sourceB, bus}) {
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {output, 0}});
        graph.addConnection({{master, 1}, {output, 1}});
    }
};

} // namespace

TEST(MixerSendFlowTest, AddSendActivatesTheLowestFreeSlotAndWiresBothLegs) {
    Rig rig;
    EXPECT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), 0);
    EXPECT_TRUE(stripAt(rig.graph, rig.sourceA)->isSendActive(0));
    EXPECT_FALSE(stripAt(rig.graph, rig.sourceA)->isSendPreFader(0)) << "post-fader by default";
    EXPECT_TRUE(sendIsWired(rig.graph, rig.sourceA, 0, rig.bus));
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.sourceA, 0), rig.bus);

    EXPECT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.sourceB), 1) << "the next send takes slot 1";
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.sourceA, 1), rig.sourceB);
}

TEST(MixerSendFlowTest, AddSendRefusesANonStripAndARunOutOfSlots) {
    Rig rig;
    EXPECT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.master), -1) << "Master is not a strip";
    EXPECT_EQ(synth::addSend(rig.graph, rig.master, rig.bus), -1);
    EXPECT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.sourceA), -1) << "a strip cannot send to itself";
    EXPECT_EQ(stripAt(rig.graph, rig.sourceA)->getActiveSendCount(), 0) << "a refusal changes nothing";

    for (int i = 0; i < ChannelStripModule::kMaxSends; ++i)
        ASSERT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), i);
    EXPECT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), -1) << "all slots in use";
    EXPECT_EQ(stripAt(rig.graph, rig.sourceA)->getActiveSendCount(), ChannelStripModule::kMaxSends);
}

TEST(MixerSendFlowTest, RemoveSendClearsTheCablesAndTheSlotOnly) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.sourceB), 1);
    stripAt(rig.graph, rig.sourceA)->setSendPreFader(0, true);

    ASSERT_TRUE(synth::removeSend(rig.graph, rig.sourceA, 0));
    EXPECT_FALSE(stripAt(rig.graph, rig.sourceA)->isSendActive(0));
    EXPECT_FALSE(stripAt(rig.graph, rig.sourceA)->isSendPreFader(0)) << "a freed slot forgets its pre/post";
    EXPECT_FALSE(sendIsWired(rig.graph, rig.sourceA, 0, rig.bus));
    EXPECT_TRUE(stripAt(rig.graph, rig.sourceA)->isSendActive(1)) << "the higher slot is untouched";
    EXPECT_TRUE(sendIsWired(rig.graph, rig.sourceA, 1, rig.sourceB)) << "and still on its own raw channels";

    EXPECT_FALSE(synth::removeSend(rig.graph, rig.sourceA, 0)) << "removing an inactive slot is a no-op";
    EXPECT_FALSE(synth::removeSend(rig.graph, rig.master, 0));
}

TEST(MixerSendFlowTest, RetargetSendMovesTheCablesAndKeepsTheSlot) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), 0);
    stripAt(rig.graph, rig.sourceA)->setSendPreFader(0, true);

    ASSERT_TRUE(synth::retargetSend(rig.graph, rig.sourceA, 0, rig.sourceB));
    EXPECT_FALSE(sendIsWired(rig.graph, rig.sourceA, 0, rig.bus));
    EXPECT_TRUE(sendIsWired(rig.graph, rig.sourceA, 0, rig.sourceB));
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.sourceA, 0), rig.sourceB);
    EXPECT_TRUE(stripAt(rig.graph, rig.sourceA)->isSendPreFader(0)) << "retargeting is not re-creating";

    EXPECT_TRUE(synth::retargetSend(rig.graph, rig.sourceA, 0, rig.sourceB)) << "already there: a no-op success";
    EXPECT_FALSE(synth::retargetSend(rig.graph, rig.sourceA, 1, rig.bus)) << "slot 1 is not active";
}

TEST(MixerSendFlowTest, CyclicTargetsAreExcludedFromTheSendMenu) {
    Rig rig;
    // sourceA -> bus makes the bus downstream of A, so a send from the BUS back to A would close a
    // loop and must never be offered.
    ASSERT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), 0);

    const auto fromBus = synth::enumerateSendTargets(rig.graph, rig.bus);
    EXPECT_EQ(std::count(fromBus.begin(), fromBus.end(), rig.sourceA), 0) << "that target would close a cycle";
    EXPECT_EQ(std::count(fromBus.begin(), fromBus.end(), rig.sourceB), 1);
    EXPECT_EQ(std::count(fromBus.begin(), fromBus.end(), rig.bus), 0) << "never itself";
    EXPECT_EQ(std::count(fromBus.begin(), fromBus.end(), rig.master), 0) << "only strips";

    EXPECT_EQ(synth::addSend(rig.graph, rig.bus, rig.sourceA), -1) << "and the flow refuses it too";
    EXPECT_EQ(stripAt(rig.graph, rig.bus)->getActiveSendCount(), 0);

    // A itself may still send anywhere else.
    const auto fromA = synth::enumerateSendTargets(rig.graph, rig.sourceA);
    EXPECT_EQ(fromA.size(), 2u);
}

TEST(MixerSendFlowTest, FindSendTargetWalksThroughAModuleOnTheSendPath) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.sourceA, rig.bus), 0);
    const auto reverb = addFactoryNode(rig.graph, "Reverb");

    rig.graph.removeConnection({{rig.sourceA, ChannelStripModule::sendLeftChannel(0)}, {rig.bus, 0}});
    rig.graph.removeConnection({{rig.sourceA, ChannelStripModule::sendRightChannel(0)}, {rig.bus, kRight}});
    rig.graph.addConnection({{rig.sourceA, ChannelStripModule::sendLeftChannel(0)}, {reverb, 0}});
    rig.graph.addConnection({{reverb, 0}, {rig.bus, 0}});

    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.sourceA, 0), rig.bus)
        << "the bus behind a user-inserted module still resolves";

    // And a send whose cable was cut entirely resolves to nothing, rather than to a stale id.
    ASSERT_EQ(synth::addSend(rig.graph, rig.sourceB, rig.bus), 0);
    rig.graph.removeConnection({{rig.sourceB, ChannelStripModule::sendLeftChannel(0)}, {rig.bus, 0}});
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.sourceB, 0), NodeID{});
}

TEST(MixerSendFlowTest, BuildBusChannelMakesAFlaggedEmptyChannelIntoMaster) {
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000.0, 64);
    const auto output = addFactoryNode(graph, "Audio Output");
    ASSERT_NE(output, NodeID{});

    const auto channel = synth::buildBusChannel(graph, {{0, 0}, {100, 0}, {200, 0}, {300, 0}});
    ASSERT_NE(channel.strip, nullptr);
    ASSERT_NE(channel.master, nullptr);
    EXPECT_TRUE(channel.eqUuid.isNotEmpty());
    EXPECT_TRUE(channel.compressorUuid.isNotEmpty());

    auto* strip = dynamic_cast<ChannelStripModule*>(channel.strip->getProcessor());
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->isBus()) << "the flag is what classifies a bus that nothing feeds yet";
    EXPECT_EQ(strip->getShape(), ChannelStripModule::Shape::Stereo);
    EXPECT_TRUE(graph.isConnected({{channel.strip->nodeID, 0}, {channel.master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(
        graph.isConnected({{channel.strip->nodeID, kRight}, {channel.master->nodeID, MasterModule::kMixRight}}));
    EXPECT_TRUE(synth::isBusStrip(graph, channel.strip->nodeID));
    EXPECT_EQ(synth::busFallbackName(graph, channel.strip->nodeID), "Bus 1");

    // The EQ and Compressor ship bypassed, same as every other channel's factory default.
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == channel.eqUuid ||
            node->properties["uuid"].toString() == channel.compressorUuid)
            EXPECT_TRUE(dynamic_cast<ModuleBase*>(node->getProcessor())->isBypassed());
}
