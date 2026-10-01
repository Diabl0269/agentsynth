// MixerSendAutomationLaneTests.cpp (docs/mixer/sends-and-buses.md#the-send-and-bus-ui,
// docs/timeline/automation.md): a send level gets its own automation lane through the SAME
// "Add lane..." picker surface a hosted plugin's parameters already use
// (MainComponent::getAvailablePluginLaneOptions / addPluginAutomationLane), never a new lane type
// or an ad-hoc path. Drives the real TimelinePanelComponent headless hooks
// (collectAutomationLaneOptions / applyAutomationLaneMenuChoice) rather than calling
// addPluginAutomationLane directly, so this exercises the same code path a real ComboBox selection
// does (juce::ComboBox/PopupMenu don't run headlessly -- see TimelinePanelComponent.h's own
// "Headless hooks" section).
//
// Groups:
//   1. PickerOffersOnlyActiveSends -- an inactive slot is never offered; activating one adds
//      exactly one "Add lane..." entry, labelled like the send knob's own accessible title.
//   2. PickerDropsOnlyTheRemovedSlot -- the sparse-slot rule (docs/mixer/sends-and-buses.md#slots-are-sparse)
//      applied to the collector: removing slot 0 drops ONLY its entry, slot 1's stays.
//   3. ChoosingTheEntryCreatesABoundLane -- selecting it creates a lane keyed on (strip uuid,
//      "send1Level") with the parameter's real -60..+12 dB range, and re-selecting is a no-op.
//   4. RemovingTheSlotLeavesTheLaneBoundNotOrphaned -- sends-and-buses.md's claim that a removed
//      slot's lane is left alone (the parameter itself never goes away) rather than orphaned.

#include "../../UI/MidiRemote/MidiRemoteMockProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineReconciler.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// getAvailablePluginLaneOptions() only ever offers a node that ALREADY carries a uuid (real usage:
// a track's linked Channel Strip gets one from the channel-creation flow) -- it never assigns one
// itself, unlike automateParameter()'s ensure-uuid. A bare graph.addNode() in a test has none, so
// this mirrors ModuleBase::setNodeUuid's mirror-into-processor idiom (Source/CLAUDE.md) directly.
NodeID addStripWithUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    auto node = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"));
    if (node == nullptr)
        return {};
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node->nodeID;
}

// Finds the "Add lane..." entry for (uuid, paramId) in the panel's own combo-index convention
// (index i -> menu id i + 1, TimelinePanelComponent.h's AutomationLaneOption contract) — returns
// -1 if there is none.
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

// getAvailablePluginLaneOptions() itself is a private TrackHeaderHost override on MainComponent
// (private inheritance, MainComponent.h), so a test reaches the SAME data these tests need to
// inspect through the panel's own public collectAutomationLaneOptions() -- the "Add lane..." half
// of it, unwrapped back to PluginLaneOption. This is the real UI path anyway (the combo re-runs
// this exact call at click time, docs/timeline/automation.md).
std::vector<synth::ui::TrackHeaderHost::PluginLaneOption>
collectAddLaneOptions(const synth::ui::TimelinePanelComponent& panel) {
    std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> result;
    for (const auto& option : panel.collectAutomationLaneOptions())
        if (option.isAddEntry)
            result.push_back(option.addOption);
    return result;
}

} // namespace

TEST(MixerSendAutomationLaneTest, PickerOffersOnlyActiveSends) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();

    const juce::String sourceUuid = "c0900000-0000-0000-0000-000000000001";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto bus = addStripWithUuid(graph, "c0900000-0000-0000-0000-000000000002");
    ASSERT_NE(source, NodeID{});
    ASSERT_NE(bus, NodeID{});
    auto* busStrip = dynamic_cast<ChannelStripModule*>(graph.getNodeForId(bus)->getProcessor());
    ASSERT_NE(busStrip, nullptr);
    busStrip->setIsBus(true);

    // No active sends yet: the picker offers nothing for this strip at all.
    EXPECT_EQ(findAddLaneMenuId(mc.getTimelinePanel(), sourceUuid, "send1Level"), -1);
    for (auto& option : collectAddLaneOptions(mc.getTimelinePanel()))
        EXPECT_NE(option.paramId, "send1Level") << "an inactive slot must never be offered";

    ASSERT_EQ(synth::addSend(graph, source, bus), 0);

    const auto options = collectAddLaneOptions(mc.getTimelinePanel());
    const auto found = std::find_if(options.begin(), options.end(), [&](const auto& option) {
        return option.nodeUuid == sourceUuid && option.paramId == "send1Level";
    });
    ASSERT_NE(found, options.end()) << "activating slot 0 must add exactly one entry for it";
    EXPECT_EQ(found->label, "Send to Bus 1") << "same FRO301 naming the send knob's accessible title uses";

    // The same active slot also offers its pan.
    const auto pan = std::find_if(options.begin(), options.end(), [&](const auto& option) {
        return option.nodeUuid == sourceUuid && option.paramId == "send1Pan";
    });
    ASSERT_NE(pan, options.end());
    EXPECT_EQ(pan->label, "Send to Bus 1 (pan)");

    // slot 1 (send2Level / send2Pan) is still inactive -- still not offered.
    for (auto& option : options) {
        EXPECT_NE(option.paramId, "send2Level");
        EXPECT_NE(option.paramId, "send2Pan");
    }
}

TEST(MixerSendAutomationLaneTest, PickerDropsOnlyTheRemovedSlot) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();

    const juce::String sourceUuid = "c0900000-0000-0000-0000-000000000006";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto bus = addStripWithUuid(graph, "c0900000-0000-0000-0000-000000000007");
    ASSERT_NE(source, NodeID{});
    ASSERT_NE(bus, NodeID{});
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(bus)->getProcessor())->setIsBus(true);

    ASSERT_EQ(synth::addSend(graph, source, bus), 0);
    ASSERT_EQ(synth::addSend(graph, source, bus), 1);

    auto hasOption = [&](const juce::String& paramId) {
        for (const auto& option : collectAddLaneOptions(mc.getTimelinePanel()))
            if (option.nodeUuid == sourceUuid && option.paramId == paramId)
                return true;
        return false;
    };
    ASSERT_TRUE(hasOption("send1Level"));
    ASSERT_TRUE(hasOption("send2Level"));

    // docs/mixer/sends-and-buses.md#slots-are-sparse: removing slot 0 clears only its own bit --
    // slot 1 keeps its own raw channels and its own offer untouched.
    ASSERT_TRUE(synth::removeSend(graph, source, 0));
    EXPECT_FALSE(hasOption("send1Level")) << "a freed slot must drop out of the picker";
    EXPECT_TRUE(hasOption("send2Level")) << "a higher slot must not be disturbed by removing a lower one";
}

TEST(MixerSendAutomationLaneTest, ChoosingTheEntryCreatesABoundLane) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();

    const juce::String uuid = "c0900000-0000-0000-0000-000000000003";
    const auto source = addStripWithUuid(graph, uuid);
    const auto bus = addStripWithUuid(graph, "c0900000-0000-0000-0000-000000000004");
    ASSERT_NE(source, NodeID{});
    ASSERT_NE(bus, NodeID{});
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(bus)->getProcessor())->setIsBus(true);
    ASSERT_EQ(synth::addSend(graph, source, bus), 0);

    auto& panel = mc.getTimelinePanel();
    const int menuId = findAddLaneMenuId(panel, uuid, "send1Level");
    ASSERT_GT(menuId, 0);

    panel.applyAutomationLaneMenuChoice(menuId);

    auto& doc = mc.getTimelineDoc();
    const auto* lane = doc.getLaneForParam(uuid, "send1Level");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->nodeUuid, uuid);
    EXPECT_EQ(lane->paramId, "send1Level");

    auto* strip = dynamic_cast<ChannelStripModule*>(graph.getNodeForId(source)->getProcessor());
    ASSERT_NE(strip, nullptr);
    auto* sendParam = strip->getSendLevelParameter(0);
    ASSERT_NE(sendParam, nullptr);
    EXPECT_FLOAT_EQ(lane->range.minValue, sendParam->getNormalisableRange().start)
        << "a real RangedAudioParameter's range, not the hosted-plugin {0, 1} convention";
    EXPECT_FLOAT_EQ(lane->range.maxValue, sendParam->getNormalisableRange().end);

    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(doc.getTrackForLane(lane->id)->id))
        << "the new lane's track folds open to show it";
    EXPECT_EQ(panel.getSelectedAutomationLane(), lane->id);

    // The lane already exists: re-collecting the picker must no longer offer an "Add lane..." entry
    // for it (the doc-wide one-lane-per-parameter rule), and it must show up as an existing lane.
    EXPECT_EQ(findAddLaneMenuId(panel, uuid, "send1Level"), -1);
    int existingLaneEntries = 0;
    for (const auto& option : panel.collectAutomationLaneOptions())
        if (!option.isAddEntry && option.id == lane->id)
            ++existingLaneEntries;
    EXPECT_EQ(existingLaneEntries, 1);

    const int laneCountBefore = (int)doc.getTrackForLane(lane->id)->lanes.size();
    for (auto& option : collectAddLaneOptions(panel))
        EXPECT_NE(option.paramId, "send1Level") << "already automated -- no second offer";
    EXPECT_EQ((int)doc.getTrackForLane(lane->id)->lanes.size(), laneCountBefore);

    // sends-and-buses.md: removing the slot leaves the lane bound to now-inert extra state rather
    // than orphaning it -- sendNLevel is one of ChannelStripModule's own parameters and it never
    // goes away (ChannelStripModule declares all four unconditionally), so TimelineReconciler's
    // "the node resolving is the whole story for orphaning" rule for our own modules
    // (Source/Timeline/TimelineReconciler.cpp) never even reaches the removed-slot case.
    ASSERT_TRUE(synth::removeSend(graph, source, 0));
    synth::TimelineReconciler::reconcile(doc, graph);
    const auto* laneAfterRemove = doc.getLaneForParam(uuid, "send1Level");
    ASSERT_NE(laneAfterRemove, nullptr) << "the lane is retained, never auto-deleted";
    EXPECT_FALSE(laneAfterRemove->orphaned);
}

TEST(MixerSendAutomationLaneTest, PickerNamesABoxedBusByItsMacroName) {
    MainComponent mc(std::make_unique<MidiRemoteMockProvider>());
    auto& graph = mc.getAudioEngine().getGraph();

    const juce::String sourceUuid = "c0900000-0000-0000-0000-00000000000a";
    const juce::String busUuid = "c0900000-0000-0000-0000-00000000000b";
    const auto source = addStripWithUuid(graph, sourceUuid);
    const auto bus = addStripWithUuid(graph, busUuid);
    ASSERT_NE(source, NodeID{});
    ASSERT_NE(bus, NodeID{});
    dynamic_cast<ChannelStripModule*>(graph.getNodeForId(bus)->getProcessor())->setIsBus(true);

    // A renamed, boxed bus: the mixer row names it by its macro, so the lane picker must too.
    synth::Macro macro;
    macro.name = "Plate Verb";
    macro.members = {busUuid};
    mc.getGraphEditor().getMacros().add(macro);

    ASSERT_EQ(synth::addSend(graph, source, bus), 0);
    const auto options = collectAddLaneOptions(mc.getTimelinePanel());
    const auto found = std::find_if(options.begin(), options.end(), [&](const auto& option) {
        return option.nodeUuid == sourceUuid && option.paramId == "send1Level";
    });
    ASSERT_NE(found, options.end());
    EXPECT_EQ(found->label, "Send to Plate Verb");
}
