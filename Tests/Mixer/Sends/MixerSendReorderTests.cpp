// MixerSendReorderTests.cpp (docs/mixer/sends-and-buses.md#reordering-sends): the Core
// reorder flows (synth::swapSends / synth::moveSendRow). Headless: a bare graph, no engine
// rendering and no UI -- except the one render test, which pushes real audio through a source
// strip to prove a swapped slot's cable really still reaches its bus.
//
// Groups:
//   1. SwapSendsExchangesCablesValuesAndBits -- both slots' cables, level/pan VALUES, and
//      active/pre/mute/mono bits move together; the parameter OBJECTS (send1Level/send2Level)
//      stay put, only what they hold moves.
//   2. SwapSendsCarriesAModuleOnTheSendPath -- a cable through a user-inserted module still moves.
//   3. SwapSendsIsANoOpForTheSameSlotAndRefusesOutOfRange.
//   4. MoveSendRowSparseCase -- slots 0 and 2 active, moving row 1 above row 0 swaps exactly slots
//      0 and 2 and leaves slot 1 untouched.
//   5. MoveSendRowReportsTheAppliedSwapSequence -- the out-param MixerPanelComponent replays
//      against automation lanes.
//   6. RenderProvesTheSwappedSlotStillReachesItsBus -- signal that went out slot 0 to bus A comes
//      out of slot 1 after the swap and still reaches bus A.

#include "AI/AIStateMapper/AIStateMapper.h"
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
    NodeID output, master, source, busA, busB, busC;

    Rig() {
        graph.setPlayConfigDetails(2, 2, 48000.0, 64);
        output = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        source = addFactoryNode(graph, "Channel Strip");
        busA = addFactoryNode(graph, "Channel Strip");
        busB = addFactoryNode(graph, "Channel Strip");
        busC = addFactoryNode(graph, "Channel Strip");
        for (auto id : {busA, busB, busC})
            stripAt(graph, id)->setIsBus(true);
        for (auto strip : {source, busA, busB, busC}) {
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {output, 0}});
        graph.addConnection({{master, 1}, {output, 1}});
    }
};

} // namespace

TEST(MixerSendReorderTest, SwapSendsExchangesCablesValuesAndBits) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1);
    auto* strip = stripAt(rig.graph, rig.source);
    strip->setSendPreFader(0, true);
    strip->setSendMuted(1, true);
    strip->setSendMono(0, true);
    strip->getSendLevelParameter(0)->setValueNotifyingHost(
        strip->getSendLevelParameter(0)->getNormalisableRange().convertTo0to1(-6.0f));
    strip->getSendPanParameter(1)->setValueNotifyingHost(
        strip->getSendPanParameter(1)->getNormalisableRange().convertTo0to1(0.5f));

    ASSERT_TRUE(synth::swapSends(rig.graph, rig.source, 0, 1));

    // Cables: slot 0 now feeds bus B, slot 1 now feeds bus A.
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 0, rig.busB));
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 1, rig.busA));
    EXPECT_FALSE(sendIsWired(rig.graph, rig.source, 0, rig.busA));
    EXPECT_FALSE(sendIsWired(rig.graph, rig.source, 1, rig.busB));

    // Bits moved with their slot.
    EXPECT_FALSE(strip->isSendPreFader(0)) << "slot 0 now holds what was slot 1's (post-fader)";
    EXPECT_TRUE(strip->isSendPreFader(1)) << "slot 1 now holds what was slot 0's (pre-fader)";
    EXPECT_TRUE(strip->isSendMuted(0)) << "slot 0 now holds what was slot 1's (muted)";
    EXPECT_FALSE(strip->isSendMuted(1)) << "slot 1 now holds what was slot 0's (unmuted)";
    EXPECT_FALSE(strip->isSendMono(0)) << "slot 0 now holds what was slot 1's (stereo)";
    EXPECT_TRUE(strip->isSendMono(1)) << "slot 1 now holds what was slot 0's (mono)";
    EXPECT_TRUE(strip->isSendActive(0));
    EXPECT_TRUE(strip->isSendActive(1));

    // Values moved -- the PARAMETER OBJECTS stay fixed to their slot ("send1Level" always names
    // slot 0), only what they hold moves.
    EXPECT_NEAR(strip->getSendLevelParameter(0)->get(), 0.0f, 0.05f) << "slot 0 now holds slot 1's unity level";
    EXPECT_NEAR(strip->getSendLevelParameter(1)->get(), -6.0f, 0.05f) << "slot 1 now holds slot 0's -6dB";
    EXPECT_NEAR(strip->getSendPanParameter(1)->get(), 0.0f, 0.01f) << "slot 1 now holds slot 0's centred pan";
    EXPECT_NEAR(strip->getSendPanParameter(0)->get(), 0.5f, 0.01f) << "slot 0 now holds slot 1's panned value";
}

TEST(MixerSendReorderTest, SwapSendsCarriesAModuleOnTheSendPath) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1);

    // Splice a Reverb into slot 0's path, same shape as MixerSendFlowTests's own
    // FindSendTargetWalksThroughAModuleOnTheSendPath.
    const auto reverb = addFactoryNode(rig.graph, "Reverb");
    rig.graph.removeConnection({{rig.source, ChannelStripModule::sendLeftChannel(0)}, {rig.busA, 0}});
    rig.graph.removeConnection({{rig.source, ChannelStripModule::sendRightChannel(0)}, {rig.busA, kRight}});
    rig.graph.addConnection({{rig.source, ChannelStripModule::sendLeftChannel(0)}, {reverb, 0}});
    rig.graph.addConnection({{reverb, 0}, {rig.busA, 0}});

    ASSERT_TRUE(synth::swapSends(rig.graph, rig.source, 0, 1));
    EXPECT_TRUE(rig.graph.isConnected({{rig.source, ChannelStripModule::sendLeftChannel(1)}, {reverb, 0}}))
        << "the module the user inserted on the send path moves with its slot";
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.source, 1), rig.busA);
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.source, 0), rig.busB);
}

TEST(MixerSendReorderTest, SwapSendsIsANoOpForTheSameSlotAndRefusesOutOfRange) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    EXPECT_TRUE(synth::swapSends(rig.graph, rig.source, 0, 0)) << "same slot: a legal no-op";
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 0, rig.busA));

    EXPECT_FALSE(synth::swapSends(rig.graph, rig.source, 0, ChannelStripModule::kMaxSends));
    EXPECT_FALSE(synth::swapSends(rig.graph, rig.master, 0, 1)) << "Master is not a strip";
}

TEST(MixerSendReorderTest, MoveSendRowSparseCase) {
    Rig rig;
    // Slots 0, 1 and 2 all active, then free the MIDDLE one -- slots 0 and 2 stay active (their own
    // raw channels, untouched), slot 1 is the sparse gap (docs/mixer/sends-and-buses.md).
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busC), 2);
    ASSERT_TRUE(synth::removeSend(rig.graph, rig.source, 1));
    auto* strip = stripAt(rig.graph, rig.source);
    ASSERT_TRUE(strip->isSendActive(0));
    ASSERT_FALSE(strip->isSendActive(1));
    ASSERT_TRUE(strip->isSendActive(2));

    std::vector<std::pair<int, int>> swaps;
    // Visible rows: row 0 = slot 0 (busA), row 1 = slot 2 (busC). Move row 1 above row 0.
    ASSERT_TRUE(synth::moveSendRow(rig.graph, rig.source, 1, 0, &swaps));

    EXPECT_EQ(swaps.size(), 1u);
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 0, rig.busC)) << "slot 0 now holds what was slot 2's";
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 2, rig.busA)) << "slot 2 now holds what was slot 0's";
    EXPECT_FALSE(strip->isSendActive(1)) << "the sparse gap at slot 1 is never touched";
}

TEST(MixerSendReorderTest, MoveSendRowReportsTheAppliedSwapSequence) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busC), 2);

    std::vector<std::pair<int, int>> swaps;
    ASSERT_TRUE(synth::moveSendRow(rig.graph, rig.source, 2, 0, &swaps));
    // A walk of adjacent swaps from row 2 to row 0: (slot2,slot1) then (slot1,slot0) -- each step
    // carries row 2's content one position closer, through slot 1, and finally into slot 0.
    ASSERT_EQ(swaps.size(), 2u);
    EXPECT_EQ(swaps[0], std::make_pair(2, 1));
    EXPECT_EQ(swaps[1], std::make_pair(1, 0));
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 0, rig.busC)) << "row 2's send is now in slot 0";
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 1, rig.busA)) << "row 0's send shifted down into slot 1";
    EXPECT_TRUE(sendIsWired(rig.graph, rig.source, 2, rig.busB)) << "row 1's send stayed in slot 2 (untouched)";

    // fromRow == toRow: a legal no-op, no swaps reported.
    swaps.clear();
    EXPECT_FALSE(synth::moveSendRow(rig.graph, rig.source, 1, 1, &swaps));
    EXPECT_TRUE(swaps.empty());

    // Out of range: nothing changed, nothing reported.
    swaps.push_back({99, 99});
    EXPECT_FALSE(synth::moveSendRow(rig.graph, rig.source, 0, 5, &swaps));
    EXPECT_TRUE(swaps.empty()) << "cleared even on refusal";
}

TEST(MixerSendReorderTest, RenderProvesTheSwappedSlotStillReachesItsBus) {
    Rig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1);
    auto* strip = stripAt(rig.graph, rig.source);
    // Slot 0 (-> bus A) quiet, slot 1 (-> bus B) unity, so the two are distinguishable in the
    // rendered signal, not just by topology.
    strip->getSendLevelParameter(0)->setValueNotifyingHost(
        strip->getSendLevelParameter(0)->getNormalisableRange().convertTo0to1(-12.0f));

    ASSERT_TRUE(synth::swapSends(rig.graph, rig.source, 0, 1));
    // Topology: slot 1's cable is now the one reaching bus A.
    ASSERT_TRUE(sendIsWired(rig.graph, rig.source, 1, rig.busA));
    ASSERT_TRUE(sendIsWired(rig.graph, rig.source, 0, rig.busB));

    // Render: push a real signal through the strip's own processBlock (MixerSendLevelTests.cpp's
    // convention -- a strip-sized buffer with the two inputs set, ramps settled) and read what
    // lands on each slot's raw channels.
    strip->prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumOutputs, 64);
    buffer.clear();
    for (int i = 0; i < 64; ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(kRight, i, 1.0f);
    }
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) { // let the level ramp settle, same margin MixerSendLevelTests uses
        auto scratch = buffer;
        strip->processBlock(scratch, midi);
    }
    strip->processBlock(buffer, midi);

    // The whole send (its cable AND its level) moved together with the slot: slot 1 now IS "the
    // -12dB send to bus A" that used to live in slot 0, and slot 0 now IS "the unity send to bus B"
    // that used to live in slot 1.
    const float slot1Output = buffer.getSample(ChannelStripModule::sendLeftChannel(1), 32);
    const float slot0Output = buffer.getSample(ChannelStripModule::sendLeftChannel(0), 32);
    const float expectedQuietGain = juce::Decibels::decibelsToGain(-12.0f);
    EXPECT_NEAR(slot1Output, expectedQuietGain, 0.02f)
        << "slot 1's cable now reaches bus A, carrying the -12dB level that used to be slot 0's";
    EXPECT_NEAR(slot0Output, 1.0f, 0.05f)
        << "slot 0's cable now reaches bus B, carrying the unity level that used to be slot 1's";
}
