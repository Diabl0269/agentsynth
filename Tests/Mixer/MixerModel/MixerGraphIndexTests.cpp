// The mixer's graph queries asked through one synth::ConnectionIndex (a whole snapshot's worth of columns sharing one
// cable scan) answer exactly as the one-call-per-question forms do, and synth::ChannelMacroIndex answers exactly as
// nearestChannelMacro does -- on a small project with two track channels, a send into a bus, a keyed Compressor and
// nested macros (docs/architecture/graph-queries.md).
#include "AudioEngine/ConnectionIndex.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "MixerModelTestFixture.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

struct SendsAndMacrosRig {
    AudioEngine engine;
    synth::TimelineDoc doc;
    synth::MacroSet macros;
    LinearChannelRigMMT a, b;
    juce::AudioProcessorGraph::Node* bus = nullptr;

    SendsAndMacrosRig() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        a = buildLinearChannelRigMMT(graph, doc, doc.addTrack(synth::TrackKind::Audio, "Drums"));
        b = buildLinearChannelRigMMT(graph, doc, doc.addTrack(synth::TrackKind::Audio, "Bass"));
        juce::String busUuid;
        bus = addPlainNodeMMT(graph, "Channel Strip", busUuid);
        synth::addSend(graph, a.strip->nodeID, bus->nodeID);                                   // a send into a bus
        synth::addSend(graph, b.strip->nodeID, synth::SendTarget{a.compressor->nodeID, true}); // a Key send

        // Drums: an outer channel macro holding the strip, with an inner group holding the EQ.
        synth::Macro outer;
        outer.id = "outer";
        outer.name = "Drums";
        outer.members = {uuidOf(a.strip), uuidOf(a.compressor)};
        macros.add(outer);
        synth::Macro inner;
        inner.id = "inner";
        inner.name = "Tone";
        inner.members = {uuidOf(a.eq)};
        macros.add(inner);
        macros.setParent("inner", "outer");
        synth::Macro plain; // a group with no strip: never a channel
        plain.id = "plain";
        plain.members = {uuidOf(b.eq)};
        macros.add(plain);
    }
    static juce::String uuidOf(juce::AudioProcessorGraph::Node* node) { return node->properties["uuid"].toString(); }
};

} // namespace

TEST(MixerGraphIndexTest, SignalEdgeAndPortFollowAgreeWithTheConnectionListForms) {
    SendsAndMacrosRig rig;
    auto& graph = rig.engine.getGraph();
    const auto connections = graph.getConnections();
    const synth::ConnectionIndex cables(graph);
    ASSERT_FALSE(connections.empty());
    for (const auto& c : connections) {
        EXPECT_EQ(synth::isSignalEdge(graph, cables, c), synth::isSignalEdge(graph, connections, c));
        const auto viaIndex = synth::resolveThroughPorts(graph, cables, c.destination);
        const auto viaList = synth::resolveThroughPorts(graph, connections, c.destination);
        EXPECT_EQ(viaIndex.nodeID, viaList.nodeID);
        EXPECT_EQ(viaIndex.channelIndex, viaList.channelIndex);
    }
}

TEST(MixerGraphIndexTest, StripFeedersAndSendTargetsAgreeWithTheOneShotForms) {
    SendsAndMacrosRig rig;
    auto& graph = rig.engine.getGraph();
    const synth::ConnectionIndex cables(graph);
    for (auto* strip : {rig.a.strip, rig.b.strip, rig.bus}) {
        EXPECT_EQ(synth::findStripsFeedingStrip(graph, cables, strip->nodeID),
                  synth::findStripsFeedingStrip(graph, strip->nodeID));
        for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot)
            EXPECT_EQ(synth::resolveSendTarget(graph, cables, strip->nodeID, slot),
                      synth::resolveSendTarget(graph, strip->nodeID, slot));
    }
    EXPECT_EQ(synth::findStripsFeedingStrip(graph, cables, rig.bus->nodeID), std::vector<NodeID>{rig.a.strip->nodeID});
    EXPECT_EQ(synth::resolveSendTarget(graph, cables, rig.b.strip->nodeID, 0),
              (synth::SendTarget{rig.a.compressor->nodeID, true}));
}

TEST(MixerGraphIndexTest, ChannelMacroIndexAgreesWithNearestChannelMacroForEveryNode) {
    SendsAndMacrosRig rig;
    auto& graph = rig.engine.getGraph();
    const synth::ChannelMacroIndex index(graph, rig.macros);
    for (auto* node : graph.getNodes()) {
        const auto uuid = node->properties["uuid"].toString();
        EXPECT_EQ(index.nearest(uuid), synth::nearestChannelMacro(graph, rig.macros, uuid)) << uuid;
    }
    EXPECT_EQ(index.nearest(SendsAndMacrosRig::uuidOf(rig.a.eq))->id, "outer") << "an inner group resolves outward";
    EXPECT_EQ(index.nearest(SendsAndMacrosRig::uuidOf(rig.b.eq))->id, "plain") << "no channel: the innermost owner";
    EXPECT_EQ(index.nearest(""), nullptr);
}

TEST(MixerGraphIndexTest, TheSnapshotReadsTheRigAsBefore) {
    SendsAndMacrosRig rig;
    const auto snapshot = synth::buildMixerSnapshot(rig.engine.getGraph(), rig.doc, rig.macros);
    std::vector<juce::String> names;
    for (const auto& column : snapshot.columns)
        names.push_back(column.name);
    ASSERT_GE(names.size(), 3u);
    EXPECT_EQ(names[0], "Drums") << "the channel macro names its column";
    EXPECT_EQ(names[1], "Bass");
    EXPECT_EQ(names[2], "Bus 1");
    EXPECT_EQ(snapshot.columns[2].kind, synth::MixerColumn::Kind::Bus);
    ASSERT_EQ(snapshot.columns[0].inserts.size(), 2u);
    ASSERT_EQ(snapshot.columns[0].sends.size(), 1u);
    EXPECT_EQ(snapshot.columns[0].sends[0].targetName, "Bus 1");
    ASSERT_EQ(snapshot.columns[1].sends.size(), 1u);
    EXPECT_TRUE(snapshot.columns[1].sends[0].keyTarget);
    EXPECT_TRUE(snapshot.columns[1].sends[0].targetName.startsWith("Key: ")) << snapshot.columns[1].sends[0].targetName;
}
