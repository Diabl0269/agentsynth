// The per-leg mixer solo rule (FRO15 / P9-9, docs/mixer/sends-and-buses.md#solo-is-a-per-leg-audible-mask).
//
//   * the rule       -- synth::computeSoloAudibleLegs against hand-built graphs: soloing a send bus
//                        opens only its sources' SEND legs; soloing a group bus opens its sources'
//                        MAIN legs; soloing a source keeps the buses it feeds audible; a strip that
//                        contributes nothing to the soloed path is fully silenced
//   * the limitation -- a main leg feeding BOTH Master and a soloed bus stays open (per-leg, not
//                        per-edge), asserted so the trade-off can't drift unnoticed
//   * publication    -- AudioEngine::refreshSoloGate hands each strip its mask, un-soloing restores
//                        every one of them, and an undo/redo across a graph REBUILD settles them at
//                        the same publishTimeline seam the count already settles at
//   * Master         -- Direct is still gated by the plain global "is anything soloed?" count
//
// The audio a gated/open leg actually carries is MixerSendLevelTests.cpp's business.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/SoloAudibleSet.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;
constexpr juce::uint32 kMain = ChannelStripModule::kMainLegBit;

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

void wireMainTo(juce::AudioProcessorGraph& graph, NodeID from, NodeID to, int leftIn, int rightIn) {
    graph.addConnection({{from, 0}, {to, leftIn}});
    graph.addConnection({{from, kRight}, {to, rightIn}});
}

void wireSendTo(juce::AudioProcessorGraph& graph, NodeID from, int slot, NodeID toStrip) {
    stripAt(graph, from)->setSendActive(slot, true);
    graph.addConnection({{from, ChannelStripModule::sendLeftChannel(slot)}, {toStrip, 0}});
    graph.addConnection({{from, ChannelStripModule::sendRightChannel(slot)}, {toStrip, kRight}});
}

juce::uint32 maskOf(const std::map<NodeID, juce::uint32>& masks, NodeID id) {
    const auto found = masks.find(id);
    return found != masks.end() ? found->second : 0u;
}

// source A and source B into Master's Mix; A also sends slot 0 into bus C, which itself goes to
// Master's Mix. The canonical "reverb send" shape.
struct SendRig {
    juce::AudioProcessorGraph graph;
    NodeID sourceA, sourceB, bus, master, output;

    SendRig() {
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        output = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        sourceA = addFactoryNode(graph, "Channel Strip");
        sourceB = addFactoryNode(graph, "Channel Strip");
        bus = addFactoryNode(graph, "Channel Strip");
        stripAt(graph, bus)->setIsBus(true);

        for (auto strip : {sourceA, sourceB, bus})
            wireMainTo(graph, strip, master, MasterModule::kMixLeft, MasterModule::kMixRight);
        wireSendTo(graph, sourceA, 0, bus);
        graph.addConnection({{master, 0}, {output, 0}});
        graph.addConnection({{master, 1}, {output, 1}});
    }
};

} // namespace

// ============================================================================
// The rule
// ============================================================================

TEST(MixerBusSoloTest, NothingSoloedOpensEveryLeg) {
    SendRig rig;
    const auto masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(masks.size(), 3u);
    for (auto strip : {rig.sourceA, rig.sourceB, rig.bus})
        EXPECT_EQ(maskOf(masks, strip), ~0u) << "with nothing soloed the gate is never consulted";
}

TEST(MixerBusSoloTest, SoloingASendBusOpensOnlyTheSourcesSendLeg) {
    SendRig rig;
    stripAt(rig.graph, rig.bus)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(rig.graph);

    EXPECT_EQ(maskOf(masks, rig.bus), ~0u) << "the soloed bus itself is fully audible";
    EXPECT_EQ(maskOf(masks, rig.sourceA), ChannelStripModule::sendLegBit(0))
        << "the source feeds the soloed bus through its send only -- its dry main leg closes";
    EXPECT_EQ(maskOf(masks, rig.sourceB), 0u) << "a source that feeds nothing soloed is fully silenced";
}

TEST(MixerBusSoloTest, SoloingAGroupBusOpensItsSourcesMainLegs) {
    // A group bus: the sources feed it through their MAIN legs and have no sends at all.
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    const auto output = addFactoryNode(graph, "Audio Output");
    const auto master = addFactoryNode(graph, "Master");
    const auto sourceA = addFactoryNode(graph, "Channel Strip");
    const auto sourceB = addFactoryNode(graph, "Channel Strip");
    const auto other = addFactoryNode(graph, "Channel Strip");
    const auto bus = addFactoryNode(graph, "Channel Strip");

    wireMainTo(graph, sourceA, bus, 0, kRight);
    wireMainTo(graph, sourceB, bus, 0, kRight);
    wireMainTo(graph, bus, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    wireMainTo(graph, other, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    graph.addConnection({{master, 0}, {output, 0}});

    stripAt(graph, bus)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(graph);

    EXPECT_EQ(maskOf(masks, bus), ~0u);
    EXPECT_EQ(maskOf(masks, sourceA) & kMain, kMain) << "a group bus is useless if its sources close";
    EXPECT_EQ(maskOf(masks, sourceB) & kMain, kMain);
    EXPECT_EQ(maskOf(masks, other), 0u);
}

TEST(MixerBusSoloTest, SoloingABusFedByAnotherBusKeepsTheWholeChainAudible) {
    // Nested group buses -- source -> inner bus -> outer bus -> Master -- with the OUTER one soloed.
    // §5.15 D5's rule (c) is "that leg lies on a signal path reaching a soloed strip", and the
    // source's path does reach it, two strips away. A walk that answered only "is the FIRST strip I
    // meet soloed?" would close the source's main leg, and the soloed bus would then be fed silence
    // by a bus that is itself audible -- soloing a nested bus would produce nothing at all.
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    const auto output = addFactoryNode(graph, "Audio Output");
    const auto master = addFactoryNode(graph, "Master");
    const auto source = addFactoryNode(graph, "Channel Strip");
    const auto innerBus = addFactoryNode(graph, "Channel Strip");
    const auto outerBus = addFactoryNode(graph, "Channel Strip");
    const auto other = addFactoryNode(graph, "Channel Strip");
    stripAt(graph, innerBus)->setIsBus(true);
    stripAt(graph, outerBus)->setIsBus(true);

    wireMainTo(graph, source, innerBus, 0, kRight);
    wireMainTo(graph, innerBus, outerBus, 0, kRight);
    wireMainTo(graph, outerBus, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    wireMainTo(graph, other, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    graph.addConnection({{master, 0}, {output, 0}});

    stripAt(graph, outerBus)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(graph);

    EXPECT_EQ(maskOf(masks, outerBus), ~0u);
    EXPECT_EQ(maskOf(masks, innerBus) & kMain, kMain) << "it feeds the soloed bus directly";
    EXPECT_EQ(maskOf(masks, source) & kMain, kMain)
        << "and the source feeds it THROUGH that bus -- a chain of buses must not break the walk";
    EXPECT_EQ(maskOf(masks, other), 0u) << "a strip off the soloed path is still fully silenced";
}

TEST(MixerBusSoloTest, ASendThroughABusChainStaysOpenToo) {
    // The same closure, reached by a SEND rather than a main leg: source -> send 0 -> inner bus ->
    // outer bus (soloed). Only the send leg may open -- the source's dry main leg goes to Master.
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    const auto output = addFactoryNode(graph, "Audio Output");
    const auto master = addFactoryNode(graph, "Master");
    const auto source = addFactoryNode(graph, "Channel Strip");
    const auto innerBus = addFactoryNode(graph, "Channel Strip");
    const auto outerBus = addFactoryNode(graph, "Channel Strip");
    stripAt(graph, innerBus)->setIsBus(true);
    stripAt(graph, outerBus)->setIsBus(true);

    wireMainTo(graph, source, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    wireSendTo(graph, source, 0, innerBus);
    wireMainTo(graph, innerBus, outerBus, 0, kRight);
    wireMainTo(graph, outerBus, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    graph.addConnection({{master, 0}, {output, 0}});

    stripAt(graph, outerBus)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(graph);

    EXPECT_EQ(maskOf(masks, innerBus) & kMain, kMain);
    EXPECT_EQ(maskOf(masks, source), ChannelStripModule::sendLegBit(0))
        << "the send leg reaches the soloed bus through the chain; the dry main leg does not";
}

TEST(MixerBusSoloTest, SoloingASourceKeepsTheBusesItFeedsAudible) {
    SendRig rig;
    stripAt(rig.graph, rig.sourceA)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(rig.graph);

    EXPECT_EQ(maskOf(masks, rig.sourceA), ~0u);
    EXPECT_EQ(maskOf(masks, rig.bus), ~0u) << "soloing a source must not mute its own reverb tail";
    EXPECT_EQ(maskOf(masks, rig.sourceB), 0u);
}

TEST(MixerBusSoloTest, DownstreamAudibilityIsTransitiveThroughBuses) {
    SendRig rig;
    // bus -> bus2 -> Master, so the second bus is two hops downstream of the soloed source.
    const auto bus2 = addFactoryNode(rig.graph, "Channel Strip");
    stripAt(rig.graph, bus2)->setIsBus(true);
    wireMainTo(rig.graph, rig.bus, bus2, 0, kRight);
    wireMainTo(rig.graph, bus2, rig.master, MasterModule::kMixLeft, MasterModule::kMixRight);

    stripAt(rig.graph, rig.sourceA)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(maskOf(masks, bus2), ~0u) << "reachable downstream, transitively";
}

TEST(MixerBusSoloTest, ALegFeedingBothMasterAndASoloedBusStaysOpen) {
    // The documented limitation: the gate is per LEG, not per EDGE. sourceA's main leg feeds Master
    // AND the bus, so soloing the bus leaves the dry signal audible as well.
    SendRig rig;
    wireMainTo(rig.graph, rig.sourceA, rig.bus, 0, kRight);
    stripAt(rig.graph, rig.bus)->setSoloed(true);

    const auto masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(maskOf(masks, rig.sourceA) & kMain, kMain)
        << "per-leg, not per-edge: splitting this would need a delay-compensated per-edge mute";
}

TEST(MixerBusSoloTest, ANonContributingStripIsFullySilenced) {
    SendRig rig;
    stripAt(rig.graph, rig.sourceB)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(rig.graph);

    EXPECT_EQ(maskOf(masks, rig.sourceB), ~0u);
    EXPECT_EQ(maskOf(masks, rig.sourceA), 0u) << "neither its main leg nor its send reaches the soloed strip";
    EXPECT_EQ(maskOf(masks, rig.bus), 0u);
}

TEST(MixerBusSoloTest, ACycleInTheGraphDoesNotHangTheWalk) {
    SendRig rig;
    // bus's output back into sourceA's input: AudioProcessorGraph may refuse it, but the walk must
    // be cycle-safe either way (it is also reachable through a chain the graph does accept).
    rig.graph.addConnection({{rig.bus, 0}, {rig.sourceA, 0}});
    stripAt(rig.graph, rig.bus)->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(masks.size(), 3u);
}

// ============================================================================
// Publication through the engine
// ============================================================================

TEST(MixerBusSoloTest, RefreshSoloGatePublishesEveryMaskAndUnsoloRestoresThem) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    const auto master = addFactoryNode(graph, "Master");
    const auto sourceA = addFactoryNode(graph, "Channel Strip");
    const auto sourceB = addFactoryNode(graph, "Channel Strip");
    const auto bus = addFactoryNode(graph, "Channel Strip");
    for (auto strip : {sourceA, sourceB, bus})
        wireMainTo(graph, strip, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    wireSendTo(graph, sourceA, 0, bus);

    ASSERT_TRUE(engine.setChannelStripSoloed(bus, true));
    EXPECT_EQ(engine.getSoloedStripCount(), 1);
    EXPECT_EQ(stripAt(graph, sourceA)->getSoloAudibleMask(), ChannelStripModule::sendLegBit(0));
    EXPECT_EQ(stripAt(graph, sourceB)->getSoloAudibleMask(), 0u);
    EXPECT_EQ(stripAt(graph, bus)->getSoloAudibleMask(), ~0u);

    ASSERT_TRUE(engine.setChannelStripSoloed(bus, false));
    EXPECT_EQ(engine.getSoloedStripCount(), 0);
    for (auto strip : {sourceA, sourceB, bus})
        EXPECT_EQ(stripAt(graph, strip)->getSoloAudibleMask(), ~0u) << "un-soloing reopens every leg";
}

TEST(MixerBusSoloTest, UndoRedoAcrossAGraphRebuildSettlesTheMasks) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    AppUndoManager undo;
    synth::TimelineDoc doc;
    // Mirrors MainComponent: every undo/redo restore ends in the reconcile + publish seam.
    undo.setRestoreHooks({}, [&] { engine.publishTimeline(doc); });

    const auto master = addFactoryNode(graph, "Master");
    const auto source = addFactoryNode(graph, "Channel Strip");
    const auto bus = addFactoryNode(graph, "Channel Strip");
    wireMainTo(graph, source, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    wireMainTo(graph, bus, master, MasterModule::kMixLeft, MasterModule::kMixRight);
    wireSendTo(graph, source, 0, bus);
    stripAt(graph, source)->setSendPreFader(0, true);
    ASSERT_TRUE(engine.setChannelStripSoloed(bus, true));
    ASSERT_EQ(stripAt(graph, source)->getSoloAudibleMask(), ChannelStripModule::sendLegBit(0));

    // A structural change that rebuilds the graph from JSON: node ids are reassigned, and the send
    // slot + its pre/post have to come back through the strip's trusted extra state for the mask to
    // be recomputable at all.
    undo.recordStructuralChange(graph, [&] { graph.removeNode(bus); });
    engine.publishTimeline(doc);
    EXPECT_EQ(engine.getSoloedStripCount(), 0) << "the soloed bus is gone";
    EXPECT_EQ(stripAt(graph, source)->getSoloAudibleMask(), ~0u) << "and nothing is left gated";

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(engine.getSoloedStripCount(), 1);
    auto* restoredSource = stripAt(graph, source);
    ASSERT_NE(restoredSource, nullptr);
    EXPECT_TRUE(restoredSource->isSendActive(0)) << "the slot survived the rebuild";
    EXPECT_TRUE(restoredSource->isSendPreFader(0));
    EXPECT_EQ(restoredSource->getSoloAudibleMask(), ChannelStripModule::sendLegBit(0))
        << "and the mask was recomputed against the REBUILT graph's new node ids";

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(engine.getSoloedStripCount(), 0);
    EXPECT_EQ(stripAt(graph, source)->getSoloAudibleMask(), ~0u);
}

TEST(MixerBusSoloTest, MasterDirectIsStillGatedByTheGlobalCount) {
    // The per-leg mask is a ChannelStrip concept; Master's Direct input is still gated by the plain
    // "is anything soloed?" flag on the playhead, unchanged by FRO15.
    synth::TransportService transport;
    MasterModule master;
    master.setPlayHead(&transport);
    master.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(MasterModule::kNumInputs, kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(MasterModule::kMixLeft, i, 0.1f);
        buffer.setSample(MasterModule::kMixRight, i, 0.1f);
        buffer.setSample(MasterModule::kDirectLeft, i, 0.4f);
        buffer.setSample(MasterModule::kDirectRight, i, 0.4f);
    }
    transport.setMixerSoloActiveForBlock(true);
    juce::MidiBuffer midi;
    master.processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), 0.1f, 1.0e-6f) << "Mix passes, Direct is gated";
}
