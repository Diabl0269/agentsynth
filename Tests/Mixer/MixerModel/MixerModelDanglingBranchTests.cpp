// MixerModelDanglingBranchTests.cpp (docs/mixer/mixer.md#inserts-in-a-free-form-graph): a branch that never reaches
// the strip (a module wired in with no output) is no path of the chain. It neither makes the list read-only nor ends
// the walk, and the edit primitives leave its cable alone. Headless: a bare AudioEngine/TimelineDoc/MacroSet.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"

namespace {
using Kind = synth::MixerInsertEntry::Kind;
using Node = juce::AudioProcessorGraph::Node;

struct DanglingRig {
    AudioEngine engine;
    juce::AudioProcessorGraph& graph = engine.getGraph();
    synth::TimelineDoc doc;
    synth::MacroSet macros;
    Node* source = nullptr;
    Node* strip = nullptr;

    DanglingRig() {
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        const auto track = doc.addTrack(synth::TrackKind::Audio, "Lead");
        juce::String sourceUuid;
        source = addPlainNodeMMT(graph, "Track Audio", sourceUuid);
        juce::String unused;
        strip = addPlainNodeMMT(graph, "Channel Strip", unused);
        if (auto* m = dynamic_cast<ChannelStripModule*>(strip->getProcessor()))
            m->setShape(ChannelStripModule::Shape::Stereo);
        doc.setTrackBinding(track, sourceUuid);
    }

    Node* add(const juce::String& type) {
        juce::String unused;
        return addPlainNodeMMT(graph, type, unused);
    }

    void wire(Node* from, Node* to) { connectStereoMMT(graph, *from, *to); }

    bool wired(Node* from, Node* to) const {
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == from->nodeID && c.destination.nodeID == to->nodeID)
                return true;
        return false;
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

std::vector<juce::String> names(const synth::MixerColumn& column) {
    std::vector<juce::String> out;
    for (const auto& row : column.inserts)
        out.push_back(row.name);
    return out;
}
} // namespace

// Regression test for FRO975: a dangling branch hid "+ Insert" by turning the chain into a read-only branch.
TEST(MixerModelDanglingBranchTests, ADanglingModuleOffAnInsertLeavesTheChainEditable) {
    DanglingRig rig;
    auto* filter = rig.add("Filter");
    auto* vca = rig.add("VCA");
    auto* dangling = rig.add("Delay"); // no output
    rig.wire(rig.source, filter);
    rig.wire(filter, dangling);
    rig.wire(filter, vca);
    rig.wire(vca, rig.strip);

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainIsLinear);
    EXPECT_FALSE(column.insertChainHasSplits);
    EXPECT_EQ(names(column), (std::vector<juce::String>{"Filter", "VCA"})) << "the real path only";
}

TEST(MixerModelDanglingBranchTests, ADanglingModuleOffTheSourceLeavesTheChainEditable) {
    DanglingRig rig;
    auto* filter = rig.add("Filter");
    auto* vca = rig.add("VCA");
    auto* dangling = rig.add("Delay");
    rig.wire(rig.source, dangling);
    rig.wire(rig.source, filter);
    rig.wire(filter, vca);
    rig.wire(vca, rig.strip);

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainIsLinear);
    EXPECT_EQ(names(column), (std::vector<juce::String>{"Filter", "VCA"}));
}

// The parser gives up on this chain (a foreign module also feeds the VCA), so the flat walk lists it read-only; the
// dangling cable is wired first so a walk that took the first signal edge would wander into it and list nothing.
TEST(MixerModelDanglingBranchTests, TheFlatWalkSkipsADanglingBranchAndStillReachesTheStrip) {
    DanglingRig rig;
    auto* filter = rig.add("Filter");
    auto* vca = rig.add("VCA");
    auto* dangling = rig.add("Delay");
    auto* foreign = rig.add("Chorus");
    rig.wire(rig.source, filter);
    rig.wire(filter, dangling);
    rig.wire(filter, vca);
    rig.wire(foreign, vca);
    rig.wire(vca, rig.strip);

    const auto column = rig.column();
    EXPECT_FALSE(column.insertChainIsLinear) << "the VCA has a second feeder, so the chain stays read-only";
    EXPECT_EQ(names(column), (std::vector<juce::String>{"Filter", "VCA"}));
}

TEST(MixerModelDanglingBranchTests, ADanglingBranchOnASplitDoesNotAddAPath) {
    DanglingRig rig;
    auto* delay = rig.add("Delay");
    auto* reverb = rig.add("Reverb");
    auto* dangling = rig.add("Chorus");
    rig.wire(rig.source, delay);
    rig.wire(rig.source, reverb);
    rig.wire(rig.source, dangling);
    rig.wire(delay, rig.strip);
    rig.wire(reverb, rig.strip);

    const auto column = rig.column();
    ASSERT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.inserts.size(), 3u);
    EXPECT_EQ(column.inserts[0].kind, Kind::Split);
    EXPECT_EQ(column.inserts[0].pathCount, 2);
}

TEST(MixerModelDanglingBranchTests, SoleSignalNeighbourIgnoresTheDanglingEdgeOnlyWhenGivenTheStrip) {
    DanglingRig rig;
    auto* filter = rig.add("Filter");
    auto* vca = rig.add("VCA");
    auto* dangling = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, dangling);
    rig.wire(filter, vca);
    rig.wire(vca, rig.strip);

    EXPECT_EQ(synth::soleSignalNeighbour(rig.graph, filter->nodeID, false), juce::AudioProcessorGraph::NodeID{});
    EXPECT_EQ(synth::soleSignalNeighbour(rig.graph, filter->nodeID, false, rig.strip->nodeID), vca->nodeID);
}

TEST(MixerModelDanglingBranchTests, RemovingTheForkModuleBridgesTheRealPathAndLeavesTheDanglingCable) {
    DanglingRig rig;
    auto* filter = rig.add("Filter");
    auto* vca = rig.add("VCA");
    auto* dangling = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, dangling);
    rig.wire(filter, vca);
    rig.wire(vca, rig.strip);

    ASSERT_TRUE(synth::spliceOutInsert(rig.graph, filter->nodeID, rig.strip->nodeID));
    EXPECT_TRUE(rig.wired(rig.source, vca));
    EXPECT_FALSE(rig.wired(rig.source, dangling)) << "never bridged into the dangling branch";
    EXPECT_TRUE(rig.wired(filter, dangling)) << "the dangling cable is untouched";
    EXPECT_FALSE(rig.wired(filter, vca));
}

TEST(MixerModelDanglingBranchTests, MovingTheForkModuleKeepsItsDanglingCableAndReordersTheRealPath) {
    DanglingRig rig;
    auto* filter = rig.add("Filter");
    auto* vca = rig.add("VCA");
    auto* dangling = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, dangling);
    rig.wire(filter, vca);
    rig.wire(vca, rig.strip);

    ASSERT_TRUE(synth::reorderInsert(rig.graph, filter->nodeID, vca->nodeID, rig.strip->nodeID, rig.strip->nodeID));
    EXPECT_EQ(names(rig.column()), (std::vector<juce::String>{"VCA", "Filter"}));
    EXPECT_TRUE(rig.wired(filter, dangling));
    EXPECT_FALSE(rig.wired(vca, dangling));
}
