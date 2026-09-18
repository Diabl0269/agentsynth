// ChannelFlowCreateChannelsTests.cpp
//
// "Create Channels" for existing projects (FRO26, P9-3e, docs/mixer/mixer.md#creating-channels-in-an-existing-project):
// wraps every channel-less track's chain into a strip as one undo step, with no automatic migration on load. Uses the
// ChannelFlowTest fixture from ChannelFlowTestFixture.h.

#include "../../StubPluginInstance.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <thread>

// -------------------------------------------------------------------------------------------
// "Create Channels" for existing projects (FRO26, P9-3e, docs/mixer/mixer.md#creating-channels-in-an-existing-project)
//
// docs/mixer/mixer.md#creating-channels-in-an-existing-project: an old project opens UNCHANGED -- no automatic
// migration on load. The
// "+ Track" menu's "Create Channels" entry (TimelinePanelComponent::kCreateChannelsMenuId, driven
// here through the same applyAddTrackMenuChoice() headless seam addAudioTrack()/addInstrumentTrack()
// use above) wraps every channel-less track's chain into a strip, as ONE undo step covering all of
// them. These tests build the "old project" starting state directly -- a "Track Audio" or
// "Track In" -> instrument chain wired straight to the output, exactly what addAudioTrack()
// produced before T173a and what a real .agsproj predating this feature still loads as, since
// nothing here ever migrates it automatically -- the same "build the pre-existing state directly"
// pattern the AutoChannelOnConnect_* tests above use for T184's own "no channel yet" starting point.
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, CreateChannelsWrapsEveryChannellessTrackAsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);

    // Legacy audio track: "Track Audio" wired straight to the output, no insert chain.
    juce::String trackAudioUuid;
    auto* trackAudioNode = addPlainNodeCFT(graph, "Track Audio", {50, 50}, trackAudioUuid);
    ASSERT_NE(trackAudioNode, nullptr);
    graph.addConnection({{trackAudioNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{trackAudioNode->nodeID, 1}, {output->nodeID, 1}});
    const auto audioTrackId = doc.addTrack(synth::TrackKind::Audio, "Legacy Audio");
    ASSERT_TRUE(audioTrackId.isValid());
    ASSERT_TRUE(doc.setTrackBinding(audioTrackId, trackAudioUuid));

    // Legacy instrument track: Track In -> Oscillator wired straight to the output -- the same
    // starting shape the AutoChannelOnConnect_* tests above build for T184's own case.
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto* trackInNode = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackInNode, nullptr);
    juce::String instrumentUuid;
    auto* instrumentNode = addPlainNodeCFT(graph, "Oscillator", {300, 300}, instrumentUuid);
    ASSERT_NE(instrumentNode, nullptr);
    graph.addConnection({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    graph.addConnection({{instrumentNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrumentNode->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0)
        << "opens unchanged -- no channel exists yet, on either track";

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2)
        << "both channel-less tracks must have gotten their own strip";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1) << "Master is a singleton shared by both";
    EXPECT_FALSE(graph.isConnected({{trackAudioNode->nodeID, 0}, {output->nodeID, 0}}))
        << "the audio track's straight-to-output feed must be re-routed through its new channel";
    EXPECT_FALSE(graph.isConnected({{trackAudioNode->nodeID, 1}, {output->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 0}, {output->nodeID, 0}}))
        << "the instrument track's straight-to-output feed must be re-routed through its new channel";
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 1}, {output->nodeID, 1}}));

    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "ONE undo must remove BOTH new channels together";
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosBefore);

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, CreateChannelsLeavesAlreadyChanneledTracksUntouched) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();

    // An already-channeled track, built the normal way.
    addAudioTrack(mc);
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    auto* existingStrip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(existingStrip, nullptr);
    const juce::String existingStripUuid = nodeUuid(existingStrip);
    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);

    // A second, channel-less legacy audio track.
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    juce::String legacyUuid;
    auto* legacyNode = addPlainNodeCFT(graph, "Track Audio", {800, 50}, legacyUuid);
    ASSERT_NE(legacyNode, nullptr);
    graph.addConnection({{legacyNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{legacyNode->nodeID, 1}, {output->nodeID, 1}});
    const auto legacyTrackId = doc.addTrack(synth::TrackKind::Audio, "Legacy Audio");
    ASSERT_TRUE(legacyTrackId.isValid());
    ASSERT_TRUE(doc.setTrackBinding(legacyTrackId, legacyUuid));
    mc.getGraphEditor().updateComponents();

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2)
        << "one pre-existing channel plus one newly-created one for the legacy track";

    // The pre-existing channel's own strip is untouched: the same node, same uuid, still wired to
    // Master exactly as before.
    EXPECT_EQ(graph.getNodeForId(existingStrip->nodeID), existingStrip);
    EXPECT_EQ(nodeUuid(existingStrip), existingStripUuid);
    EXPECT_TRUE(graph.isConnected({{existingStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{existingStrip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    // The legacy track's own feed must have been re-routed through its NEW strip.
    EXPECT_FALSE(graph.isConnected({{legacyNode->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_FALSE(graph.isConnected({{legacyNode->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, CreateChannelsIsANoOpWhenNothingNeedsAChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    // Deliberately NOT newPatchForTest() here: GraphEditor::newPatch() itself pushes TWO undo
    // steps of its own (its own comment: graph clear + timeline clear, kept separate on purpose),
    // which would make "one undo empties the stack" a false negative below for a reason that has
    // nothing to do with Create Channels. The default factory preset starts with zero tracks
    // (same starting point every non-newPatch test above relies on), so it's a clean baseline.
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc); // already fully channeled -- nothing for "Create Channels" to do
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    ASSERT_TRUE(mc.getUndoManager().canUndo()) << "addAudioTrack itself pushed one undo step";

    const int nodesBefore = (int)graph.getNodes().size();
    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ((int)graph.getNodes().size(), nodesBefore) << "no new nodes -- nothing needed a channel";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "byte-for-byte unchanged -- a true no-op";

    // No new undo step was pushed: the ONE undo available must be addAudioTrack's own, removing
    // the whole channel it built, not a no-op Create Channels step sitting on top of it.
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0)
        << "undoing once removed the whole audio track, proving Create Channels pushed nothing of its own";
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

// D1 (docs/mixer/mixer.md#channels-follow-audio-not-tracks): "channels follow audio," not tracks, so two tracks that
// share one unchanneled instrument must come out of the sweep with exactly ONE channel between them, not two. This
// falls out for free from reusing T184's own per-node builder: the first track's call builds the channel and removes
// the shared instrument's exit edges, so the second track's call sees findUnchanneledOutputFeeds already empty and does
// nothing.
TEST_F(ChannelFlowTest, CreateChannelsGivesTwoTracksSharingOneInstrumentJustOneChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);

    // Two legacy MIDI tracks (bare Track In, auto-created+bound by the menu action) both feeding
    // one shared, channel-less Oscillator wired straight to the output.
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    std::vector<juce::AudioProcessorGraph::Node*> trackIns;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == ModuleType::TimelineMidiSource)
                    trackIns.push_back(node);
    ASSERT_EQ(trackIns.size(), 2u);

    juce::String instrumentUuid;
    auto* instrumentNode = addPlainNodeCFT(graph, "Oscillator", {300, 300}, instrumentUuid);
    ASSERT_NE(instrumentNode, nullptr);
    for (auto* trackIn : trackIns)
        graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    graph.addConnection({{instrumentNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrumentNode->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0) << "opens unchanged";

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1)
        << "one shared instrument gets one channel, not one per track (D1: channels follow audio)";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 0}, {output->nodeID, 0}}))
        << "the shared instrument's feed must be re-routed through its one new channel";
    EXPECT_FALSE(graph.isConnected({{instrumentNode->nodeID, 1}, {output->nodeID, 1}}));

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0)
        << "the one undo step removes the whole (single) channel";
}

// The spec's other required no-op clause: the "+ Track" menu's "Create Channels" entry itself must
// be DISABLED (not just a silent no-op) whenever nothing needs a channel, and enabled the moment
// something does. openAddTrackMenu() reads this straight off
// TrackHeaderHost::hasTracksNeedingChannels() (TimelinePanelComponent.cpp), so exercising that same
// public seam here proves the real menu's enabled state without needing to open the async
// juce::PopupMenu itself.
TEST_F(ChannelFlowTest, HasTracksNeedingChannelsBacksTheMenusEnabledState) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();

    EXPECT_FALSE(mc.hasTracksNeedingChannelsForTest()) << "a brand-new patch has no tracks at all";

    addAudioTrack(mc); // fully channeled via the normal flow
    EXPECT_FALSE(mc.hasTracksNeedingChannelsForTest())
        << "the menu entry must stay disabled -- this track already has a channel";

    // A legacy audio track, wired straight to the output with no insert chain -- the same
    // pre-P9-3 shape the tests above build.
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    juce::String legacyUuid;
    auto* legacyNode = addPlainNodeCFT(graph, "Track Audio", {800, 50}, legacyUuid);
    ASSERT_NE(legacyNode, nullptr);
    graph.addConnection({{legacyNode->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{legacyNode->nodeID, 1}, {output->nodeID, 1}});
    auto& doc = mc.getTimelineDoc();
    const auto legacyTrackId = doc.addTrack(synth::TrackKind::Audio, "Legacy Audio");
    ASSERT_TRUE(legacyTrackId.isValid());
    ASSERT_TRUE(doc.setTrackBinding(legacyTrackId, legacyUuid));
    mc.getGraphEditor().updateComponents();

    EXPECT_TRUE(mc.hasTracksNeedingChannelsForTest())
        << "the legacy track has no channel yet -- the menu entry must now be enabled";

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);
    EXPECT_FALSE(mc.hasTracksNeedingChannelsForTest()) << "both tracks are channeled now -- disabled again";
}
