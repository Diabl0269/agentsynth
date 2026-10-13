// MixerModelSplitInsertTests.cpp (docs/mixer/mixer.md#parallel-paths): a strip chain that splits into parallel paths
// and meets again before the strip lists as a Split row plus its paths' rows; any other branch stays read-only.
// Headless: a bare AudioEngine/TimelineDoc/MacroSet.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"
#include "Modules/FX/GateModule.h"

namespace {
using Kind = synth::MixerInsertEntry::Kind;

// A bare graph with one audio track whose Track Audio node is the chain's source, and one stereo strip.
struct SplitRig {
    AudioEngine engine;
    juce::AudioProcessorGraph& graph = engine.getGraph();
    synth::TimelineDoc doc;
    synth::MacroSet macros;
    juce::AudioProcessorGraph::Node* source = nullptr;
    juce::AudioProcessorGraph::Node* strip = nullptr;
    juce::String sourceUuid;

    SplitRig() {
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        const auto track = doc.addTrack(synth::TrackKind::Audio, "Lead");
        source = addPlainNodeMMT(graph, "Track Audio", sourceUuid);
        juce::String unused;
        strip = addPlainNodeMMT(graph, "Channel Strip", unused);
        if (auto* m = dynamic_cast<ChannelStripModule*>(strip->getProcessor()))
            m->setShape(ChannelStripModule::Shape::Stereo);
        doc.setTrackBinding(track, sourceUuid);
    }

    juce::AudioProcessorGraph::Node* add(const juce::String& type) {
        juce::String unused;
        return addPlainNodeMMT(graph, type, unused);
    }

    synth::MixerColumn column() {
        const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
        for (const auto& c : snapshot.columns)
            if (c.nodeId == strip->nodeID)
                return c;
        ADD_FAILURE() << "no column for the strip";
        return {};
    }
};

void expectRow(const synth::MixerInsertEntry& row, Kind kind, const juce::String& name, int depth, int path) {
    EXPECT_EQ(row.kind, kind) << name;
    EXPECT_EQ(row.name, name);
    EXPECT_EQ(row.depth, depth) << name;
    EXPECT_EQ(row.path, path) << name;
}
} // namespace

// The ticket's shape: Filter feeds Delay and Reverb in parallel, both into the VCA, then the strip.
TEST(MixerModelSplitInsertTests, TwoParallelEffectsMeetingAtAnInsertListAsOneSplitRowWithBothPaths) {
    SplitRig rig;
    auto* filter = rig.add("Filter");
    auto* delay = rig.add("Delay");
    auto* reverb = rig.add("Reverb");
    auto* vca = rig.add("VCA");
    connectStereoMMT(rig.graph, *rig.source, *filter);
    connectStereoMMT(rig.graph, *filter, *delay);
    connectStereoMMT(rig.graph, *filter, *reverb);
    connectStereoMMT(rig.graph, *delay, *vca);
    connectStereoMMT(rig.graph, *reverb, *vca);
    connectStereoMMT(rig.graph, *vca, *rig.strip);

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainHasSplits);
    EXPECT_FALSE(column.insertChainIsLinear) << "a track delete must keep a split chain's modules, as for any branch";
    EXPECT_TRUE(column.editOnCanvasTargetUuid.isEmpty());
    ASSERT_EQ(column.inserts.size(), 5u);
    expectRow(column.inserts[0], Kind::Module, "Filter", 0, -1);
    expectRow(column.inserts[1], Kind::Split, "2 paths", 0, -1);
    EXPECT_EQ(column.inserts[1].pathCount, 2);
    EXPECT_EQ(column.inserts[1].nodeId, juce::AudioProcessorGraph::NodeID{});
    EXPECT_EQ(column.inserts[1].uuid, column.inserts[0].uuid) << "the split row carries the fork's uuid";
    expectRow(column.inserts[2], Kind::Module, "Delay", 1, 0);
    expectRow(column.inserts[3], Kind::Module, "Reverb", 1, 1);
    expectRow(column.inserts[4], Kind::Module, "VCA", 0, -1);
    EXPECT_EQ(column.inserts[2].nodeId, delay->nodeID);
}

TEST(MixerModelSplitInsertTests, PathsMeetingAtTheStripItselfAndASourceThatFansOutParse) {
    SplitRig rig;
    auto* delay = rig.add("Delay");
    auto* reverb = rig.add("Reverb");
    connectStereoMMT(rig.graph, *rig.source, *delay);
    connectStereoMMT(rig.graph, *rig.source, *reverb);
    connectStereoMMT(rig.graph, *delay, *rig.strip);
    connectStereoMMT(rig.graph, *reverb, *rig.strip);

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.inserts.size(), 3u);
    expectRow(column.inserts[0], Kind::Split, "2 paths", 0, -1);
    EXPECT_EQ(column.inserts[0].uuid, rig.sourceUuid);
    expectRow(column.inserts[1], Kind::Module, "Delay", 1, 0);
    expectRow(column.inserts[2], Kind::Module, "Reverb", 1, 1);
}

// The commonest parallel-FX patch: one path is wet, the other wired straight across (dry).
TEST(MixerModelSplitInsertTests, ADryPathWiredStraightToTheMergeIsAPathWithNoRows) {
    SplitRig rig;
    auto* reverb = rig.add("Reverb");
    auto* vca = rig.add("VCA");
    connectStereoMMT(rig.graph, *rig.source, *reverb);
    connectStereoMMT(rig.graph, *rig.source, *vca);
    connectStereoMMT(rig.graph, *reverb, *vca);
    connectStereoMMT(rig.graph, *vca, *rig.strip);

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.inserts.size(), 3u);
    expectRow(column.inserts[0], Kind::Split, "2 paths", 0, -1);
    expectRow(column.inserts[1], Kind::Module, "Reverb", 1, 0);
    expectRow(column.inserts[2], Kind::Module, "VCA", 0, -1);
}

TEST(MixerModelSplitInsertTests, ASplitInsideAPathNestsOneLevelDeeper) {
    SplitRig rig;
    auto* delay = rig.add("Delay");
    auto* chorus = rig.add("Chorus");
    auto* phaser = rig.add("Phaser");
    auto* mix = rig.add("VCA");
    auto* reverb = rig.add("Reverb");
    auto* out = rig.add("Compressor");
    // source -> {delay -> {chorus | phaser} -> mix | reverb} -> out -> strip
    connectStereoMMT(rig.graph, *rig.source, *delay);
    connectStereoMMT(rig.graph, *rig.source, *reverb);
    connectStereoMMT(rig.graph, *delay, *chorus);
    connectStereoMMT(rig.graph, *delay, *phaser);
    connectStereoMMT(rig.graph, *chorus, *mix);
    connectStereoMMT(rig.graph, *phaser, *mix);
    connectStereoMMT(rig.graph, *mix, *out);
    connectStereoMMT(rig.graph, *reverb, *out);
    connectStereoMMT(rig.graph, *out, *rig.strip);

    const auto column = rig.column();
    ASSERT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.inserts.size(), 8u);
    expectRow(column.inserts[0], Kind::Split, "2 paths", 0, -1);
    expectRow(column.inserts[1], Kind::Module, "Delay", 1, 0);
    expectRow(column.inserts[2], Kind::Split, "2 paths", 1, 0);
    expectRow(column.inserts[3], Kind::Module, "Chorus", 2, 0);
    expectRow(column.inserts[4], Kind::Module, "Phaser", 2, 1);
    expectRow(column.inserts[5], Kind::Module, "VCA", 1, 0);
    expectRow(column.inserts[6], Kind::Module, "Reverb", 1, 1);
    expectRow(column.inserts[7], Kind::Module, "Compressor", 0, -1);
}

// A path that leaves the reach (feeds something other than the merge) is not a clean split: today's read-only list.
TEST(MixerModelSplitInsertTests, APathThatLeavesTheChainFallsBackToTheReadOnlyBranchingList) {
    SplitRig rig;
    auto* delay = rig.add("Delay");
    auto* reverb = rig.add("Reverb");
    auto* elsewhere = rig.add("Audio Output");
    connectStereoMMT(rig.graph, *rig.source, *delay);
    connectStereoMMT(rig.graph, *rig.source, *reverb);
    connectStereoMMT(rig.graph, *delay, *rig.strip);
    rig.graph.addConnection({{reverb->nodeID, 0}, {elsewhere->nodeID, 0}});

    const auto column = rig.column();
    EXPECT_FALSE(column.insertChainHasSplits);
    EXPECT_FALSE(column.insertChainIsLinear);
    EXPECT_FALSE(column.editOnCanvasTargetUuid.isEmpty()) << "a branch the mixer cannot list points at the canvas";
    for (const auto& row : column.inserts)
        EXPECT_EQ(row.kind, Kind::Module);
}

// A sidechain cable into a Key jack is not signal: it neither adds a path nor counts as a second feed of the merge.
TEST(MixerModelSplitInsertTests, ASidechainKeyCableDoesNotChangeTheSplit) {
    SplitRig rig;
    auto* delay = rig.add("Delay");
    auto* reverb = rig.add("Reverb");
    auto* gate = rig.add("Gate");
    connectStereoMMT(rig.graph, *rig.source, *delay);
    connectStereoMMT(rig.graph, *rig.source, *reverb);
    connectStereoMMT(rig.graph, *delay, *gate);
    connectStereoMMT(rig.graph, *reverb, *gate);
    connectStereoMMT(rig.graph, *gate, *rig.strip);
    rig.graph.addConnection({{delay->nodeID, 0}, {gate->nodeID, GateModule::kKeyBase}});

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.inserts.size(), 4u);
    expectRow(column.inserts[3], Kind::Module, "Gate", 0, -1);
}

TEST(MixerModelSplitInsertTests, ALinearChainIsUnchanged) {
    SplitRig rig;
    auto* delay = rig.add("Delay");
    connectStereoMMT(rig.graph, *rig.source, *delay);
    connectStereoMMT(rig.graph, *delay, *rig.strip);

    const auto column = rig.column();
    EXPECT_FALSE(column.insertChainHasSplits);
    EXPECT_TRUE(column.insertChainIsLinear);
    ASSERT_EQ(column.inserts.size(), 1u);
    expectRow(column.inserts[0], Kind::Module, "Delay", 0, -1);
}
