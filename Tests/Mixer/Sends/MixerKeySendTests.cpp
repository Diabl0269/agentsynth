// MixerKeySendTests.cpp (docs/mixer/sends-and-buses.md#sending-to-a-key-input): a send whose
// target is a Compressor/Gate Key input rather than a strip. Headless: a bare graph (plus one
// AudioEngine for the publication test), no rendering -- MixerKeySendDuckingTests.cpp hears it.
//
//   * resolution  -- resolveSendTarget names a Key target for a slot wired onto a Key input and a
//                    strip target otherwise; findSendTarget stays strip-only
//   * wiring      -- addSend onto a Key target lands on the module's own Key L/R channels, and the
//                    engine's graph-change listener then reports the module keyed
//   * cycles      -- a Key target whose module output reaches the source is refused, including when
//                    the path back runs through ANOTHER key edge; the same all-edges rule now guards
//                    strip targets too
//   * retarget / reorder -- strip <-> Key retargets, swapSends/moveSendRow with a Key send and a bus
//   * labels      -- "Send to Key: Compressor 1 on Channel" / "Key: Compressor 1" with no channel
//   * solo        -- soloing the keyed channel keeps the Key send open and the kick's main leg shut
//   * stems       -- a Key send adds no stem and never makes the keyed channel a bus

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SidechainConnections.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Mixer/SoloAudibleSet.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/GateModule.h"
#include "Modules/MasterModule.h"
#include "Transport/StemSession.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::SendTarget;
constexpr int kRight = ChannelStripModule::kRightBase;

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

ModuleBase* moduleAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
}

bool keySendIsWired(juce::AudioProcessorGraph& graph, NodeID source, int slot, NodeID module) {
    return graph.isConnected(
               {{source, ChannelStripModule::sendLeftChannel(slot)}, {module, CompressorModule::kKeyBase}}) &&
           graph.isConnected(
               {{source, ChannelStripModule::sendRightChannel(slot)}, {module, CompressorModule::kKeyBase + 1}});
}

bool contains(const std::vector<NodeID>& ids, NodeID id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

/** kick strip ─► Master; bass osc ─► Compressor ─► bass strip ─► Master; plus an empty bus. The
 *  kick sends nothing yet -- each test wires the send it is about. */
struct KeyRig {
    juce::AudioProcessorGraph graph;
    NodeID output, master, kick, bassOsc, bassComp, bass, bus;

    KeyRig() {
        graph.setPlayConfigDetails(2, 2, 48000.0, 64);
        output = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        kick = addFactoryNode(graph, "Channel Strip");
        bassOsc = addFactoryNode(graph, "Oscillator");
        bassComp = addFactoryNode(graph, "Compressor");
        bass = addFactoryNode(graph, "Channel Strip");
        bus = addFactoryNode(graph, "Channel Strip");
        stripAt(graph, bus)->setIsBus(true);
        moduleAt(graph, bassComp)->setModuleName("Compressor 1");

        graph.addConnection({{bassOsc, 0}, {bassComp, 0}});
        graph.addConnection({{bassOsc, 0}, {bassComp, 1}});
        graph.addConnection({{bassComp, 0}, {bass, 0}});
        graph.addConnection({{bassComp, 1}, {bass, kRight}});
        for (auto strip : {kick, bass, bus}) {
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {output, 0}});
        graph.addConnection({{master, 1}, {output, 1}});
    }
};

} // namespace

TEST(MixerKeySendTest, ResolvesAKeyTargetForASlotOnAKeyInputAndAStripTargetOtherwise) {
    KeyRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, rig.bus), 1);

    EXPECT_TRUE(keySendIsWired(rig.graph, rig.kick, 0, rig.bassComp)) << "onto the module's own Key L/R";
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 0), (SendTarget{rig.bassComp, true}));
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.kick, 0), NodeID{})
        << "the strip-only view never reports a Key send as the bass strip behind it";
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 1), (SendTarget{rig.bus, false}));
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.kick, 1), rig.bus);
    EXPECT_FALSE(synth::isBusStrip(rig.graph, rig.bass)) << "a Key send never makes the keyed channel a bus";
}

TEST(MixerKeySendTest, AddingAKeySendKeysTheModuleOnceTheEngineRepublishes) {
    // The engine republishes on its graph's own (async) change broadcast -- pump like a live app.
    AudioEngine engine;
    auto& graph = engine.getGraph();
    const auto kick = addFactoryNode(graph, "Channel Strip");
    const auto gateId = addFactoryNode(graph, "Gate");
    auto* gate = moduleAt(graph, gateId);
    ASSERT_NE(gate, nullptr);
    const auto pumpUntil = [&](bool expected) {
        for (int i = 0; i < 100 && gate->isSidechainConnected() != expected; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        return gate->isSidechainConnected() == expected;
    };

    ASSERT_EQ(synth::addSend(graph, kick, SendTarget{gateId, true}), 0);
    EXPECT_TRUE(graph.isConnected({{kick, ChannelStripModule::sendLeftChannel(0)}, {gateId, GateModule::kKeyBase}}));
    EXPECT_TRUE(pumpUntil(true)) << "the Gate listens to its Key once the send exists";

    ASSERT_TRUE(synth::removeSend(graph, kick, 0));
    EXPECT_TRUE(pumpUntil(false)) << "removing the send returns it to self-detection";
}

TEST(MixerKeySendTest, KeyTargetsAreOnlyModulesWithAKeyInputAndNeverACycle) {
    KeyRig rig;
    // The kick's own chain: kick comp -> kick strip. Keying it from the kick is a render cycle.
    const auto kickComp = addFactoryNode(rig.graph, "Compressor");
    ASSERT_TRUE(rig.graph.addConnection({{kickComp, 0}, {rig.kick, 0}}));

    const auto targets = synth::enumerateKeySendTargets(rig.graph, rig.kick);
    EXPECT_TRUE(contains(targets, rig.bassComp));
    EXPECT_FALSE(contains(targets, kickComp)) << "its output reaches the kick itself";
    for (auto notKeyable : {rig.bassOsc, rig.bass, rig.bus, rig.master, rig.kick})
        EXPECT_FALSE(contains(targets, notKeyable));
    EXPECT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{kickComp, true}), -1);
    EXPECT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bass, true}), -1) << "a strip has no Key input";
    EXPECT_EQ(stripAt(rig.graph, rig.kick)->getActiveSendCount(), 0) << "a refusal changes nothing";
}

TEST(MixerKeySendTest, ACycleThroughAnotherKeyEdgeIsStillRefused) {
    KeyRig rig;
    // gate's output KEYS the kick's own compressor: gate -(key)-> kick comp -> kick strip. Keying the
    // gate from the kick closes a render loop even though no signal edge joins them.
    const auto kickComp = addFactoryNode(rig.graph, "Compressor");
    const auto gate = addFactoryNode(rig.graph, "Gate");
    ASSERT_TRUE(rig.graph.addConnection({{kickComp, 0}, {rig.kick, 0}}));
    ASSERT_TRUE(rig.graph.addConnection({{gate, 0}, {kickComp, CompressorModule::kKeyBase}}));

    EXPECT_FALSE(contains(synth::enumerateKeySendTargets(rig.graph, rig.kick), gate));
    EXPECT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{gate, true}), -1);

    // The same all-edges rule guards a STRIP target: the bus keys the kick's compressor, so sending
    // the kick into that bus would loop kick -> bus -(key)-> kick comp -> kick.
    ASSERT_TRUE(rig.graph.addConnection({{rig.bus, 0}, {kickComp, CompressorModule::kKeyBase + 1}}));
    EXPECT_FALSE(contains(synth::enumerateSendTargets(rig.graph, rig.kick), rig.bus));
    EXPECT_EQ(synth::addSend(rig.graph, rig.kick, rig.bus), -1);
}

TEST(MixerKeySendTest, RetargetMovesBetweenAStripAndAKeyTarget) {
    KeyRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);

    ASSERT_TRUE(synth::retargetSend(rig.graph, rig.kick, 0, rig.bus));
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 0), (SendTarget{rig.bus, false}));
    EXPECT_FALSE(rig.graph.isConnected(
        {{rig.kick, ChannelStripModule::sendLeftChannel(0)}, {rig.bassComp, CompressorModule::kKeyBase}}))
        << "the Key cables went with the retarget";

    ASSERT_TRUE(synth::retargetSend(rig.graph, rig.kick, 0, SendTarget{rig.bassComp, true}));
    EXPECT_TRUE(keySendIsWired(rig.graph, rig.kick, 0, rig.bassComp));
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.kick, 0), NodeID{});

    // Key -> the bass strip itself is a real move, not "already there" (same node chain, other kind).
    ASSERT_TRUE(synth::retargetSend(rig.graph, rig.kick, 0, rig.bass));
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 0), (SendTarget{rig.bass, false}));
}

TEST(MixerKeySendTest, SwapSendsKeepsAKeySendAndABusSendWorking) {
    KeyRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, rig.bus), 1);

    ASSERT_TRUE(synth::swapSends(rig.graph, rig.kick, 0, 1));
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 0), (SendTarget{rig.bus, false}));
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 1), (SendTarget{rig.bassComp, true}));
    EXPECT_TRUE(keySendIsWired(rig.graph, rig.kick, 1, rig.bassComp));

    ASSERT_TRUE(synth::moveSendRow(rig.graph, rig.kick, 1, 0));
    EXPECT_EQ(synth::resolveSendTarget(rig.graph, rig.kick, 0), (SendTarget{rig.bassComp, true}));
    EXPECT_TRUE(keySendIsWired(rig.graph, rig.kick, 0, rig.bassComp));
    EXPECT_EQ(synth::findSendTarget(rig.graph, rig.kick, 1), rig.bus);

    synth::publishSidechainConnections(rig.graph);
    EXPECT_TRUE(moduleAt(rig.graph, rig.bassComp)->isSidechainConnected()) << "still keyed after the reorder";
}

TEST(MixerKeySendTest, LabelsNameTheKeyedModuleAndItsChannel) {
    KeyRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);

    EXPECT_EQ(synth::findKeyTargetChannel(rig.graph, rig.bassComp), rig.bass);
    const auto channel = synth::sendTargetName(rig.graph, nullptr, rig.bass);
    EXPECT_EQ(synth::sendTargetName(rig.graph, nullptr, SendTarget{rig.bassComp, true}),
              "Key: Compressor 1 on " + channel);
    EXPECT_EQ(synth::describeSendSlotLabel(rig.graph, nullptr, rig.kick, 0), "Send to Key: Compressor 1 on " + channel);

    // A custom card title wins, and a module that reaches no channel names only itself.
    rig.graph.getNodeForId(rig.bassComp)->properties.set("displayName", "Duck");
    rig.graph.removeConnection({{rig.bassComp, 0}, {rig.bass, 0}});
    rig.graph.removeConnection({{rig.bassComp, 1}, {rig.bass, kRight}});
    EXPECT_EQ(synth::describeSendSlotLabel(rig.graph, nullptr, rig.kick, 0), "Send to Key: Duck");
}

TEST(MixerKeySendTest, SoloingTheKeyedChannelKeepsTheKeySendOpenAndTheKickMainShut) {
    KeyRig rig;
    stripAt(rig.graph, rig.bass)->setSoloed(true);
    // Control first: with no Key send the kick contributes nothing to the soloed bass.
    EXPECT_EQ(synth::computeSoloAudibleLegs(rig.graph)[rig.kick], 0u);

    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);
    auto masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(masks[rig.bass], ~0u);
    EXPECT_EQ(masks[rig.kick], ChannelStripModule::sendLegBit(0))
        << "the bass keeps ducking under solo, but the kick itself is not heard";

    // Soloing something the keyed channel does not feed closes the Key send again.
    stripAt(rig.graph, rig.bass)->setSoloed(false);
    stripAt(rig.graph, rig.bus)->setSoloed(true);
    masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(masks[rig.kick], 0u);
}

TEST(MixerKeySendTest, SoloingTheKickNeverMakesTheKeyedChannelAudible) {
    KeyRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);
    stripAt(rig.graph, rig.kick)->setSoloed(true);
    auto masks = synth::computeSoloAudibleLegs(rig.graph);
    EXPECT_EQ(masks[rig.kick], ~0u);
    EXPECT_EQ(masks[rig.bass], 0u) << "a Key edge is not a signal path downstream of the solo";
}

TEST(MixerKeySendTest, AKeySendAddsNoStemAndLeavesStemNamingAlone) {
    KeyRig rig;
    const auto before = synth::collectStemStrips(rig.graph);
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, SendTarget{rig.bassComp, true}), 0);
    const auto after = synth::collectStemStrips(rig.graph);

    ASSERT_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size(); ++i)
        EXPECT_EQ(before[i].nodeId, after[i].nodeId);
    EXPECT_FALSE(synth::isBusStrip(rig.graph, rig.bass)) << "so its stem is never renamed \"Bus N\"";
}
