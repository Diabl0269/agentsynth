// MixerModelDanglingBranchTests.cpp (docs/mixer/mixer.md#inserts-in-a-free-form-graph): a branch that never reaches
// the strip (a module wired in with no output) is no path of the chain. It neither makes the list read-only nor ends
// the walk, and the edit primitives leave its cable alone. Headless: a bare AudioEngine/TimelineDoc/MacroSet.
#include "../../fixtures/saved-project/SavedProjectFixture.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"
#include "Modules/VCAModule.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>

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

// The shape of a real Oscillator track in a saved project (Track In -> Oscillator and ADSR; Oscillator -> VCA
// audio, ADSR -> VCA gain CV; VCA -> Gate -> EQ -> Compressor -> strip), opened through the saved-project fixture.
namespace {
struct OscTrackProject {
    synth::test::SavedProjectFixture project;
    std::unique_ptr<MainComponent> mc;

    OscTrackProject() {
        if (!project.wasSaved())
            return;
        mc = project.open();
        if (mc != nullptr)
            mc->simulateAddInstrumentTrackClick(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    Node* node(const juce::String& name) {
        Node* found = nullptr;
        for (auto* n : graph().getNodes())
            if (n->getProcessor()->getName() == name)
                found = n; // the last one: the new track's nodes are added after the fixture's
        return found;
    }
    Node* addDelay() {
        juce::String unused;
        return addPlainNodeMMT(graph(), "Delay", unused);
    }
    synth::MixerColumn column() {
        const auto snapshot =
            synth::buildMixerSnapshot(graph(), mc->getTimelineDoc(), mc->getGraphEditor().getMacros());
        for (const auto& c : snapshot.columns)
            if (c.nodeId == node("Channel Strip")->nodeID)
                return c;
        ADD_FAILURE() << "no column for the Oscillator track's strip";
        return {};
    }
};
} // namespace

std::vector<juce::String> rowNames(const synth::MixerColumn& column) { return names(column); }

// Bug kind real-project: an Oscillator track (VCA with ADSR on its gain CV) with a Delay hung off the
// oscillator. Hung with no output it is a dangling branch and shows nothing (nothing of the chain). Wired into the
// VCA's audio input it is a real second path beside the oscillator's direct cable, so the column shows it as 2 paths
// with the Dry row. The shape was rebuilt through the saved-project fixture because the project itself is not on
// every machine.
TEST(MixerModelDanglingBranchTests, ARealOscillatorTrackShowsADelayWiredIntoItsVcaAsTwoPaths) {
    OscTrackProject rig;
    ASSERT_NE(rig.mc, nullptr);
    auto* osc = rig.node("Oscillator");
    auto* vca = rig.node("VCA");
    auto* delay = rig.addDelay();
    ASSERT_TRUE(osc != nullptr && vca != nullptr && delay != nullptr);
    connectStereoMMT(rig.graph(), *osc, *delay);

    const std::vector<juce::String> chain{"Oscillator", "VCA", "Gate", "Parametric EQ", "Compressor"};
    EXPECT_EQ(rowNames(rig.column()), chain) << "a Delay with no output is not part of the chain";

    rig.graph().addConnection({{delay->nodeID, 0}, {vca->nodeID, 0}});
    rig.graph().addConnection({{delay->nodeID, 1}, {vca->nodeID, VCAModule::kRightBase}});
    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainHasSplits);
    const auto shown = rowNames(column);
    EXPECT_EQ(std::count(shown.begin(), shown.end(), juce::String("Delay")), 1);
    EXPECT_EQ(std::count(shown.begin(), shown.end(), juce::String("Dry (no effects)")), 1);
    ASSERT_GT(column.inserts.size(), 1u);
    EXPECT_EQ(column.inserts[1].kind, Kind::Split);
    EXPECT_EQ(column.inserts[1].pathCount, 2);
}

// The Delay's output on the VCA's gain CV input is modulation by design: it stays off the signal path, so the
// column shows the plain chain.
TEST(MixerModelDanglingBranchTests, ARealOscillatorTrackIgnoresADelayWiredIntoItsVcaGainCv) {
    OscTrackProject rig;
    ASSERT_NE(rig.mc, nullptr);
    auto* osc = rig.node("Oscillator");
    auto* vca = rig.node("VCA");
    auto* delay = rig.addDelay();
    connectStereoMMT(rig.graph(), *osc, *delay);
    rig.graph().addConnection({{delay->nodeID, 0}, {vca->nodeID, 1}});

    const auto column = rig.column();
    EXPECT_TRUE(column.insertChainIsLinear);
    EXPECT_EQ(rowNames(column),
              (std::vector<juce::String>{"Oscillator", "VCA", "Gate", "Parametric EQ", "Compressor"}));
}
