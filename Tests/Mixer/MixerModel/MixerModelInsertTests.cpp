// MixerModelInsertTests.cpp -- FRO11 (P9-5, docs/mixer.md §5.6): a column's insert list (linear
// vs. branching, in signal order) and the three insert-list mutation primitives. Headless: a bare
// AudioEngine/TimelineDoc/MacroSet, no MainComponent/GraphEditor.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"
#include <algorithm>

TEST(MixerModelInsertTests, LinearEqCompressorChainReportsLinearInInsertOrder) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rig = buildLinearChannelRigMMT(graph, doc, track);
    ASSERT_NE(rig.strip, nullptr);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u);
    const auto& column = snapshot.columns[0];
    EXPECT_TRUE(column.insertChainIsLinear);
    ASSERT_EQ(column.inserts.size(), 2u);
    EXPECT_EQ(column.inserts[0].name, "Parametric EQ");
    EXPECT_EQ(column.inserts[1].name, "Compressor");
    EXPECT_TRUE(column.editOnCanvasTargetUuid.isEmpty()) << "only set for a branching chain";
}

TEST(MixerModelInsertTests, ASharedEqFeedingTwoStripsMakesTheChainBranching) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Drums");
    juce::String trackAudioUuid, unused, strip2Uuid;
    auto* trackAudio = addPlainNodeMMT(graph, "Track Audio", trackAudioUuid);
    auto* eq = addPlainNodeMMT(graph, "Parametric EQ", unused);
    auto* compressor1 = addPlainNodeMMT(graph, "Compressor", unused);
    auto* compressor2 = addPlainNodeMMT(graph, "Compressor", unused);
    juce::String stripUuid;
    auto* strip1 = addPlainNodeMMT(graph, "Channel Strip", stripUuid);
    auto* strip2 = addPlainNodeMMT(graph, "Channel Strip", strip2Uuid);
    ASSERT_NE(trackAudio, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(compressor1, nullptr);
    ASSERT_NE(compressor2, nullptr);
    ASSERT_NE(strip1, nullptr);
    ASSERT_NE(strip2, nullptr);
    if (auto* m = dynamic_cast<ChannelStripModule*>(strip1->getProcessor()))
        m->setShape(ChannelStripModule::Shape::Stereo);
    if (auto* m = dynamic_cast<ChannelStripModule*>(strip2->getProcessor()))
        m->setShape(ChannelStripModule::Shape::Stereo);

    connectStereoMMT(graph, *trackAudio, *eq);
    // eq fans out to TWO independent chains -- a shared insert, per §5.6.
    connectStereoMMT(graph, *eq, *compressor1);
    connectStereoMMT(graph, *compressor1, *strip1);
    connectStereoMMT(graph, *eq, *compressor2);
    connectStereoMMT(graph, *compressor2, *strip2);
    doc.setTrackBinding(track, trackAudioUuid);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    // strip1 is reached by the one bound track; strip2 is never reached by ANY track's own walk
    // (only strip1 is), so it appears too, as its own orphan column (§8 item 4's own ordering
    // rule) -- this test only cares about strip1's insert list.
    ASSERT_EQ(snapshot.columns.size(), 2u);
    const auto it = std::find_if(snapshot.columns.begin(), snapshot.columns.end(),
                                 [&](const auto& c) { return c.nodeId == strip1->nodeID; });
    ASSERT_NE(it, snapshot.columns.end());
    EXPECT_FALSE(it->insertChainIsLinear) << "eq feeds two chains -- a fan-out, not a straight line";
    EXPECT_EQ(it->editOnCanvasTargetUuid, eq->properties["uuid"].toString())
        << "eq is unboxed, so Edit on canvas targets the node itself";
}

TEST(MixerModelInsertTests, EditOnCanvasTargetIsTheOwningMacroWhenTheBranchingNodeIsBoxed) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Drums");
    juce::String trackAudioUuid, eqUuid, unused, strip2Uuid, stripUuid;
    auto* trackAudio = addPlainNodeMMT(graph, "Track Audio", trackAudioUuid);
    auto* eq = addPlainNodeMMT(graph, "Parametric EQ", eqUuid);
    auto* compressor1 = addPlainNodeMMT(graph, "Compressor", unused);
    auto* compressor2 = addPlainNodeMMT(graph, "Compressor", unused);
    auto* strip1 = addPlainNodeMMT(graph, "Channel Strip", stripUuid);
    auto* strip2 = addPlainNodeMMT(graph, "Channel Strip", strip2Uuid);
    ASSERT_NE(trackAudio, nullptr);
    ASSERT_NE(eq, nullptr);
    if (auto* m = dynamic_cast<ChannelStripModule*>(strip1->getProcessor()))
        m->setShape(ChannelStripModule::Shape::Stereo);
    if (auto* m = dynamic_cast<ChannelStripModule*>(strip2->getProcessor()))
        m->setShape(ChannelStripModule::Shape::Stereo);

    connectStereoMMT(graph, *trackAudio, *eq);
    connectStereoMMT(graph, *eq, *compressor1);
    connectStereoMMT(graph, *compressor1, *strip1);
    connectStereoMMT(graph, *eq, *compressor2);
    connectStereoMMT(graph, *compressor2, *strip2);
    doc.setTrackBinding(track, trackAudioUuid);

    synth::Macro macro;
    macro.name = "Shared EQ Bus";
    macro.members.push_back(eqUuid);
    macros.add(macro);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 2u); // strip1 (track-driven) + strip2 (orphan) -- see the
                                            // sibling test above for why both appear.
    const auto it = std::find_if(snapshot.columns.begin(), snapshot.columns.end(),
                                 [&](const auto& c) { return c.nodeId == strip1->nodeID; });
    ASSERT_NE(it, snapshot.columns.end());
    EXPECT_FALSE(it->insertChainIsLinear);
    EXPECT_EQ(it->editOnCanvasTargetUuid, macros.getAll().front().id);
}

TEST(MixerModelInsertTests, SpliceOutInsertBridgesPredecessorDirectlyToSuccessor) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rig = buildLinearChannelRigMMT(graph, doc, track);
    ASSERT_NE(rig.eq, nullptr);

    ASSERT_TRUE(synth::spliceOutInsert(graph, rig.eq->nodeID));

    const auto connections = graph.getConnections();
    const bool eqStillWired = std::any_of(connections.begin(), connections.end(), [&](const auto& c) {
        return c.source.nodeID == rig.eq->nodeID || c.destination.nodeID == rig.eq->nodeID;
    });
    EXPECT_FALSE(eqStillWired) << "the removed node must end up with no signal connections at all";

    auto* trackModule = dynamic_cast<ModuleBase*>(rig.trackAudio->getProcessor());
    auto* compressorModule = dynamic_cast<ModuleBase*>(rig.compressor->getProcessor());
    const bool bridged = std::any_of(connections.begin(), connections.end(), [&](const auto& c) {
        return c.source.nodeID == rig.trackAudio->nodeID && c.destination.nodeID == rig.compressor->nodeID &&
               c.source.channelIndex == 0 && c.destination.channelIndex == 0;
    });
    EXPECT_TRUE(bridged) << "Track Audio must now feed Compressor directly on the left leg";
    juce::ignoreUnused(trackModule, compressorModule);
}

TEST(MixerModelInsertTests, ReorderInsertMovesANodeToANewPositionInTheChain) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rig = buildLinearChannelRigMMT(graph, doc, track);
    ASSERT_NE(rig.eq, nullptr);
    ASSERT_NE(rig.compressor, nullptr);
    ASSERT_NE(rig.strip, nullptr);

    // trackAudio -> eq -> compressor -> strip  becomes  trackAudio -> compressor -> eq -> strip.
    ASSERT_TRUE(synth::reorderInsert(graph, rig.eq->nodeID, rig.compressor->nodeID, rig.strip->nodeID));

    synth::MacroSet macros;
    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u);
    const auto& column = snapshot.columns[0];
    EXPECT_TRUE(column.insertChainIsLinear);
    ASSERT_EQ(column.inserts.size(), 2u);
    EXPECT_EQ(column.inserts[0].name, "Compressor");
    EXPECT_EQ(column.inserts[1].name, "Parametric EQ");
}
