// MixerSendReorderMidiLearnTests.cpp (docs/mixer/sends-and-buses.md#reordering-sends,
// docs/control/midi-remote.md): a MIDI Learn mapping on a send's level/pan follows the send
// through a reorder, in the SAME undo step as the cables and the automation lane
// (MixerSendReorderLaneTests.cpp covers the lane half). Same MainComponent-backed rig as that
// file, plus the real fake-CC-injection idiom E2EPluginCardWorkflowTests.cpp's
// MidiLearnOnAHostedKnobThenAFakeCcDrivesTheParameter uses, so a regression in the live
// republish (MixerPanelComponent::onPublishMidiRemoteAssignments) fails a test here too, not
// just a check of MidiRemoteProjectDoc's own assignment list. Reads the doc's state through
// MidiLearnController::queryMappings() (paramId -> label), same accessor the MIDI Remote panel
// itself uses, rather than a test-only doc getter on MainComponent.
//
// Groups:
//   1. MidiLearnMappingFollowsItsSendAcrossAReorder -- a mapping on send1Level ends up on
//      send2Level, and a fake CC after the reorder drives the send that's now in slot 1 (not the
//      one left behind in slot 0).
//   2. ReorderIsOneUndoStepAndUndoRestoresTheMidiMappingToo -- one undo() restores cables, the
//      lane AND the MIDI mapping together; redo() re-applies all three together.

#include "../../UI/MidiRemote/MidiRemoteMockProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>

#include <chrono>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

constexpr const char* kFakeDevice = "fro296-fake-controller";
constexpr int kCc = 30;

// Same idiom MixerSendReorderLaneTests.cpp uses -- a bare graph.addNode() carries no uuid until
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

// Settles a fresh learn onto a real wall-clock RemoteEngine (mainComp_'s, unlike
// MidiLearnControllerTests.cpp's fake-clock fixture) -- mirrors
// MidiLearnOnAHostedKnobThenAFakeCcDrivesTheParameter's own wait loop exactly.
void settleLearn(MainComponent& mc, synth::midi::MidiLearnController& controller, synth::midi::RemoteEngine& remote) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (controller.isArmed() && std::chrono::steady_clock::now() < deadline) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        remote.drain();
    }
    ASSERT_FALSE(controller.isArmed()) << "the learn settled and bound";
}

} // namespace

TEST(MixerSendReorderMidiLearnTest, MidiLearnMappingFollowsItsSendAcrossAReorder) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();
    auto& panel = mc.getBottomDock().getMixerPanel();
    auto& controller = mc.getMidiLearnControllerForTest();
    auto& remote = mc.getRemoteEngineForTest();

    const juce::String sourceUuid = "fa296100-0000-0000-0000-000000000001";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto busA = addStripWithUuid(graph, "fa296100-0000-0000-0000-0000000000a1");
    const auto busB = addStripWithUuid(graph, "fa296100-0000-0000-0000-0000000000a2");
    ASSERT_NE(source, NodeID{});
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busA)->getProcessor())->setIsBus(true);
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busB)->getProcessor())->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, source, busA), 0);
    ASSERT_EQ(synth::addSend(graph, source, busB), 1);

    // Learn a fake CC onto slot 0's level (send1Level), same fake-device idiom
    // MidiLearnOnAHostedKnobThenAFakeCcDrivesTheParameter uses.
    controller.arm(source, "send1Level");
    ASSERT_TRUE(controller.isArmed());
    remote.setSources({juce::String(kFakeDevice)});
    remote.setDefaultTakeover(synth::Takeover::jump);
    mc.getAudioEngine().handleIncomingMidiMessageFromSource(kFakeDevice,
                                                            juce::MidiMessage::controllerEvent(1, kCc, 64));
    remote.drain();
    settleLearn(mc, controller, remote);

    ASSERT_EQ(controller.queryMappings(source).size(), 1u);
    EXPECT_TRUE(controller.queryMappings(source).count("send1Level"));

    // Row 0 (slot 0, -> bus A) moves to row 1 (slot 1, -> bus B) -- a plain adjacent swap.
    ASSERT_TRUE(panel.moveSendRow(source, 0, 1));
    EXPECT_EQ(synth::findSendTarget(graph, source, 1), busA);
    EXPECT_EQ(synth::findSendTarget(graph, source, 0), busB);

    // The mapping followed the send: no more mapping on send1Level, one on send2Level, still on the
    // very same node -- queryMappings() reads straight off MidiRemoteProjectDoc::assignments
    // (MidiLearnController.cpp), so this is the doc's own state, not a UI-layer cache.
    const auto mappingsAfter = controller.queryMappings(source);
    ASSERT_EQ(mappingsAfter.size(), 1u);
    EXPECT_FALSE(mappingsAfter.count("send1Level"));
    EXPECT_TRUE(mappingsAfter.count("send2Level"))
        << "the mapping must move to the slot the send is now in, not stay behind on slot 0";

    // The live resolver (RemoteEngine's own published cache, republished by
    // MixerPanelComponent::onPublishMidiRemoteAssignments right after moveSendRow's own edit) must
    // already reflect this -- a fake CC now drives slot 1's level (the send that moved), not slot 0's
    // (the one left behind).
    auto* strip = stripAt(graph, source);
    auto* level0 = strip->getSendLevelParameter(0);
    auto* level1 = strip->getSendLevelParameter(1);
    ASSERT_NE(level0, nullptr);
    ASSERT_NE(level1, nullptr);
    const float before0 = level0->get();
    const float before1 = level1->get();

    mc.getAudioEngine().handleIncomingMidiMessageFromSource(kFakeDevice, juce::MidiMessage::controllerEvent(1, kCc, 0));
    remote.drain();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the drain's apply is a real gesture

    EXPECT_NE(level1->get(), before1) << "the fake CC now drives slot 1, where the mapped send now lives";
    EXPECT_FLOAT_EQ(level0->get(), before0) << "slot 0 (the send left behind) must not move";

    // Close the still-open change gesture before mc (and the graph under it) tears down, same
    // reasoning as MidiLearnOnAHostedKnobThenAFakeCcDrivesTheParameter's own closing comment.
    const auto idleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < idleDeadline)
        remote.drain();
}

TEST(MixerSendReorderMidiLearnTest, ReorderIsOneUndoStepAndUndoRestoresTheMidiMappingToo) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();
    auto& panel = mc.getBottomDock().getMixerPanel();
    auto& controller = mc.getMidiLearnControllerForTest();
    auto& remote = mc.getRemoteEngineForTest();

    const juce::String sourceUuid = "fa296100-0000-0000-0000-000000000002";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto busA = addStripWithUuid(graph, "fa296100-0000-0000-0000-0000000000b1");
    const auto busB = addStripWithUuid(graph, "fa296100-0000-0000-0000-0000000000b2");
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busA)->getProcessor())->setIsBus(true);
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(busB)->getProcessor())->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, source, busA), 0);
    ASSERT_EQ(synth::addSend(graph, source, busB), 1);

    controller.arm(source, "send1Level");
    remote.setSources({juce::String(kFakeDevice)});
    remote.setDefaultTakeover(synth::Takeover::jump);
    mc.getAudioEngine().handleIncomingMidiMessageFromSource(kFakeDevice,
                                                            juce::MidiMessage::controllerEvent(1, kCc, 64));
    remote.drain();
    settleLearn(mc, controller, remote);
    ASSERT_EQ(controller.queryMappings(source).size(), 1u);
    const auto label = controller.queryMappings(source).at("send1Level");

    // Also carries a lane on the same parameter, so this test proves all three domains (graph,
    // timeline lane, MIDI mapping) revert and re-apply together, not just the MIDI half in
    // isolation.
    auto& timelinePanel = mc.getTimelinePanel();
    auto& doc = mc.getTimelineDoc();
    const auto options = timelinePanel.collectAutomationLaneOptions();
    int menuId = -1;
    for (int i = 0; i < (int)options.size(); ++i)
        if (options[(size_t)i].isAddEntry && options[(size_t)i].addOption.nodeUuid == sourceUuid &&
            options[(size_t)i].addOption.paramId == "send1Level")
            menuId = i + 1;
    ASSERT_GT(menuId, 0);
    timelinePanel.applyAutomationLaneMenuChoice(menuId);
    const auto laneId = doc.getLaneForParam(sourceUuid, "send1Level")->id;

    ASSERT_TRUE(panel.moveSendRow(source, 0, 1));
    EXPECT_EQ(synth::findSendTarget(graph, source, 1), busA);
    EXPECT_TRUE(controller.queryMappings(source).count("send2Level"));
    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send2Level")->id, laneId);

    // ONE undo() call must restore graph, timeline lane AND the MIDI mapping together -- the whole
    // point of recordGraphTimelineAndMacroChange now also capturing the MidiRemoteProjectDoc
    // domain (AppUndoManager.cpp's own comment on the method).
    ASSERT_TRUE(mc.getUndoManager().undo());

    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send1Level")->id, laneId) << "the lane binding must revert";
    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send2Level"), nullptr);

    const auto mappingsAfterUndo = controller.queryMappings(source);
    ASSERT_EQ(mappingsAfterUndo.size(), 1u);
    ASSERT_TRUE(mappingsAfterUndo.count("send1Level"))
        << "the MIDI mapping must revert to slot 0 together with the graph and the lane";
    EXPECT_EQ(mappingsAfterUndo.at("send1Level"), label) << "the same mapping, not a new one";

    // redo() must re-apply all three domains together, the same as they were right after
    // moveSendRow.
    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(synth::findSendTarget(graph, source, 1), busA);
    EXPECT_EQ(doc.getLaneForParam(sourceUuid, "send2Level")->id, laneId);
    EXPECT_TRUE(controller.queryMappings(source).count("send2Level"))
        << "redo must re-apply the MIDI mapping's move too, not just the graph and lane";

    // Close the still-open change gesture before mc (and the graph under it) tears down.
    const auto idleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < idleDeadline)
        remote.drain();
}
