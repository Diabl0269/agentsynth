// MixerModelBusColumnTests.cpp -- FRO15 (P9-9, docs/mixer.md §5.15): how buildMixerSnapshot sees
// buses and sends. Headless, same bare AudioEngine/TimelineDoc/MacroSet rig as the rest of the
// MixerModel suite.
#include "Mixer/MasterSplice.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "MixerModelTestFixture.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

ChannelStripModule* stripModule(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

const synth::MixerColumn* columnFor(const synth::MixerSnapshot& snapshot, NodeID id) {
    for (const auto& column : snapshot.columns)
        if (column.nodeId == id)
            return &column;
    return nullptr;
}

} // namespace

TEST(MixerModelBusColumnTests, BusColumnIsKindBusAndListsItsSourceStrips) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto trackB = doc.addTrack(synth::TrackKind::Audio, "Bass");
    const auto rigA = buildLinearChannelRigMMT(graph, doc, trackA);
    const auto rigB = buildLinearChannelRigMMT(graph, doc, trackB);
    juce::String busUuid;
    auto* bus = addPlainNodeMMT(graph, "Channel Strip", busUuid);
    ASSERT_NE(bus, nullptr);
    // A group bus: both tracks' strips feed it through their main legs.
    connectStereoMMT(graph, *rigA.strip, *bus);
    connectStereoMMT(graph, *rigB.strip, *bus);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 3u);
    // Track-driven strips first, then the bus (the orphan-strip append already puts it there).
    EXPECT_EQ(snapshot.columns[0].kind, synth::MixerColumn::Kind::Strip);
    EXPECT_EQ(snapshot.columns[1].kind, synth::MixerColumn::Kind::Strip);

    const auto& busColumn = snapshot.columns[2];
    EXPECT_EQ(busColumn.kind, synth::MixerColumn::Kind::Bus)
        << "recognised structurally, with no isBus flag written anywhere";
    EXPECT_EQ(busColumn.nodeId, bus->nodeID);
    EXPECT_EQ(busColumn.name, "Bus 1") << "a bus has no feeding track to name it";
    EXPECT_FALSE(busColumn.linkedToTrack);
    ASSERT_EQ(busColumn.busSources.size(), 2u);
    EXPECT_EQ(busColumn.busSources[0], "Drums");
    EXPECT_EQ(busColumn.busSources[1], "Bass");
    EXPECT_TRUE(busColumn.feedingTracks.empty()) << "no track chip on a bus column";
}

TEST(MixerModelBusColumnTests, AnEmptyNewBusStillClassifiesAsBus) {
    // A freshly added bus has no predecessors at all, so only the trusted flag can classify it --
    // which is exactly why the flag exists alongside the structural fallback.
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    juce::String uuid;
    auto* bus = addPlainNodeMMT(graph, "Channel Strip", uuid);
    ASSERT_NE(stripModule(bus), nullptr);

    EXPECT_EQ(synth::buildMixerSnapshot(graph, doc, macros).columns[0].kind, synth::MixerColumn::Kind::Strip)
        << "an unfed, unflagged strip is just an orphan channel";

    stripModule(bus)->setIsBus(true);
    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u);
    EXPECT_EQ(snapshot.columns[0].kind, synth::MixerColumn::Kind::Bus);
    EXPECT_EQ(snapshot.columns[0].name, "Bus 1");
    EXPECT_TRUE(snapshot.columns[0].busSources.empty());
}

TEST(MixerModelBusColumnTests, SendListMirrorsTheStripsActiveSlots) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Vocals");
    const auto rig = buildLinearChannelRigMMT(graph, doc, track);
    juce::String busUuid;
    auto* bus = addPlainNodeMMT(graph, "Channel Strip", busUuid);
    ASSERT_NE(stripModule(bus), nullptr);
    stripModule(bus)->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, rig.strip->nodeID, bus->nodeID), 0);
    ASSERT_EQ(synth::addSend(graph, rig.strip->nodeID, bus->nodeID), 1);
    ASSERT_TRUE(synth::removeSend(graph, rig.strip->nodeID, 0));
    stripModule(rig.strip)->setSendPreFader(1, true);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    const auto* column = columnFor(snapshot, rig.strip->nodeID);
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->sends.size(), 1u) << "the removed slot is gone, the higher one stays";
    EXPECT_EQ(column->sends[0].slot, 1) << "and keeps its own slot index, so it still names send2Level";
    EXPECT_TRUE(column->sends[0].preFader);
    EXPECT_EQ(column->sends[0].targetNodeId, bus->nodeID);
    EXPECT_EQ(column->sends[0].targetName, "Bus 1");

    // Cutting the cable on the canvas leaves the slot but not the target.
    graph.removeConnection({{rig.strip->nodeID, ChannelStripModule::sendLeftChannel(1)}, {bus->nodeID, 0}});
    const auto cut = synth::buildMixerSnapshot(graph, doc, macros);
    const auto* cutColumn = columnFor(cut, rig.strip->nodeID);
    ASSERT_NE(cutColumn, nullptr);
    ASSERT_EQ(cutColumn->sends.size(), 1u);
    EXPECT_EQ(cutColumn->sends[0].targetNodeId, NodeID{});
    EXPECT_EQ(cutColumn->sends[0].targetName, "No target");
}

TEST(MixerModelBusColumnTests, ABoxedBusTakesItsMacroName) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    juce::String busUuid;
    auto* bus = addPlainNodeMMT(graph, "Channel Strip", busUuid);
    ASSERT_NE(stripModule(bus), nullptr);
    stripModule(bus)->setIsBus(true);
    synth::Macro macro;
    macro.name = "Reverb Bus";
    macro.members = {busUuid};
    macros.add(macro);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u);
    EXPECT_EQ(snapshot.columns[0].kind, synth::MixerColumn::Kind::Bus);
    EXPECT_EQ(snapshot.columns[0].name, "Reverb Bus") << "the macro name wins over the Bus N fallback";
}
