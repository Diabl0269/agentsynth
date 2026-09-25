// MixerModelMasterInsertTests.cpp -- FRO148 (docs/mixer/mixer.md#master-inserts): Master's own insert list. Master's
// inserts sit AFTER its fader, so the chain is walked FORWARD from Master's output to the Rec Tap / Audio Output
// terminator (`chainEndNodeId`). Headless: a bare AudioEngine/TimelineDoc/MacroSet, no MainComponent/GraphEditor.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"

namespace {

struct MasterInsertFixture {
    AudioEngine engine;
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    MasterInsertFixture() { engine.getGraph().setPlayConfigDetails(0, 2, 44100.0, 512); }
    auto& graph() { return engine.getGraph(); }
    synth::MixerSnapshot snapshot() { return synth::buildMixerSnapshot(graph(), doc, macros); }
};

} // namespace

TEST(MixerModelMasterInsertTests, MasterWithNoInsertsIsLinearAndEndsAtAudioOutput) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/false);
    ASSERT_NE(rig.master, nullptr);

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    EXPECT_TRUE(column->inserts.empty());
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, rig.output->nodeID);
    EXPECT_EQ(column->sourceNodeId, rig.master->nodeID) << "Master's chain hangs off Master's own output";
    EXPECT_TRUE(column->editOnCanvasTargetUuid.isEmpty());
}

TEST(MixerModelMasterInsertTests, MasterWithARecTapEndsAtTheRecTapNotTheOutput) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/true);
    ASSERT_NE(rig.master, nullptr);
    ASSERT_NE(rig.recTap, nullptr);

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    EXPECT_TRUE(column->inserts.empty()) << "the Rec Tap is the terminator, never an insert";
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, rig.recTap->nodeID);
}

TEST(MixerModelMasterInsertTests, LimiterBetweenMasterAndRecTapIsTheOnlyInsert) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/true);
    ASSERT_NE(rig.master, nullptr);
    juce::String limiterUuid;
    auto* limiter = addPlainNodeMMT(f.graph(), "Limiter", limiterUuid);
    ASSERT_NE(limiter, nullptr);
    ASSERT_TRUE(synth::spliceInInsert(f.graph(), rig.master->nodeID, rig.recTap->nodeID, limiter->nodeID));

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->inserts.size(), 1u);
    EXPECT_EQ(column->inserts[0].name, "Limiter");
    EXPECT_EQ(column->inserts[0].nodeId, limiter->nodeID);
    EXPECT_EQ(column->inserts[0].uuid, limiterUuid);
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, rig.recTap->nodeID);
}

TEST(MixerModelMasterInsertTests, InsertsListInSignalOrderAheadOfAudioOutput) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/false);
    ASSERT_NE(rig.master, nullptr);
    juce::String unused;
    auto* eq = addPlainNodeMMT(f.graph(), "Parametric EQ", unused);
    auto* limiter = addPlainNodeMMT(f.graph(), "Limiter", unused);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(limiter, nullptr);
    ASSERT_TRUE(synth::spliceInInsert(f.graph(), rig.master->nodeID, rig.output->nodeID, eq->nodeID));
    ASSERT_TRUE(synth::spliceInInsert(f.graph(), eq->nodeID, rig.output->nodeID, limiter->nodeID));

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->inserts.size(), 2u);
    EXPECT_EQ(column->inserts[0].name, "Parametric EQ");
    EXPECT_EQ(column->inserts[1].name, "Limiter");
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, rig.output->nodeID);
}

TEST(MixerModelMasterInsertTests, ABranchOffMasterMakesTheChainReadOnlyWithEditOnCanvas) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/false);
    ASSERT_NE(rig.master, nullptr);
    juce::String limiterUuid, stubUuid;
    auto* limiter = addPlainNodeMMT(f.graph(), "Limiter", limiterUuid);
    auto* stub = addPlainNodeMMT(f.graph(), "Compressor", stubUuid);
    ASSERT_NE(limiter, nullptr);
    ASSERT_NE(stub, nullptr);
    ASSERT_TRUE(synth::spliceInInsert(f.graph(), rig.master->nodeID, rig.output->nodeID, limiter->nodeID));
    // Master is ALSO cabled to a second node: a fan-out off Master's own output.
    f.graph().addConnection({{rig.master->nodeID, 0}, {stub->nodeID, 0}});

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    EXPECT_FALSE(column->insertChainIsLinear);
    EXPECT_FALSE(column->editOnCanvasTargetUuid.isEmpty()) << "a read-only chain must offer Edit on canvas";
    // Whichever successor the walk follows, "Edit on canvas" lands on something that exists on the canvas.
    const bool targetsALiveNode = column->editOnCanvasTargetUuid == limiterUuid ||
                                  column->editOnCanvasTargetUuid == column->uuid ||
                                  column->editOnCanvasTargetUuid == stubUuid;
    EXPECT_TRUE(targetsALiveNode);
}

TEST(MixerModelMasterInsertTests, ABranchWithNoInsertsBetweenTargetsMasterItself) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/false);
    ASSERT_NE(rig.master, nullptr);
    juce::String unused;
    auto* stub = addPlainNodeMMT(f.graph(), "Compressor", unused);
    ASSERT_NE(stub, nullptr);
    f.graph().addConnection({{rig.master->nodeID, 0}, {stub->nodeID, 0}});

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    EXPECT_FALSE(column->insertChainIsLinear);
    EXPECT_EQ(column->editOnCanvasTargetUuid, column->uuid);
}

TEST(MixerModelMasterInsertTests, AnInsertSharedWithAnotherSourceMakesTheChainReadOnly) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/false);
    ASSERT_NE(rig.master, nullptr);
    juce::String limiterUuid, unused;
    auto* limiter = addPlainNodeMMT(f.graph(), "Limiter", limiterUuid);
    auto* other = addPlainNodeMMT(f.graph(), "Compressor", unused);
    ASSERT_NE(limiter, nullptr);
    ASSERT_NE(other, nullptr);
    ASSERT_TRUE(synth::spliceInInsert(f.graph(), rig.master->nodeID, rig.output->nodeID, limiter->nodeID));
    // A second signal source lands on the very same Limiter: a merge, so reordering/removing it would drag that
    // other signal along.
    f.graph().addConnection({{other->nodeID, 0}, {limiter->nodeID, 0}});

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->inserts.size(), 1u);
    EXPECT_FALSE(column->insertChainIsLinear);
    EXPECT_EQ(column->editOnCanvasTargetUuid, limiterUuid);
}

TEST(MixerModelMasterInsertTests, AnotherFeederOnTheRecTapDoesNotMakeTheChainReadOnly) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/true);
    ASSERT_NE(rig.master, nullptr);
    juce::String unused;
    auto* other = addPlainNodeMMT(f.graph(), "Compressor", unused);
    ASSERT_NE(other, nullptr);
    // A hand-wired source landing straight on the Rec Tap -- the tap is a shared sink by design.
    f.graph().addConnection({{other->nodeID, 0}, {rig.recTap->nodeID, 0}});

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, rig.recTap->nodeID);
}

TEST(MixerModelMasterInsertTests, AMasterThatNeverReachesATerminatorHasAnEmptyReadOnlyList) {
    MasterInsertFixture f;
    const auto rig = buildMasterRigMMT(f.graph(), /*withRecTap=*/false);
    ASSERT_NE(rig.master, nullptr);
    // Cut Master loose from Audio Output: nothing to splice an insert in front of.
    for (int channel = 0; channel < 2; ++channel)
        f.graph().removeConnection({{rig.master->nodeID, channel}, {rig.output->nodeID, channel}});

    const auto snapshot = f.snapshot();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    EXPECT_TRUE(column->inserts.empty());
    EXPECT_FALSE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, juce::AudioProcessorGraph::NodeID{});
    EXPECT_EQ(column->editOnCanvasTargetUuid, column->uuid);
}
