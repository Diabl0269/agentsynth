// MixerSendReorderLaneTests.cpp -- FRO296 (docs/mixer/sends-and-buses.md#reordering-sends,
// docs/timeline/automation.md): the caller-level half of a send reorder -- MixerPanelComponent's
// moveSendRow(), which is the ONE place graph, TimelineDoc and macros are all reachable together,
// so the slot swap and its lane rebind land in a single AppUndoManager::recordGraphTimelineAndMacroChange
// step. Drives the real MainComponent-backed panel, same rig style as MixerSendAutomationLaneTests.cpp.
//
// Groups:
//   1. LaneFollowsItsSendAcrossAReorder -- a lane on send1Level ends up bound to send2Level, its
//      points intact, and still resolves to the parameter now driving the moved send.
//   2. ReorderIsOneUndoStepAndUndoRestoresEverything -- cables, values and the lane binding all
//      revert together on a single undo().
//   3. SaveReloadRoundTripKeepsTheNewOrder -- the graph's own graphToJSON/applyJSONToGraph
//      round trip (the real save/reload path) and the TimelineDoc's toVar/fromVar round trip both
//      keep the swapped slots' cables, values and the lane's new paramId.

#include "../../UI/MidiRemote/MidiRemoteMockProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Same idiom MixerSendAutomationLaneTests.cpp uses -- a bare graph.addNode() carries no uuid until
// mirrored into the processor (ModuleBase::setNodeUuid, Source/CLAUDE.md).
NodeID addStripWithUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    auto node = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"));
    if (node == nullptr)
        return {};
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node->nodeID;
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

NodeID findNodeByUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

int findAddLaneMenuId(const synth::ui::TimelinePanelComponent& panel, const juce::String& uuid,
                      const juce::String& paramId) {
    const auto options = panel.collectAutomationLaneOptions();
    for (int i = 0; i < (int)options.size(); ++i) {
        const auto& option = options[(size_t)i];
        if (option.isAddEntry && option.addOption.nodeUuid == uuid && option.addOption.paramId == paramId)
            return i + 1;
    }
    return -1;
}

} // namespace

TEST(MixerSendReorderLaneTest, LaneFollowsItsSendAcrossAReorder) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();
    auto& panel = mc.getBottomDock().getMixerPanel();
    auto& doc = mc.getTimelineDoc();

    const juce::String sourceUuid = "fa296000-0000-0000-0000-000000000001";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto busA = addStripWithUuid(graph, "fa296000-0000-0000-0000-0000000000a1");
    const auto busB = addStripWithUuid(graph, "fa296000-0000-0000-0000-0000000000a2");
    ASSERT_NE(source, NodeID{});
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busA)->getProcessor())->setIsBus(true);
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busB)->getProcessor())->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, source, busA), 0);
    ASSERT_EQ(synth::addSend(graph, source, busB), 1);

    // Create a lane on slot 0's level through the real "Add lane..." picker path, same as
    // MixerSendAutomationLaneTests.cpp's own ChoosingTheEntryCreatesABoundLane.
    auto& timelinePanel = mc.getTimelinePanel();
    const int menuId = findAddLaneMenuId(timelinePanel, sourceUuid, "send1Level");
    ASSERT_GT(menuId, 0);
    timelinePanel.applyAutomationLaneMenuChoice(menuId);
    const auto* laneBefore = doc.getLaneForParam(sourceUuid, "send1Level");
    ASSERT_NE(laneBefore, nullptr);
    const auto laneId = laneBefore->id;

    ASSERT_TRUE(doc.addBreakpoint(laneId, 0.0, -6.0));
    ASSERT_TRUE(doc.addBreakpoint(laneId, 4.0, -18.0));
    ASSERT_EQ(doc.getLane(laneId)->points.size(), 2u);

    // Row 0 (slot 0, -> bus A) moves to row 1 (slot 1, -> bus B) -- a plain adjacent swap.
    ASSERT_TRUE(panel.moveSendRow(source, 0, 1));

    // The physical send moved: slot 1 now feeds bus A.
    EXPECT_EQ(synth::findSendTarget(graph, source, 1), busA);
    EXPECT_EQ(synth::findSendTarget(graph, source, 0), busB);

    // The lane followed it: no more lane on send1Level, a lane on send2Level with the same points.
    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send1Level"), nullptr);
    const auto* laneAfter = doc.getLaneForParam(sourceUuid, "send2Level");
    ASSERT_NE(laneAfter, nullptr);
    EXPECT_EQ(laneAfter->id, laneId) << "the SAME lane object is rebound, not deleted and recreated";
    ASSERT_EQ(laneAfter->points.size(), 2u) << "its points must survive the rebind";
    EXPECT_DOUBLE_EQ(laneAfter->points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(laneAfter->points[1].beat, 4.0);

    // And it still drives the send that's now in slot 1 -- getSendLevelParameter(1) is the
    // parameter object "send2Level" names.
    auto* strip = stripAt(graph, source);
    EXPECT_EQ(ChannelStripModule::getSendLevelParameterId(1), laneAfter->paramId);
    EXPECT_NE(strip->getSendLevelParameter(1), nullptr);
}

TEST(MixerSendReorderLaneTest, ReorderIsOneUndoStepAndUndoRestoresEverything) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();
    auto& panel = mc.getBottomDock().getMixerPanel();
    auto& doc = mc.getTimelineDoc();

    const juce::String sourceUuid = "fa296000-0000-0000-0000-000000000002";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto busA = addStripWithUuid(graph, "fa296000-0000-0000-0000-0000000000b1");
    const auto busB = addStripWithUuid(graph, "fa296000-0000-0000-0000-0000000000b2");
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busA)->getProcessor())->setIsBus(true);
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busB)->getProcessor())->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, source, busA), 0);
    ASSERT_EQ(synth::addSend(graph, source, busB), 1);

    auto& timelinePanel = mc.getTimelinePanel();
    const int menuId = findAddLaneMenuId(timelinePanel, sourceUuid, "send1Level");
    ASSERT_GT(menuId, 0);
    timelinePanel.applyAutomationLaneMenuChoice(menuId);
    const auto laneId = doc.getLaneForParam(sourceUuid, "send1Level")->id;
    ASSERT_TRUE(doc.addBreakpoint(laneId, 2.0, -3.0));

    ASSERT_TRUE(panel.moveSendRow(source, 0, 1));
    EXPECT_EQ(synth::findSendTarget(graph, source, 1), busA);
    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send2Level")->id, laneId);

    // ONE undo() call must restore graph, timeline AND macro domains together -- the whole point of
    // recordGraphTimelineAndMacroChange's single transaction (no beginNewTransaction between the
    // graph/macro push and the timeline push, AppUndoManager.cpp's own comment on the method).
    ASSERT_TRUE(mc.getUndoManager().undo());

    // Cables restored.
    EXPECT_EQ(synth::findSendTarget(graph, findNodeByUuid(graph, sourceUuid), 0),
              findNodeByUuid(graph, "fa296000-0000-0000-0000-0000000000b1"));
    // Lane restored to its original binding, with its point intact.
    const auto* laneAfterUndo = doc.getLaneForParam(sourceUuid, "send1Level");
    ASSERT_NE(laneAfterUndo, nullptr) << "the lane binding must revert with the graph";
    EXPECT_EQ(laneAfterUndo->id, laneId);
    ASSERT_EQ(laneAfterUndo->points.size(), 1u);
    EXPECT_DOUBLE_EQ(laneAfterUndo->points[0].beat, 2.0);
    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send2Level"), nullptr);
}

TEST(MixerSendReorderLaneTest, SaveReloadRoundTripKeepsTheNewOrder) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();
    auto& panel = mc.getBottomDock().getMixerPanel();
    auto& doc = mc.getTimelineDoc();

    const juce::String sourceUuid = "fa296000-0000-0000-0000-000000000003";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto busA = addStripWithUuid(graph, "fa296000-0000-0000-0000-0000000000c1");
    const auto busB = addStripWithUuid(graph, "fa296000-0000-0000-0000-0000000000c2");
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busA)->getProcessor())->setIsBus(true);
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busB)->getProcessor())->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, source, busA), 0);
    ASSERT_EQ(synth::addSend(graph, source, busB), 1);
    stripAt(graph, source)
        ->getSendLevelParameter(0)
        ->setValueNotifyingHost(
            stripAt(graph, source)->getSendLevelParameter(0)->getNormalisableRange().convertTo0to1(-9.0f));

    auto& timelinePanel = mc.getTimelinePanel();
    const int menuId = findAddLaneMenuId(timelinePanel, sourceUuid, "send1Level");
    ASSERT_GT(menuId, 0);
    timelinePanel.applyAutomationLaneMenuChoice(menuId);
    const auto laneId = doc.getLaneForParam(sourceUuid, "send1Level")->id;
    ASSERT_TRUE(doc.addBreakpoint(laneId, 1.0, -1.0));

    ASSERT_TRUE(panel.moveSendRow(source, 0, 1)); // slot 1 now -> bus A, send2Level now holds the lane

    // The real save/reload path: graphToJSON/applyJSONToGraph(trusted=true) for the graph (own
    // output replayed, Source/CLAUDE.md's trusted=true rule) and toVar/fromVar for the timeline.
    const auto graphJson = synth::AIStateMapper::graphToJSON(graph);
    const auto timelineJson = doc.toVar();

    juce::AudioProcessorGraph reloadedGraph;
    reloadedGraph.setPlayConfigDetails(2, 2, 48000.0, 64);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(graphJson, reloadedGraph, true, true, false));
    synth::TimelineDoc reloadedDoc;
    ASSERT_TRUE(reloadedDoc.fromVar(timelineJson));

    const auto reloadedSource = findNodeByUuid(reloadedGraph, sourceUuid);
    const auto reloadedBusA = findNodeByUuid(reloadedGraph, "fa296000-0000-0000-0000-0000000000c1");
    const auto reloadedBusB = findNodeByUuid(reloadedGraph, "fa296000-0000-0000-0000-0000000000c2");
    ASSERT_NE(reloadedSource, NodeID{});

    EXPECT_EQ(synth::findSendTarget(reloadedGraph, reloadedSource, 1), reloadedBusA)
        << "the swapped order survives the round trip";
    EXPECT_EQ(synth::findSendTarget(reloadedGraph, reloadedSource, 0), reloadedBusB);

    auto* reloadedStrip = stripAt(reloadedGraph, reloadedSource);
    ASSERT_NE(reloadedStrip, nullptr);
    EXPECT_NEAR(reloadedStrip->getSendLevelParameter(1)->get(), -9.0f, 0.05f)
        << "the level value moved with the slot and survives the round trip";

    const auto* reloadedLane = reloadedDoc.getLaneForParam(sourceUuid, "send2Level");
    ASSERT_NE(reloadedLane, nullptr) << "the rebound lane survives the round trip";
    ASSERT_EQ(reloadedLane->points.size(), 1u);
    EXPECT_DOUBLE_EQ(reloadedLane->points[0].beat, 1.0);
    EXPECT_EQ(reloadedDoc.getLaneForParam(sourceUuid, "send1Level"), nullptr);
}
