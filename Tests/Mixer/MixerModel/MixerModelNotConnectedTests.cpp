// MixerModelNotConnectedTests.cpp (docs/mixer/mixer.md#not-connected-modules): a module hanging off a track's chain
// that leads nowhere is listed in the column's `dangling`, so every module reachable from the track's source shows
// exactly once, as a chain row or as a not-connected row. Bug kind sibling-gap: the guard walks the graph itself and
// compares it with the column over every shape a chain takes (flat, split, dry path, not connected at each position,
// not connected after a split). Headless: a bare AudioEngine/TimelineDoc/MacroSet.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"
#include "Modules/VCAModule.h"
#include <algorithm>
#include <set>

namespace {
using Node = juce::AudioProcessorGraph::Node;
using Kind = synth::MixerInsertEntry::Kind;

struct NotConnectedRig {
    AudioEngine engine;
    juce::AudioProcessorGraph& graph = engine.getGraph();
    synth::TimelineDoc doc;
    synth::MacroSet macros;
    Node* source = nullptr;
    Node* strip = nullptr;

    NotConnectedRig() {
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

    Node* add(const juce::String& type, int y = 0) {
        juce::String unused;
        auto* node = addPlainNodeMMT(graph, type, unused);
        node->properties.set("y", y);
        return node;
    }
    void wire(Node* from, Node* to) { connectStereoMMT(graph, *from, *to); }

    synth::MixerColumn column() {
        const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
        for (const auto& c : snapshot.columns)
            if (c.nodeId == strip->nodeID)
                return c;
        ADD_FAILURE() << "no column for the strip";
        return {};
    }

    // Every module with a cable path from the source, the strip and the source themselves left out.
    std::set<juce::String> reachableFromSource() const {
        std::set<juce::AudioProcessorGraph::NodeID> seen{source->nodeID};
        std::vector<juce::AudioProcessorGraph::NodeID> todo{source->nodeID};
        while (!todo.empty()) {
            const auto id = todo.back();
            todo.pop_back();
            for (const auto& c : graph.getConnections())
                if (c.source.nodeID == id && c.destination.nodeID != strip->nodeID &&
                    seen.insert(c.destination.nodeID).second)
                    todo.push_back(c.destination.nodeID);
        }
        seen.erase(source->nodeID);
        std::set<juce::String> uuids;
        for (auto id : seen)
            uuids.insert(dynamic_cast<ModuleBase*>(graph.getNodeForId(id)->getProcessor())->getNodeUuid());
        return uuids;
    }

    // The guard: each reachable module appears exactly once across the chain rows and the not-connected rows.
    void expectEveryReachableModuleOnce(const char* shape) {
        const auto column = this->column();
        std::vector<juce::String> shown;
        for (const auto& row : column.inserts)
            if (row.kind == Kind::Module)
                shown.push_back(row.uuid);
        for (const auto& d : column.dangling)
            shown.push_back(d.uuid);
        std::set<juce::String> unique(shown.begin(), shown.end());
        EXPECT_EQ(unique.size(), shown.size()) << shape << ": a module is listed twice";
        EXPECT_EQ(unique, reachableFromSource()) << shape;
    }

    static std::vector<juce::String> names(const std::vector<synth::MixerDanglingEntry>& rows) {
        std::vector<juce::String> out;
        for (const auto& d : rows)
            out.push_back(d.name);
        return out;
    }
};
} // namespace

TEST(MixerModelNotConnectedTests, AFlatChainWithNothingHungOffItHasNoNotConnectedRows) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    auto* delay = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, delay);
    rig.wire(delay, rig.strip);
    EXPECT_TRUE(rig.column().dangling.empty());
    rig.expectEveryReachableModuleOnce("flat");
}

// Not connected at each position of a flat chain: off the source, off the first, middle and last module.
TEST(MixerModelNotConnectedTests, AModuleHungOffAnyPositionOfAFlatChainIsListedOnceAfterItsAnchor) {
    for (int position = -1; position < 3; ++position) {
        NotConnectedRig rig;
        Node* chain[3] = {rig.add("Filter"), rig.add("Chorus"), rig.add("Delay")};
        rig.wire(rig.source, chain[0]);
        rig.wire(chain[0], chain[1]);
        rig.wire(chain[1], chain[2]);
        rig.wire(chain[2], rig.strip);
        auto* hung = rig.add("Reverb");
        rig.wire(position < 0 ? rig.source : chain[position], hung);

        const auto column = rig.column();
        EXPECT_TRUE(column.insertChainIsLinear) << "position " << position;
        EXPECT_EQ(column.inserts.size(), 3u) << "the chain rows are unchanged";
        ASSERT_EQ(column.dangling.size(), 1u) << "position " << position;
        EXPECT_EQ(column.dangling[0].name, "Reverb");
        EXPECT_EQ(column.dangling[0].anchorRow, position) << "it hangs off the row it is fed from";
        EXPECT_EQ(column.dangling[0].depth, 1);
        rig.expectEveryReachableModuleOnce("flat, not connected at one position");
    }
}

TEST(MixerModelNotConnectedTests, AModuleWithNoCableAtAllIsNotListed) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    rig.add("Reverb"); // on the canvas, wired to nothing
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    EXPECT_TRUE(rig.column().dangling.empty());
    rig.expectEveryReachableModuleOnce("an unwired module");
}

// Regression test for FRO1209: a real Oscillator track's own macro has an unwired Macro Out fed by the oscillator, and
// the column listed it as a not-connected module. A macro's port is plumbing, never a loose module.
TEST(MixerModelNotConnectedTests, AMacroPortHungOffTheChainIsNotListed) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    auto* port = rig.add("Macro Out");
    ASSERT_NE(port, nullptr);
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    rig.wire(filter, port);
    EXPECT_TRUE(rig.column().dangling.empty());
}

// A cable to a module outside the track's macro runs through the macro's Macro Out: the module is listed, off the same
// chain row, and the port is not.
TEST(MixerModelNotConnectedTests, AModuleBeyondAMacroPortIsListedOffTheRowThatFeedsThePort) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    auto* port = rig.add("Macro Out");
    auto* delay = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    rig.wire(filter, port);
    rig.wire(port, delay);
    const auto column = rig.column();
    ASSERT_EQ(column.dangling.size(), 1u);
    EXPECT_EQ(column.dangling[0].name, "Delay");
    EXPECT_EQ(column.dangling[0].anchorRow, 0);
    EXPECT_EQ(column.dangling[0].depth, 1);
}

TEST(MixerModelNotConnectedTests, ABranchOfNotConnectedModulesListsEachOnceWithItsDepth) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    auto* first = rig.add("Chorus");
    auto* second = rig.add("Reverb");
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    rig.wire(filter, first);
    rig.wire(first, second);
    const auto column = rig.column();
    ASSERT_EQ(column.dangling.size(), 2u);
    EXPECT_EQ(NotConnectedRig::names(column.dangling), (std::vector<juce::String>{"Chorus", "Reverb"}));
    EXPECT_EQ(column.dangling[0].depth, 1);
    EXPECT_EQ(column.dangling[1].depth, 2);
    EXPECT_EQ(column.dangling[1].anchorRow, 0);
    rig.expectEveryReachableModuleOnce("a not-connected branch");
}

// An effect fed from the chain whose only output goes to a CV jack is not in the sound either.
TEST(MixerModelNotConnectedTests, AModuleWhoseOutputGoesOnlyToACvJackIsListed) {
    NotConnectedRig rig;
    auto* vca = rig.add("VCA");
    auto* delay = rig.add("Delay");
    rig.wire(rig.source, vca);
    rig.wire(vca, rig.strip);
    rig.wire(rig.source, delay);
    rig.graph.addConnection({{delay->nodeID, 0}, {vca->nodeID, 1}}); // the VCA's gain CV
    const auto column = rig.column();
    EXPECT_EQ(NotConnectedRig::names(column.dangling), (std::vector<juce::String>{"Delay"}));
    rig.expectEveryReachableModuleOnce("a CV jack");
}

TEST(MixerModelNotConnectedTests, ASplitListsAModuleHungOffAPathOrOffTheForkOnce) {
    NotConnectedRig rig;
    auto* upper = rig.add("Delay", 100);
    auto* lower = rig.add("Chorus", 300);
    auto* offPath = rig.add("Reverb");
    auto* offFork = rig.add("Filter");
    rig.wire(rig.source, upper);
    rig.wire(rig.source, lower);
    rig.wire(upper, rig.strip);
    rig.wire(lower, rig.strip);
    rig.wire(upper, offPath);
    rig.wire(rig.source, offFork);
    const auto column = rig.column();
    ASSERT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.dangling.size(), 2u);
    for (const auto& d : column.dangling) {
        if (d.name == "Reverb") {
            ASSERT_GE(d.anchorRow, 0);
            EXPECT_EQ(column.inserts[(size_t)d.anchorRow].name, "Delay") << "hangs off the path's module";
        } else {
            EXPECT_EQ(d.name, "Filter");
            EXPECT_EQ(d.anchorRow, -1) << "hangs off the source, the fork";
        }
    }
    rig.expectEveryReachableModuleOnce("split");
}

TEST(MixerModelNotConnectedTests, ADryPathChainListsAModuleHungOffTheMerge) {
    NotConnectedRig rig;
    auto* effect = rig.add("Delay", 100);
    auto* merge = rig.add("Compressor", 300);
    auto* hung = rig.add("Reverb");
    rig.wire(rig.source, effect);
    rig.wire(rig.source, merge); // the dry path
    rig.wire(effect, merge);
    rig.wire(merge, rig.strip);
    rig.wire(merge, hung);
    const auto column = rig.column();
    ASSERT_TRUE(column.insertChainHasSplits);
    EXPECT_EQ(std::count_if(column.inserts.begin(), column.inserts.end(),
                            [](const auto& r) { return r.kind == Kind::DryPath; }),
              1);
    EXPECT_EQ(NotConnectedRig::names(column.dangling), (std::vector<juce::String>{"Reverb"}));
    rig.expectEveryReachableModuleOnce("dry path");
}

TEST(MixerModelNotConnectedTests, AModuleHungOffTheLastRowAfterASplitIsListedAfterIt) {
    NotConnectedRig rig;
    auto* upper = rig.add("Delay", 100);
    auto* lower = rig.add("Chorus", 300);
    auto* merge = rig.add("Compressor", 200);
    auto* hung = rig.add("Reverb");
    rig.wire(rig.source, upper);
    rig.wire(rig.source, lower);
    rig.wire(upper, merge);
    rig.wire(lower, merge);
    rig.wire(merge, rig.strip);
    rig.wire(merge, hung);
    const auto column = rig.column();
    ASSERT_TRUE(column.insertChainHasSplits);
    ASSERT_EQ(column.dangling.size(), 1u);
    ASSERT_GE(column.dangling[0].anchorRow, 0);
    EXPECT_EQ(column.inserts[(size_t)column.dangling[0].anchorRow].name, "Compressor");
    EXPECT_EQ(column.dangling[0].anchorRow, (int)column.inserts.size() - 1) << "after the split's merge, the last row";
    rig.expectEveryReachableModuleOnce("not connected after a split");
}

// Plugging the module into the chain makes it a chain row and drops it from the not-connected rows, and unplugging
// puts it back.
TEST(MixerModelNotConnectedTests, PluggingAModuleIntoTheChainMovesItFromNotConnectedToInsertsAndBack) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    auto* hung = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    rig.wire(filter, hung);
    ASSERT_EQ(rig.column().dangling.size(), 1u);

    rig.wire(hung, rig.strip);
    auto column = rig.column();
    EXPECT_TRUE(column.dangling.empty());
    EXPECT_EQ(rig.column().inserts.size() > 1u, true) << "the Delay is a second path now";
    rig.expectEveryReachableModuleOnce("plugged in");

    for (const auto& c : rig.graph.getConnections())
        if (c.source.nodeID == hung->nodeID && c.destination.nodeID == rig.strip->nodeID)
            rig.graph.removeConnection(c);
    column = rig.column();
    EXPECT_EQ(NotConnectedRig::names(column.dangling), (std::vector<juce::String>{"Delay"}));
    EXPECT_EQ(column.inserts.size(), 1u);
}

TEST(MixerModelNotConnectedTests, NotConnectedRowsDoNotChangeWhatTheChainEditsMayDo) {
    NotConnectedRig rig;
    auto* filter = rig.add("Filter");
    auto* hung = rig.add("Delay");
    rig.wire(rig.source, filter);
    rig.wire(filter, rig.strip);
    rig.wire(filter, hung);
    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainIsLinear);
    EXPECT_EQ(synth::soleSignalNeighbour(rig.graph, filter->nodeID, false, rig.strip->nodeID), rig.strip->nodeID);
}
