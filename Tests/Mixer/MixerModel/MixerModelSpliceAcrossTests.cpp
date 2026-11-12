// MixerModelSpliceAcrossTests.cpp (docs/mixer/mixer.md#adding-an-insert): spliceInInsertAcross puts one module in front
// of every path of a split (its fork feeds it, it feeds each path) or behind every path (each path's end feeds it, it
// feeds the merge). Every case asserts the RAW graph edges (node, channel pairs), never a drawn cable, over the
// collapsed/split sides of the modules involved (a module's right leg is not always channel 1).
// Headless: a bare AudioEngine.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"

#include <set>
#include <tuple>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Node = juce::AudioProcessorGraph::Node;
using Edge = std::tuple<juce::uint32, int, juce::uint32, int>;

struct AcrossRig {
    AudioEngine engine;
    juce::AudioProcessorGraph& graph = engine.getGraph();
    Node* source = nullptr;
    Node* strip = nullptr;

    AcrossRig() {
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        juce::String unused;
        source = addPlainNodeMMT(graph, "Track Audio", unused);
        strip = addPlainNodeMMT(graph, "Channel Strip", unused);
        if (auto* m = dynamic_cast<ChannelStripModule*>(strip->getProcessor()))
            m->setShape(ChannelStripModule::Shape::Stereo);
    }

    Node* add(const juce::String& type) {
        juce::String unused;
        return addPlainNodeMMT(graph, type, unused);
    }

    void wire(Node* from, Node* to) { connectStereoMMT(graph, *from, *to); }

    std::set<Edge> edges() const {
        std::set<Edge> out;
        for (const auto& c : graph.getConnections())
            out.insert(
                {c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
        return out;
    }
};

// The raw edges of one stereo link `from` -> `to`: left ch0 to ch0, right from the source's own right leg to the
// destination's own right leg.
std::set<Edge> stereoEdges(Node* from, Node* to) {
    auto* a = dynamic_cast<ModuleBase*>(from->getProcessor());
    auto* b = dynamic_cast<ModuleBase*>(to->getProcessor());
    std::set<Edge> out{{from->nodeID.uid, 0, to->nodeID.uid, 0}};
    // A source with no right output (the Track Audio node) has no right cable to carry.
    if (a->rightAudioLegChannel() >= 0 && b->rightAudioLegChannel() >= 0)
        out.insert({from->nodeID.uid, a->rightAudioLegChannel(), to->nodeID.uid, b->rightAudioLegChannel()});
    return out;
}

std::set<Edge> links(std::initializer_list<std::pair<Node*, Node*>> pairs) {
    std::set<Edge> out;
    for (const auto& [from, to] : pairs)
        out.merge(stereoEdges(from, to));
    return out;
}

const char* const kPathModules[] = {"Delay", "Filter"};
const char* const kInserted[] = {"Compressor", "Filter", "Reverb", "VCA"};

std::vector<NodeID> ids(std::initializer_list<Node*> nodes) {
    std::vector<NodeID> out;
    for (auto* n : nodes)
        out.push_back(n->nodeID);
    return out;
}
} // namespace

// Split last: the paths meet at the strip. The new module sits between them and the strip, once, and the strip's
// left and right inputs each get exactly the one cable from it.
TEST(MixerModelSpliceAcrossTests, BehindPathsThatMeetAtTheStripWiresEveryPathThroughTheNewModule) {
    for (const auto* pathType : kPathModules)
        for (const auto* insertedType : kInserted) {
            SCOPED_TRACE(juce::String(pathType) + " paths, " + insertedType + " inserted");
            AcrossRig rig;
            auto* a = rig.add(pathType);
            auto* b = rig.add(pathType);
            auto* node = rig.add(insertedType);
            rig.wire(rig.source, a);
            rig.wire(rig.source, b);
            rig.wire(a, rig.strip);
            rig.wire(b, rig.strip);

            const auto predecessors = ids({rig.source, a, b});
            ASSERT_TRUE(synth::spliceInInsertAcross(rig.graph, predecessors, ids({rig.strip}), node->nodeID));
            EXPECT_EQ(rig.edges(), links({{rig.source, a}, {rig.source, b}, {a, node}, {b, node}, {node, rig.strip}}));
        }
}

// A dry path (the fork cabled straight into the merge) is one more cable into the new module.
TEST(MixerModelSpliceAcrossTests, BehindPathsWithADryPathAlsoRoutesTheDryCableThroughTheNewModule) {
    for (const auto* insertedType : kInserted) {
        SCOPED_TRACE(insertedType);
        AcrossRig rig;
        auto* a = rig.add("Delay");
        auto* node = rig.add(insertedType);
        rig.wire(rig.source, a);
        rig.wire(rig.source, rig.strip); // the dry path
        rig.wire(a, rig.strip);

        ASSERT_TRUE(synth::spliceInInsertAcross(rig.graph, ids({rig.source, a}), ids({rig.strip}), node->nodeID));
        EXPECT_EQ(rig.edges(), links({{rig.source, a}, {a, node}, {rig.source, node}, {node, rig.strip}}));
    }
}

// Split first: the fork feeds the new module, which feeds each path's first node (or the merge for a dry path).
TEST(MixerModelSpliceAcrossTests, InFrontOfPathsRoutesTheForkThroughTheNewModuleIntoEveryPath) {
    for (const auto* pathType : kPathModules)
        for (const auto* insertedType : kInserted) {
            SCOPED_TRACE(juce::String(pathType) + " paths, " + insertedType + " inserted");
            AcrossRig rig;
            auto* a = rig.add(pathType);
            auto* b = rig.add("Reverb");
            auto* merge = rig.add("VCA");
            auto* node = rig.add(insertedType);
            auto* fork = rig.add("Oscillator"); // a stereo source: both legs carry cables
            rig.wire(rig.source, fork);
            rig.wire(fork, a);
            rig.wire(fork, b);
            rig.wire(fork, merge); // a dry third path
            rig.wire(a, merge);
            rig.wire(b, merge);
            rig.wire(merge, rig.strip);

            ASSERT_TRUE(synth::spliceInInsertAcross(rig.graph, ids({fork}), ids({a, b, merge}), node->nodeID));
            EXPECT_EQ(rig.edges(), links({{rig.source, fork},
                                          {fork, node},
                                          {node, a},
                                          {node, b},
                                          {node, merge},
                                          {a, merge},
                                          {b, merge},
                                          {merge, rig.strip}}));
        }
}

// A mid-chain split: the module before it is the fork, the module after the paths the merge. Both adds at once leave
// every path wired between them.
TEST(MixerModelSpliceAcrossTests, BothSidesOfAMidChainSplitKeepEveryPathWired) {
    AcrossRig rig;
    auto* filter = rig.add("Filter");
    auto* a = rig.add("Delay");
    auto* b = rig.add("Reverb");
    auto* vca = rig.add("VCA");
    auto* before = rig.add("Compressor");
    auto* after = rig.add("Chorus");
    rig.wire(rig.source, filter);
    rig.wire(filter, a);
    rig.wire(filter, b);
    rig.wire(a, vca);
    rig.wire(b, vca);
    rig.wire(vca, rig.strip);

    ASSERT_TRUE(synth::spliceInInsertAcross(rig.graph, ids({filter}), ids({a, b}), before->nodeID));
    ASSERT_TRUE(synth::spliceInInsertAcross(rig.graph, ids({filter, a, b}), ids({vca}), after->nodeID));
    EXPECT_EQ(rig.edges(), links({{rig.source, filter},
                                  {filter, before},
                                  {before, a},
                                  {before, b},
                                  {a, after},
                                  {b, after},
                                  {after, vca},
                                  {vca, rig.strip}}));
}

// With one predecessor and one successor it is spliceInInsert: the same edges for the same input.
TEST(MixerModelSpliceAcrossTests, OnePairBehavesLikeSpliceInInsert) {
    AcrossRig one, across;
    for (auto* rig : {&one, &across}) {
        auto* a = rig->add("Filter");
        auto* node = rig->add("Compressor");
        rig->wire(rig->source, a);
        rig->wire(a, rig->strip);
        if (rig == &one)
            ASSERT_TRUE(synth::spliceInInsert(rig->graph, a->nodeID, rig->strip->nodeID, node->nodeID));
        else
            ASSERT_TRUE(synth::spliceInInsertAcross(rig->graph, ids({a}), ids({rig->strip}), node->nodeID));
    }
    EXPECT_EQ(one.edges().size(), across.edges().size());
}

// No cable between the two sets: false, and the graph is as it was.
TEST(MixerModelSpliceAcrossTests, NoCableBetweenTheSetsChangesNothing) {
    AcrossRig rig;
    auto* a = rig.add("Delay");
    auto* node = rig.add("Compressor");
    rig.wire(rig.source, a);
    const auto before = rig.edges();
    EXPECT_FALSE(synth::spliceInInsertAcross(rig.graph, ids({a}), ids({rig.strip}), node->nodeID));
    EXPECT_EQ(rig.edges(), before);
}

// A module hung off the fork with no output is not part of the split: it is no exit of the fork, and an add before
// the paths leaves its cable alone.
TEST(MixerModelSpliceAcrossTests, ADanglingBranchIsNotAnExitAndKeepsItsCable) {
    AcrossRig rig;
    auto* a = rig.add("Delay");
    auto* b = rig.add("Reverb");
    auto* dangling = rig.add("Distortion");
    rig.wire(rig.source, a);
    rig.wire(rig.source, b);
    rig.wire(a, rig.strip);
    rig.wire(b, rig.strip);
    rig.wire(rig.source, dangling);

    const auto exits = synth::liveExitsOf(rig.graph, ids({rig.source}), rig.strip->nodeID);
    EXPECT_EQ(exits.size(), 2u);
    EXPECT_EQ(std::count(exits.begin(), exits.end(), dangling->nodeID), 0);
    EXPECT_EQ(synth::liveExitsOf(rig.graph, ids({rig.source, a, b}), rig.strip->nodeID),
              std::vector<NodeID>{rig.strip->nodeID});

    auto* node = rig.add("Compressor");
    ASSERT_TRUE(synth::spliceInInsertAcross(rig.graph, ids({rig.source}), exits, node->nodeID));
    const auto after = rig.edges();
    for (const auto& edge : stereoEdges(rig.source, dangling))
        EXPECT_EQ(after.count(edge), 1u) << "the dangling cable stays";
    for (const auto& edge : stereoEdges(rig.source, a))
        EXPECT_EQ(after.count(edge), 0u);
}
