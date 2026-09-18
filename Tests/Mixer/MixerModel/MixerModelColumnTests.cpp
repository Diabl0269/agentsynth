// MixerModelColumnTests.cpp -- FRO11 (P9-5, docs/mixer/panel.md#what-the-mixer-shows): buildMixerSnapshot's column
// enumeration. Headless: a bare AudioEngine/TimelineDoc/MacroSet, no MainComponent/GraphEditor.
#include "Mixer/MasterSplice.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"

TEST(MixerModelColumnTests, ColumnSetIsStripsInTrackOrderThenDirectThenMaster) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto trackB = doc.addTrack(synth::TrackKind::Audio, "Bass");
    const auto rigA = buildLinearChannelRigMMT(graph, doc, trackA);
    const auto rigB = buildLinearChannelRigMMT(graph, doc, trackB);
    ASSERT_NE(rigA.strip, nullptr);
    ASSERT_NE(rigB.strip, nullptr);

    // No Master spliced -- Direct/Master must both be absent.
    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 2u);
    EXPECT_EQ(snapshot.columns[0].nodeId, rigA.strip->nodeID) << "Drums is the first track -> first column";
    EXPECT_EQ(snapshot.columns[1].nodeId, rigB.strip->nodeID);
    EXPECT_FALSE(snapshot.hasDirect);
    EXPECT_FALSE(snapshot.hasMaster);
}

TEST(MixerModelColumnTests, MasterSpliceAddsDirectThenMasterAfterEveryStrip) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    juce::String outputUuid;
    auto* output = addPlainNodeMMT(graph, "Audio Output", outputUuid);
    ASSERT_NE(output, nullptr);

    const auto trackA = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rigA = buildLinearChannelRigMMT(graph, doc, trackA);
    ASSERT_NE(rigA.strip, nullptr);
    auto* master = synth::spliceMasterNode(graph, {900, 0});
    ASSERT_NE(master, nullptr);
    // Strip output (ch0/kRightBase) -> Master's Mix input (ch0/ch1) -- Master's own channel
    // numbering, not the generic stereo-pair helper (Source/Modules/MasterModule.h).
    graph.addConnection({{rigA.strip->nodeID, 0}, {master->nodeID, 0}});
    graph.addConnection({{rigA.strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, 1}});

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 3u);
    EXPECT_EQ(snapshot.columns[0].nodeId, rigA.strip->nodeID);
    EXPECT_EQ(snapshot.columns[1].kind, synth::MixerColumn::Kind::Direct);
    EXPECT_EQ(snapshot.columns[2].kind, synth::MixerColumn::Kind::Master);
    EXPECT_EQ(snapshot.columns[2].nodeId, master->nodeID);
    EXPECT_TRUE(snapshot.hasDirect);
    EXPECT_TRUE(snapshot.hasMaster);
}

TEST(MixerModelColumnTests, TwoTracksSharingOneChannelProduceOneColumnListingBothTracks) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "Kick");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "Snare");
    juce::String trackInAUuid, trackInBUuid, unused;
    auto* trackInA = addPlainNodeMMT(graph, "Track In", trackInAUuid);
    auto* trackInB = addPlainNodeMMT(graph, "Track In", trackInBUuid);
    auto* instrument = addPlainNodeMMT(graph, "Sampler", unused);
    juce::String stripUuid;
    auto* strip = addPlainNodeMMT(graph, "Channel Strip", stripUuid);
    ASSERT_NE(trackInA, nullptr);
    ASSERT_NE(trackInB, nullptr);
    ASSERT_NE(instrument, nullptr);
    ASSERT_NE(strip, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(strip->getProcessor()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);

    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    graph.addConnection({{trackInA->nodeID, midi}, {instrument->nodeID, midi}});
    graph.addConnection({{trackInB->nodeID, midi}, {instrument->nodeID, midi}});
    connectStereoMMT(graph, *instrument, *strip);

    doc.setTrackBinding(trackA, trackInAUuid);
    doc.setTrackBinding(trackB, trackInBUuid);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u) << "one shared channel must produce exactly one column";
    EXPECT_EQ(snapshot.columns[0].nodeId, strip->nodeID);
    ASSERT_EQ(snapshot.columns[0].feedingTracks.size(), 2u);
    EXPECT_EQ(snapshot.columns[0].feedingTracks[0], trackA);
    EXPECT_EQ(snapshot.columns[0].feedingTracks[1], trackB);
    EXPECT_FALSE(snapshot.columns[0].linkedToTrack) << "two feeders is shared, not the docs/mixer/mixer.md#channels-follow-audio-not-tracks link";
}

TEST(MixerModelColumnTests, AnOrphanStripWithNoTrackBindingStillAppearsAppendedByNodeId) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rigA = buildLinearChannelRigMMT(graph, doc, trackA);
    ASSERT_NE(rigA.strip, nullptr);

    // A hand-built strip with nothing feeding it from the timeline at all.
    juce::String orphanUuid;
    auto* orphan = addPlainNodeMMT(graph, "Channel Strip", orphanUuid);
    ASSERT_NE(orphan, nullptr);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 2u);
    EXPECT_EQ(snapshot.columns[0].nodeId, rigA.strip->nodeID) << "the track-driven strip stays first";
    EXPECT_EQ(snapshot.columns[1].nodeId, orphan->nodeID);
    EXPECT_TRUE(snapshot.columns[1].feedingTracks.empty());
}

TEST(MixerModelColumnTests, NoMasterMeansNoDirectAndNoMasterColumn) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    EXPECT_TRUE(snapshot.columns.empty());
    EXPECT_FALSE(snapshot.hasDirect);
    EXPECT_FALSE(snapshot.hasMaster);
}

TEST(MixerModelColumnTests, ColumnNameAndColourComeFromTheOwningMacroWhenBoxed) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rigA = buildLinearChannelRigMMT(graph, doc, trackA);
    ASSERT_NE(rigA.strip, nullptr);

    synth::Macro macro;
    macro.name = "Drum Bus";
    macro.colour = juce::Colour(0xffaa00aa);
    macro.members.push_back(rigA.strip->properties["uuid"].toString());
    macros.add(macro);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u);
    EXPECT_EQ(snapshot.columns[0].name, "Drum Bus");
    EXPECT_EQ(snapshot.columns[0].colour, juce::Colour(0xffaa00aa));
}
