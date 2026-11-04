// AutomationLanesCrossTrackMoveTests.cpp -- a mod row moved onto a lane of ANOTHER track, in a real saved project
// (tracks in their own channel macros, one folded, one nested, one open): the moved row still names the real LFO or
// envelope, keeps its controls and amount lane, marks a source that lives outside the lane's macro, and one undo puts
// it back; Cmd+Alt+Up/Down past a lane's end carries the row into the next lane on screen. Also the load path: a
// modulation that crosses a macro boundary reopens with one attenuverter, not two.
#include "../../../UniqueTempDir.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/ConnectionIndex.h"
#include "AudioEngine/ModuleTitle.h"
#include "AutomationLanesCrossTrackFixture.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace {
using synth::test::SavedProjectFixture;
using synth::ui::ModulatorInfo;
using namespace cross_track_test;

int attenuverterCount(MainComponent& mc) {
    int count = 0;
    for (auto* node : mc.getAudioEngine().getGraph().getNodes())
        count += dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr ? 1 : 0;
    return count;
}

// Every routing and cable of the graph as text, printed when a row does not name what it should.
std::string dumpRoutings(MainComponent& mc) {
    auto& graph = mc.getAudioEngine().getGraph();
    const auto name = [&graph](juce::AudioProcessorGraph::NodeID id) {
        auto* n = graph.getNodeForId(id);
        return n != nullptr ? synth::moduleTitle(*n).toStdString() + "#" + std::to_string(id.uid) : std::string("?");
    };
    std::string out;
    for (const auto& r : mc.getAudioEngine().getModulationRoutings())
        out += "routing kind=" + std::to_string((int)r.kind) + " " + name(r.sourceNodeID) + " -> " +
               name(r.destNodeID) + ":" + std::to_string(r.destChannelIndex) + "\n";
    for (const auto& c : graph.getConnections())
        out += "  cable " + name(c.source.nodeID) + ":" + std::to_string(c.source.channelIndex) + " -> " +
               name(c.destination.nodeID) + ":" + std::to_string(c.destination.channelIndex) + "\n";
    return out;
}

} // namespace

// Regression test for FRO1092: the drop crossed two macros (an outlet on the LFO's track, an inlet on the other), and
// the cable from the inlet to the parameter showed as a second, read-only "Macro In" row.
TEST(AutomationLanesCrossTrackMove, AnLfoRowMovedToAnotherTracksLaneIsTheOneLfoRowWithItsControlsAndOneUndoPutsItBack) {
    CrossTrack s;
    ASSERT_NE(s.mc, nullptr);
    const auto own = s.rows(0, "threshold"); // the saved project's own LFO, reopened
    ASSERT_EQ(own.size(), 1u) << dumpRoutings(*s.mc);
    EXPECT_TRUE(own.front().isLfo) << own.front().sourceTitle << "\n" << dumpRoutings(*s.mc);
    const auto before = s.rows(0, "ratio");
    ASSERT_EQ(before.size(), 1u) << dumpRoutings(*s.mc);
    ASSERT_EQ(before.front().sourceUuid, s.lfoUuid);
    EXPECT_TRUE(before.front().sourcePlace.isEmpty()) << "the LFO sits in the lane's own macro";
    ASSERT_TRUE(s.host().canMoveModulatorToLane(before.front(), s.thresholdC));

    ASSERT_TRUE(s.host().moveModulatorToLane(before.front(), s.thresholdC, 0));
    const auto moved = s.rows(2, "threshold");
    ASSERT_EQ(moved.size(), 1u) << "no second row for the cable's leg behind the macro port\n" << dumpRoutings(*s.mc);
    EXPECT_EQ(moved.front().sourceUuid, s.lfoUuid) << moved.front().sourceTitle;
    EXPECT_TRUE(moved.front().isLfo) << "an LFO row: the shape, rate and sync controls";
    EXPECT_TRUE(moved.front().attenuverterUuid.isNotEmpty()) << "an amount to edit";
    EXPECT_EQ(moved.front().sourcePlace, s.macroName(0)) << "the sign names where the LFO lives";
    EXPECT_TRUE(s.host().canChangeModulatorSource(moved.front()));
    EXPECT_TRUE(s.rows(0, "ratio").empty());

    ASSERT_TRUE(s.mc->getUndoManager().undo());
    EXPECT_FALSE(s.mc->getUndoManager().canUndo()) << "ONE step";
    const auto restored = s.rows(0, "ratio");
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored.front().sourceUuid, s.lfoUuid);
    EXPECT_TRUE(s.rows(2, "threshold").empty());
}

TEST(AutomationLanesCrossTrackMove, TheAmountLaneFollowsTheRowAcrossTracks) {
    CrossTrack s;
    ASSERT_NE(s.mc, nullptr);
    const auto row = s.rows(0, "ratio").front();
    const std::vector<synth::AutomationLane::Breakpoint> points{{0.0, 0.25, 0.0f, 0}, {4.0, -0.5, 0.0f, 0}};
    ASSERT_TRUE(synth::ui::writeAmountLane(s.doc(), s.doc().getTracks()[0].id, row.attenuverterUuid, points));
    const auto amountId = synth::ui::amountLaneFor(s.doc(), row.attenuverterUuid)->id;

    ASSERT_TRUE(s.host().moveModulatorToLane(row, s.thresholdC, 0));
    const auto moved = s.rows(2, "threshold");
    ASSERT_EQ(moved.size(), 1u);
    const auto* amount = synth::ui::amountLaneFor(s.doc(), moved.front().attenuverterUuid);
    ASSERT_NE(amount, nullptr) << "the amount lane is keyed by the row's own attenuverter";
    EXPECT_EQ(amount->id, amountId);
    EXPECT_EQ(amount->points.size(), points.size());
}

TEST(AutomationLanesCrossTrackMove, ARowMovedOnIntoANestedTrackAndBackLeavesNoPortsOrAttenuvertersBehind) {
    CrossTrack s;
    ASSERT_NE(s.mc, nullptr);
    const int attenuverters = attenuverterCount(*s.mc);
    const auto nodes = s.mc->getAudioEngine().getGraph().getNumNodes();

    ASSERT_TRUE(s.host().moveModulatorToLane(s.rows(0, "ratio").front(), s.thresholdC, 0));
    const auto onC = s.rows(2, "threshold");
    ASSERT_EQ(onC.size(), 1u);
    ASSERT_TRUE(s.host().moveModulatorToLane(onC.front(), s.thresholdB, 0)) << dumpRoutings(*s.mc);
    const auto onB = s.rows(1, "threshold");
    ASSERT_EQ(onB.size(), 1u) << dumpRoutings(*s.mc);
    EXPECT_EQ(onB.front().sourceUuid, s.lfoUuid);
    EXPECT_EQ(onB.front().sourcePlace, s.macroName(0));
    EXPECT_TRUE(s.rows(2, "threshold").empty()) << dumpRoutings(*s.mc);

    ASSERT_TRUE(s.host().moveModulatorToLane(onB.front(), s.attackA, 0));
    const auto home = s.rows(0, "attack");
    ASSERT_EQ(home.size(), 1u);
    EXPECT_EQ(home.front().sourceUuid, s.lfoUuid);
    EXPECT_TRUE(home.front().sourcePlace.isEmpty()) << "back inside its own macro: no sign";
    EXPECT_EQ(attenuverterCount(*s.mc), attenuverters) << dumpRoutings(*s.mc);
    EXPECT_EQ(s.mc->getAudioEngine().getGraph().getNumNodes(), nodes) << "every port it crossed is gone\n"
                                                                      << dumpRoutings(*s.mc);
}

TEST(AutomationLanesCrossTrackMove, AnEnvelopeRowMovedToAnotherTrackStillNamesTheEnvelope) {
    CrossTrack s;
    ASSERT_NE(s.mc, nullptr);
    auto* comp = nodeByUuid(*s.mc, s.compressor[0]);
    auto& editor = s.mc->getGraphEditor();
    const int raw = editor.modulationChannelFor(comp->nodeID, "attack");
    ASSERT_NE(editor.addModulationSourceModule("ADSR", 0, comp->nodeID, raw, 0.5f, true).uid, 0u);
    const auto rows = s.rows(0, "attack");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(rows.front().isEnvelope);
    s.mc->getUndoManager().clearUndoHistory();

    ASSERT_TRUE(s.host().moveModulatorToLane(rows.front(), s.thresholdC, 0));
    const auto moved = s.rows(2, "threshold");
    ASSERT_EQ(moved.size(), 1u) << dumpRoutings(*s.mc);
    EXPECT_EQ(moved.front().sourceUuid, rows.front().sourceUuid);
    EXPECT_TRUE(moved.front().isEnvelope);
    EXPECT_EQ(moved.front().sourcePlace, s.macroName(0));
    ASSERT_TRUE(s.mc->getUndoManager().undo());
    EXPECT_EQ(s.rows(0, "attack").size(), 1u);
    EXPECT_TRUE(s.rows(2, "threshold").empty());
}

TEST(AutomationLanesCrossTrackMove, ARowMovedBetweenLanesOfTheSameTrackHasNoSign) {
    CrossTrack s;
    ASSERT_NE(s.mc, nullptr);
    ASSERT_TRUE(s.host().moveModulatorToLane(s.rows(0, "ratio").front(), s.attackA, 0));
    const auto moved = s.rows(0, "attack");
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved.front().sourceUuid, s.lfoUuid);
    EXPECT_TRUE(moved.front().sourcePlace.isEmpty());
}

// Regression test for FRO1092: reopening a project whose modulation crosses into a macro wrapped the port-to-parameter
// cable in a second attenuverter, so the row named that attenuverter ("Mod Slot 1") instead of the LFO.
TEST(AutomationLanesCrossTrackMove, AModulationIntoAMacroReopensWithOneAttenuverterAndTheRowNamesTheLfo) {
    const auto file = synth::test::uniqueTempPath("cross-macro-modulation", ".agsproj");
    juce::String compressor, lfoUuid;
    {
        MainComponent mc{std::make_unique<synth::test::SavedProjectFixtureProvider>()};
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        auto& editor = mc.getGraphEditor();
        compressor = SavedProjectFixture::memberUuidOfType(mc, SavedProjectFixture::trackMacroId(mc, 0), "Compr");
        auto lfo = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule("LFO"));
        lfoUuid = synth::AIStateMapper::ensureNodeUuid(lfo.get());
        editor.updateComponents();
        auto* target = nodeByUuid(mc, compressor);
        ASSERT_NE(target, nullptr);
        editor.connectModulationSource(lfo->nodeID, 0, target->nodeID,
                                       editor.modulationChannelFor(target->nodeID, "threshold"), 0.5f);
        ASSERT_EQ(attenuverterCount(mc), 1) << dumpRoutings(mc);
        ASSERT_TRUE(mc.saveProjectForTest(file));
    }
    MainComponent mc{std::make_unique<synth::test::SavedProjectFixtureProvider>()};
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(file));
    EXPECT_EQ(attenuverterCount(mc), 1) << dumpRoutings(mc);
    const auto rows = mc.trackHeaderHostForTest().getModulators(compressor, "threshold");
    ASSERT_EQ(rows.size(), 1u) << dumpRoutings(mc);
    EXPECT_EQ(rows.front().sourceUuid, lfoUuid) << rows.front().sourceTitle;
    EXPECT_EQ(rows.front().sourcePlace, "the main canvas");
    file.deleteRecursively();
}

// The walk back from a direct cable's source passes each port fed by one cable, and stops at a port that is shared.
TEST(ModMatrixEndpointsSourceWalk, ADirectCableOutOfAPortIsWalkedBackToWhatFeedsItAndASharedPortStays) {
    juce::AudioProcessorGraph graph;
    // Stand-ins all: isPort decides what a port is, and a walk never asks a node what it is.
    auto lfo = graph.addNode(std::make_unique<AttenuverterModule>());
    auto other = graph.addNode(std::make_unique<AttenuverterModule>());
    auto portA = graph.addNode(std::make_unique<AttenuverterModule>());
    auto portB = graph.addNode(std::make_unique<AttenuverterModule>());
    graph.addConnection({{lfo->nodeID, 0}, {portA->nodeID, 0}});
    graph.addConnection({{portA->nodeID, 0}, {portB->nodeID, 0}});
    const auto isPort = [&](juce::AudioProcessorGraph::NodeID id) {
        return id == portA->nodeID || id == portB->nodeID;
    };

    std::vector<juce::AudioProcessorGraph::NodeID> passed;
    const auto real =
        synth::ui::realSourceBehindPorts(synth::ConnectionIndex(graph), {portB->nodeID, 0}, isPort, &passed);
    EXPECT_EQ(real.node, lfo->nodeID);
    EXPECT_EQ(passed, (std::vector<juce::AudioProcessorGraph::NodeID>{portB->nodeID, portA->nodeID}));

    graph.addConnection({{other->nodeID, 0}, {portA->nodeID, 1}}); // portA now has two cables in: shared
    const auto shared = synth::ui::realSourceBehindPorts(synth::ConnectionIndex(graph), {portB->nodeID, 0}, isPort);
    EXPECT_EQ(shared.node, portA->nodeID) << "a shared port stays the answer";
}

TEST(AutomationLanesCrossLaneKeyMove, DownPastALanesLastRowCarriesTheRowIntoTheNextLanesOnScreenAcrossTracks) {
    KeyMove s;
    ASSERT_NE(s.mc, nullptr);
    ASSERT_FALSE(s.lfoKeyOn(s.ratioA).isEmpty()) << s.layout();

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.ratioA, s.lfoKeyOn(s.ratioA), 1)) << s.layout();
    EXPECT_FALSE(s.lfoKeyOn(s.attackA).isEmpty()) << "the next lane of the same track first\n" << s.layout();
    EXPECT_EQ(s.lanes().modulatorRowFor(s.attackA, 0)->getInfo().sourceUuid, s.lfoUuid) << "at the lane's top";

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.attackA, s.lfoKeyOn(s.attackA), 1)) << s.layout();
    EXPECT_FALSE(s.lfoKeyOn(s.thresholdB).isEmpty()) << "then the next track's lane\n" << s.layout();
    EXPECT_EQ(s.rows(1, "threshold").front().sourcePlace, s.macroName(0));

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.thresholdB, s.lfoKeyOn(s.thresholdB), 1)) << s.layout();
    ASSERT_FALSE(s.lfoKeyOn(s.thresholdC).isEmpty()) << s.layout();
    EXPECT_FALSE(s.lanes().moveModulatorBy(s.thresholdC, s.lfoKeyOn(s.thresholdC), 1)) << "the last lane: nothing";
    EXPECT_FALSE(s.lfoKeyOn(s.thresholdC).isEmpty());

    ASSERT_TRUE(s.mc->getUndoManager().undo());
    EXPECT_FALSE(s.lfoKeyOn(s.thresholdB).isEmpty()) << "each lane step is one undo step\n" << s.layout();
}

TEST(AutomationLanesCrossLaneKeyMove, UpPastALanesFirstRowLandsAtTheBottomOfTheLaneAboveAndSkipsAClosedTrack) {
    KeyMove s;
    ASSERT_NE(s.mc, nullptr);
    ASSERT_TRUE(s.host().moveModulatorToLane(s.rows(0, "ratio").front(), s.thresholdC, 0));
    s.panel().setTrackAutomationExpanded(s.doc().getTracks()[1].id, false);
    s.panel().refreshModulators();

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.thresholdC, s.lfoKeyOn(s.thresholdC), -1)) << s.layout();
    EXPECT_TRUE(s.lfoKeyOn(s.thresholdB).isEmpty()) << "the middle track's lanes are closed\n" << s.layout();
    ASSERT_FALSE(s.lfoKeyOn(s.attackA).isEmpty()) << s.layout();

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.attackA, s.lfoKeyOn(s.attackA), -1)) << s.layout();
    const int onRatio = s.lanes().modulatorCount(s.ratioA);
    ASSERT_GE(onRatio, 1) << s.layout();
    EXPECT_EQ(s.lanes().modulatorRowFor(s.ratioA, onRatio - 1)->getInfo().sourceUuid, s.lfoUuid) << "at the bottom";
}

TEST(AutomationLanesCrossLaneKeyMove, ALaneWhoseJackAlreadyHasTheSourceIsSkipped) {
    KeyMove s;
    ASSERT_NE(s.mc, nullptr);
    auto* comp = nodeByUuid(*s.mc, s.compressor[0]);
    auto* lfo = nodeByUuid(*s.mc, s.lfoUuid);
    auto& editor = s.mc->getGraphEditor();
    editor.connectModulationSource(lfo->nodeID, 0, comp->nodeID, editor.modulationChannelFor(comp->nodeID, "attack"),
                                   0.5f);
    s.panel().refreshModulators();
    ASSERT_FALSE(s.lfoKeyOn(s.attackA).isEmpty()) << s.layout();
    ASSERT_FALSE(s.host().canMoveModulatorToLane(s.rows(0, "ratio").front(), s.attackA));

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.ratioA, s.lfoKeyOn(s.ratioA), 1)) << s.layout();
    EXPECT_TRUE(s.lfoKeyOn(s.ratioA).isEmpty()) << s.layout();
    EXPECT_EQ(s.rows(0, "attack").size(), 1u) << "Attack keeps its own one routing\n" << s.layout();
    EXPECT_FALSE(s.lfoKeyOn(s.thresholdB).isEmpty()) << s.layout();
}
