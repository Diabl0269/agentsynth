// TimelineMidiDestinationMacroRoutingTests.cpp: a Timeline "MIDI destinations" toggle routes through macro MIDI ports
// the way a mixer send does (docs/macros/auto-ports.md#programmatic-connections). Drives a real MainComponent as the
// TrackHeaderHost with hand-built macros around a MIDI track's Track In and its destinations.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ModuleBase.h"
#include "TimelinePanel/TimelinePanelTestFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

class MidiDestinationMacroRoutingTest : public TimelineAppWiringTest {
protected:
    void SetUp() override {
        TimelineAppWiringTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderTL>());
        mc->setSize(1600, 900);
        quiesceEngine(*mc);
        prepareCanvas(*mc, 0);
        mc->simulateAddMidiTrackClick();
        ASSERT_EQ(mc->getTimelineDoc().getTracks().size(), 1u);
        track = mc->getTimelineDoc().getTracks()[0].id;
        trackIn = nodeForUuid(mc->getTimelineDoc().getTracks()[0].bindingUuid);
        ASSERT_NE(trackIn, NodeID{});
        mc->getUndoManager().clearUndoHistory();
    }
    void TearDown() override {
        mc.reset();
        TimelineAppWiringTest::TearDown();
    }

    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    GraphEditor& editor() { return mc->getGraphEditor(); }

    NodeID nodeForUuid(const juce::String& uuid) {
        for (auto* node : graph().getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return node->nodeID;
        return {};
    }

    NodeID addOscillator(int x) {
        std::set<juce::uint32> before;
        for (auto* node : graph().getNodes())
            before.insert(node->nodeID.uid);
        editor().addModuleAtCanvasPosition("Oscillator", {x, 600}, {});
        for (auto* node : graph().getNodes())
            if (before.count(node->nodeID.uid) == 0)
                return node->nodeID;
        return {};
    }

    juce::String group(std::initializer_list<NodeID> ids) {
        editor().setSelectedNodes(std::vector<NodeID>(ids));
        const auto id = editor().getMacroController().groupSelectionIntoMacro();
        editor().clearSelection();
        return id;
    }

    int countPorts(ModuleType type) {
        int n = 0;
        for (auto* node : graph().getNodes())
            if (auto* m = dynamic_cast<ModuleBase*>(node->getProcessor()); m != nullptr && m->getModuleType() == type)
                ++n;
        return n;
    }
    int midiInlets() { return countPorts(ModuleType::MacroMidiInlet); }
    int midiOutlets() { return countPorts(ModuleType::MacroMidiOutlet); }

    bool midiEdge(NodeID from, NodeID to) {
        return graph().isConnected(
            {{from, juce::AudioProcessorGraph::midiChannelIndex}, {to, juce::AudioProcessorGraph::midiChannelIndex}});
    }

    // Every MIDI edge that lands on `to`, as its source node.
    std::vector<NodeID> midiSourcesOf(NodeID to) {
        std::vector<NodeID> sources;
        for (const auto& c : graph().getConnections())
            if (c.destination.nodeID == to && c.source.isMIDI())
                sources.push_back(c.source.nodeID);
        return sources;
    }

    // The node a MIDI edge into `to` comes from, asserting there is exactly one.
    NodeID soleMidiSource(NodeID to) {
        const auto sources = midiSourcesOf(to);
        EXPECT_EQ(sources.size(), 1u);
        return sources.empty() ? NodeID{} : sources[0];
    }

    bool optionConnected(NodeID target) {
        for (const auto& option : mc->getMidiDestinationOptionsForTest(track))
            if (option.nodeUid == target.uid)
                return option.connected;
        ADD_FAILURE() << "target missing from the options";
        return false;
    }

    // A uuid-keyed fingerprint of the graph's edges (node ids are reassigned when an undo rebuilds the graph).
    std::set<juce::String> edgeFingerprint() {
        const auto uuidOf = [this](NodeID id) { return graph().getNodeForId(id)->properties["uuid"].toString(); };
        std::set<juce::String> edges;
        for (const auto& c : graph().getConnections())
            edges.insert(uuidOf(c.source.nodeID) + ":" + juce::String(c.source.channelIndex) + ">" +
                         uuidOf(c.destination.nodeID) + ":" + juce::String(c.destination.channelIndex));
        return edges;
    }

    void toggle(NodeID target, bool connect) { mc->setMidiDestinationConnectedForTest(track, target.uid, connect); }

    BottomDockActiveTabResetGuardMDT resetGuard;
    std::unique_ptr<MainComponent> mc;
    synth::TrackId track;
    NodeID trackIn;
};

} // namespace

TEST_F(MidiDestinationMacroRoutingTest, ATargetInsideAMacroIsReachedThroughAMintedMidiInlet) {
    const auto target = addOscillator(200);
    const auto spare = addOscillator(500);
    ASSERT_FALSE(group({target, spare}).isEmpty());

    toggle(target, true);

    EXPECT_EQ(midiInlets(), 1);
    EXPECT_FALSE(midiEdge(trackIn, target)) << "no cable skips the macro boundary";
    const auto inlet = soleMidiSource(target);
    ASSERT_NE(inlet, NodeID{});
    EXPECT_TRUE(midiEdge(trackIn, inlet));
}

TEST_F(MidiDestinationMacroRoutingTest, ATrackInAndATargetInDifferentMacrosAreWiredPortToPort) {
    const auto target = addOscillator(200);
    const auto targetSpare = addOscillator(500);
    const auto trackInSpare = addOscillator(800);
    ASSERT_FALSE(group({target, targetSpare}).isEmpty());
    ASSERT_FALSE(group({trackIn, trackInSpare}).isEmpty());

    toggle(target, true);

    EXPECT_EQ(midiOutlets(), 1);
    EXPECT_EQ(midiInlets(), 1);
    EXPECT_FALSE(midiEdge(trackIn, target));
    const auto inlet = soleMidiSource(target);
    const auto outlet = soleMidiSource(inlet);
    EXPECT_EQ(soleMidiSource(outlet), trackIn);
}

TEST_F(MidiDestinationMacroRoutingTest, ATargetInANestedChildMacroGetsAChainOfTwoInlets) {
    const auto target = addOscillator(200);
    const auto childSpare = addOscillator(500);
    const auto parentA = addOscillator(800);
    const auto parentB = addOscillator(1100);
    const auto childId = group({target, childSpare});
    const auto parentId = group({parentA, parentB});
    ASSERT_TRUE(editor().getMacros().setParent(childId, parentId));

    toggle(target, true);

    EXPECT_EQ(midiInlets(), 2);
    const auto childInlet = soleMidiSource(target);
    const auto parentInlet = soleMidiSource(childInlet);
    EXPECT_EQ(soleMidiSource(parentInlet), trackIn);
    EXPECT_FALSE(midiEdge(trackIn, target));
}

TEST_F(MidiDestinationMacroRoutingTest, TheOptionsReportAMacroRoutedTargetAsConnected) {
    const auto inside = addOscillator(200);
    const auto spare = addOscillator(500);
    const auto outside = addOscillator(800);
    ASSERT_FALSE(group({inside, spare}).isEmpty());
    EXPECT_FALSE(optionConnected(inside));

    toggle(inside, true);
    toggle(outside, true);

    EXPECT_FALSE(midiEdge(trackIn, inside));
    EXPECT_TRUE(optionConnected(inside)) << "reached through the inlet, not by a direct cable";
    EXPECT_TRUE(optionConnected(outside));
    EXPECT_FALSE(optionConnected(spare));
}

TEST_F(MidiDestinationMacroRoutingTest, UntickingRemovesTheChainAndItsPorts) {
    const auto target = addOscillator(200);
    const auto spare = addOscillator(500);
    ASSERT_FALSE(group({target, spare}).isEmpty());
    toggle(target, true);
    ASSERT_EQ(midiInlets(), 1);

    toggle(target, false);

    EXPECT_EQ(midiInlets(), 0) << "the stranded inlet is swept";
    EXPECT_TRUE(midiSourcesOf(target).empty());
    EXPECT_FALSE(optionConnected(target));
}

TEST_F(MidiDestinationMacroRoutingTest, UntickingOneOfTwoDestinationsInAMacroKeepsTheSharedInlet) {
    const auto first = addOscillator(200);
    const auto second = addOscillator(500);
    ASSERT_FALSE(group({first, second}).isEmpty());
    toggle(first, true);
    ASSERT_EQ(midiInlets(), 1);
    // A hand-drawn cable makes the one inlet feed both instruments (the routing seam mints one inlet per
    // inside node, so a shared inlet only arises this way).
    const auto inlet = soleMidiSource(first);
    ASSERT_TRUE(graph().addConnection(
        {{inlet, juce::AudioProcessorGraph::midiChannelIndex}, {second, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(optionConnected(second));

    toggle(first, false);

    EXPECT_EQ(midiInlets(), 1);
    EXPECT_FALSE(optionConnected(first));
    EXPECT_TRUE(optionConnected(second));
    EXPECT_TRUE(midiEdge(soleMidiSource(second), second));
}

TEST_F(MidiDestinationMacroRoutingTest, WithAutoPortsOffTheCableIsAPlainDirectEdge) {
    const auto target = addOscillator(200);
    const auto spare = addOscillator(500);
    ASSERT_FALSE(group({target, spare}).isEmpty());
    editor().setAutoCreateMacroPortsOnDragEnabled(false);

    toggle(target, true);

    EXPECT_EQ(midiInlets(), 0);
    EXPECT_TRUE(midiEdge(trackIn, target));
    EXPECT_TRUE(optionConnected(target));
}

TEST_F(MidiDestinationMacroRoutingTest, NoMacroPortNodeAppearsInTheOptions) {
    const auto target = addOscillator(200);
    const auto spare = addOscillator(500);
    ASSERT_FALSE(group({target, spare}).isEmpty());
    toggle(target, true);
    ASSERT_EQ(midiInlets(), 1);

    std::set<juce::uint32> portUids;
    for (auto* node : graph().getNodes())
        if (auto* m = dynamic_cast<ModuleBase*>(node->getProcessor());
            m != nullptr &&
            (m->getModuleType() == ModuleType::MacroMidiInlet || m->getModuleType() == ModuleType::MacroMidiOutlet))
            portUids.insert(node->nodeID.uid);
    ASSERT_EQ(portUids.size(), 1u);

    for (const auto& option : mc->getMidiDestinationOptionsForTest(track))
        EXPECT_EQ(portUids.count(option.nodeUid), 0u) << option.displayName;
}

TEST_F(MidiDestinationMacroRoutingTest, OneUndoRestoresTheGraphAndTheMacrosBeforeTheConnect) {
    const auto target = addOscillator(200);
    const auto spare = addOscillator(500);
    const auto macroId = group({target, spare});
    ASSERT_FALSE(macroId.isEmpty());
    mc->getUndoManager().clearUndoHistory();
    const auto edgesBefore = edgeFingerprint();
    const auto membersBefore = editor().getMacros().find(macroId)->members.size();

    toggle(target, true);
    ASSERT_EQ(midiInlets(), 1);
    ASSERT_TRUE(mc->getUndoManager().undo());

    EXPECT_EQ(edgeFingerprint(), edgesBefore);
    EXPECT_EQ(midiInlets(), 0);
    ASSERT_NE(editor().getMacros().find(macroId), nullptr);
    EXPECT_EQ(editor().getMacros().find(macroId)->members.size(), membersBefore);
}
