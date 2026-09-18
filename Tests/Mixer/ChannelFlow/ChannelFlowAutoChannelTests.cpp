// ChannelFlowAutoChannelTests.cpp
//
// T184 (P9-3c, docs/mixer.md §5.2): a MIDI track auto-creates the destination's mixer channel
// on connect. Core-level tests for synth::findUnchanneledOutputFeeds/buildChannelForFeeds
// first, then real-mouse-gesture coverage through GraphEditor::endConnectionDrag.
// dragRealMidiCableBetweenCFT below is local to this file.

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

// =================================================================================================
// T184 (P9-3c, docs/mixer.md §5.2 "main workflow"): a MIDI track auto-creates the destination's
// mixer channel on connect. Core-level tests for synth::findUnchanneledOutputFeeds /
// synth::buildChannelForFeeds first, then real-mouse-gesture coverage through GraphEditor's
// endConnectionDrag (docs/development/test-patterns.md's real-mouse-path guidance — the same reason
// Tests/Macros/MacroPortRealMouseDragTests.cpp drives ModuleComponent::mouseDown/mouseDrag/mouseUp
// directly rather than calling GraphEditor's drag methods).
// =================================================================================================

namespace {

// The MIDI analog of MacroPortRealMouseDragTests.cpp's dragRealCableBetween (which only drives
// audio/CV jacks via ModuleComponent::getPortCenter): drives a full real mouseDown -> mouseDrag ->
// mouseUp gesture from `srcComp`'s MIDI OUT jack to `dstComp`'s MIDI IN jack, exactly like a real
// user's press-drag-release. A MIDI jack's position comes from ModuleComponent::getMidiPortCenter
// (fixed top-right/top-left, not part of the indexed audio-jack gutter — see
// ModuleComponent::getPortForPoint's own MIDI special-case).
void dragRealMidiCableBetweenCFT(ModuleComponent& srcComp, ModuleComponent& dstComp) {
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const auto srcJackLocal = srcComp.getMidiPortCenter(true);
    srcComp.mouseDown(realMouseEventCFT(srcComp, srcJackLocal, srcJackLocal, leftClick));

    const auto targetScreenPos = dstComp.localPointToGlobal(dstComp.getMidiPortCenter(false));
    const auto srcLocalForTarget = srcComp.getLocalPoint(nullptr, targetScreenPos);
    srcComp.mouseDrag(realMouseEventCFT(srcComp, srcLocalForTarget, srcJackLocal, leftClick, /*wasDragged=*/true));
    srcComp.mouseUp(realMouseEventCFT(srcComp, srcLocalForTarget, srcJackLocal, leftClick, /*wasDragged=*/true));
}

} // namespace
// -------------------------------------------------------------------------------------------
// Latency (per the T184 brief: STOP and report if either is nonzero rather than working around
// it — the whole feature premise is inserting this chain into a path that previously went
// straight to the output).
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowAutoChannelCore, BypassedEQAndCompressorReportZeroLatencyAfterPrepare) {
    auto eq = synth::AIStateMapper::createModule("Parametric EQ");
    auto compressor = synth::AIStateMapper::createModule("Compressor");
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(compressor, nullptr);
    if (auto* m = dynamic_cast<ModuleBase*>(eq.get()))
        m->setBypassed(true);
    if (auto* m = dynamic_cast<ModuleBase*>(compressor.get()))
        m->setBypassed(true);
    eq->prepareToPlay(44100.0, 512);
    compressor->prepareToPlay(44100.0, 512);
    EXPECT_EQ(eq->getLatencySamples(), 0) << "an inserted-but-bypassed EQ must add no latency to the chain";
    EXPECT_EQ(compressor->getLatencySamples(), 0) << "an inserted-but-bypassed Compressor must add no latency";
}

// -------------------------------------------------------------------------------------------
// synth::findUnchanneledOutputFeeds
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowAutoChannelCore, FindUnchanneledOutputFeedsOnInstrumentToOutputFindsTwoExits) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    EXPECT_EQ(exits.size(), 2u);
}

TEST(ChannelFlowAutoChannelCore, FindUnchanneledOutputFeedsOnStripChanneledInstrumentFindsZero) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    auto stripProcessor = synth::AIStateMapper::createModule("Channel Strip");
    ASSERT_NE(stripProcessor, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(stripProcessor));
    ASSERT_NE(stripNode, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {stripNode->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {stripNode->nodeID, ChannelStripModule::kRightBase}});
    graph.addConnection({{stripNode->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{stripNode->nodeID, ChannelStripModule::kRightBase}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    EXPECT_TRUE(exits.empty()) << "already reaches the output through a Channel Strip";
}

TEST(ChannelFlowAutoChannelCore, FindUnchanneledOutputFeedsNotReachingOutputFindsZero) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    juce::String oscUuid, filterUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    auto* filter = addPlainNodeCFT(graph, "Filter", {200, 0}, filterUuid);
    ASSERT_NE(osc, nullptr);
    ASSERT_NE(filter, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    EXPECT_TRUE(exits.empty()) << "no Audio Output/Rec Tap/Master node exists downstream at all";
}

TEST(ChannelFlowAutoChannelCore,
     FindUnchanneledOutputFeedsIgnoresModulationBranchAndSweepsTheUnrelatedPathOntoMasterDirect) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    juce::String oscAUuid, oscBUuid, filterUuid;
    auto* oscA = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscAUuid);
    auto* oscB = addPlainNodeCFT(graph, "Oscillator", {0, 300}, oscBUuid);
    auto* filter = addPlainNodeCFT(graph, "Filter", {200, 300}, filterUuid);
    ASSERT_NE(oscA, nullptr);
    ASSERT_NE(oscB, nullptr);
    ASSERT_NE(filter, nullptr);

    // oscA's own straight path to the output -- the two exits this search should find.
    graph.addConnection({{oscA->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{oscA->nodeID, 1}, {outNode->nodeID, 1}});

    // oscA ALSO modulates the unrelated Filter's cutoff -- a hidden AttenuverterModule leg that
    // must be neither traversed nor counted as an exit.
    auto* filterModule = dynamic_cast<ModuleBase*>(filter->getProcessor());
    ASSERT_NE(filterModule, nullptr);
    int cutoffChannel = -1;
    for (const auto& target : filterModule->getModulationTargets()) {
        cutoffChannel = target.channelIndex;
        break;
    }
    ASSERT_GE(cutoffChannel, 0) << "Filter must declare at least one modulation target";
    engine.addModRouting(oscA->nodeID, 0, filter->nodeID, cutoffChannel);

    // The unrelated Filter's OWN, totally separate signal path to the output -- must survive
    // untouched by a search rooted at oscA.
    graph.addConnection({{oscB->nodeID, 0}, {filter->nodeID, 0}});
    graph.addConnection({{oscB->nodeID, 1}, {filter->nodeID, 1}});
    graph.addConnection({{filter->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{filter->nodeID, 1}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, oscA->nodeID);
    ASSERT_EQ(exits.size(), 2u);
    for (const auto& exit : exits)
        EXPECT_EQ(exit.source.nodeID, oscA->nodeID) << "only oscA's own direct path may appear";

    const synth::DefaultChannelLayout layout{{500, 0}, {600, 0}, {700, 0}, {800, 0}};
    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());
    ASSERT_NE(channel.master, nullptr);

    // The unrelated Filter -> oscB leg itself is completely untouched by the build.
    EXPECT_TRUE(graph.isConnected({{oscB->nodeID, 0}, {filter->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscB->nodeID, 1}, {filter->nodeID, 1}}));

    // Filter's own straight-to-output feed pre-dated Master, so spliceMasterNode's sweep (run as
    // part of building oscA's channel, since no Master yet existed) re-routes it onto Master's
    // Direct bus like every other pre-existing direct feed -- see MasterSplice.cpp's own contract
    // ("every audio connection that fed that node's ch0/ch1 is re-routed into Master"). It still
    // reaches the output, just via Master now, exactly like oscA's own feed does.
    EXPECT_FALSE(graph.isConnected({{filter->nodeID, 0}, {outNode->nodeID, 0}}))
        << "swept onto Master's Direct bus by the same-transaction spliceMasterNode call";
    EXPECT_FALSE(graph.isConnected({{filter->nodeID, 1}, {outNode->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 0}, {channel.master->nodeID, MasterModule::kDirectLeft}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 1}, {channel.master->nodeID, MasterModule::kDirectRight}}));
    // Master's OWN outputs are plain 0/1 (MasterModule::kNumOutputs) -- kDirectLeft/kDirectRight
    // are input-side indices only, never valid on the output side.
    EXPECT_TRUE(graph.isConnected({{channel.master->nodeID, 0}, {outNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{channel.master->nodeID, 1}, {outNode->nodeID, 1}}));
}

// -------------------------------------------------------------------------------------------
// synth::buildChannelForFeeds
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowAutoChannelCore, BuildChannelForFeedsRemovesExitEdgesAndWiresThroughToANewMaster) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {outNode->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {outNode->nodeID, 1}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    ASSERT_EQ(exits.size(), 2u);

    const synth::DefaultChannelLayout layout{{500, 0}, {600, 0}, {700, 0}, {800, 0}};
    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());
    ASSERT_NE(channel.master, nullptr) << "no Master existed yet; this call must splice one";

    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {outNode->nodeID, 0}})) << "the exit edges must be gone";
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 1}, {outNode->nodeID, 1}}));

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {channel.master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {channel.master->nodeID, MasterModule::kMixRight}}));
    EXPECT_TRUE(graph.isConnected({{channel.master->nodeID, 0}, {outNode->nodeID, 0}}));
}

TEST(ChannelFlowAutoChannelCore, BuildChannelForFeedsReusesAnExistingMasterAndClearsDirectFeeds) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto outNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    ASSERT_NE(outNode, nullptr);
    auto* master = synth::spliceMasterNode(graph, {400, 0});
    ASSERT_NE(master, nullptr);

    juce::String oscUuid;
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {0, 0}, oscUuid);
    ASSERT_NE(osc, nullptr);
    graph.addConnection({{osc->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}});
    graph.addConnection({{osc->nodeID, 1}, {master->nodeID, MasterModule::kDirectRight}});

    const auto exits = synth::findUnchanneledOutputFeeds(graph, osc->nodeID);
    ASSERT_EQ(exits.size(), 2u);

    const synth::DefaultChannelLayout layout{{500, 0}, {600, 0}, {700, 0}, {800, 0}};
    const auto channel = synth::buildChannelForFeeds(graph, exits, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());
    EXPECT_EQ(channel.master, master) << "the pre-existing Master singleton must be reused, not duplicated";

    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}}))
        << "the exit edges (on Direct) must be gone";
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 1}, {master->nodeID, MasterModule::kDirectRight}}));

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{osc->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "nothing of osc's own feeds Master Direct any more";
}

// -------------------------------------------------------------------------------------------
// Real-mouse-gesture coverage through GraphEditor::endConnectionDrag, via a live MainComponent.
// FIXTURE ORDER MATTERS: createTrackInNode() auto-wires a fresh Track In to the sole existing MIDI
// instrument when there is exactly one — so every test below starts from newPatchForTest() (zero
// nodes but a seeded Audio Output, T187) and adds the MIDI track BEFORE any instrument exists,
// keeping Track In unwired until the real-mouse gesture under test wires it.
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, AutoChannelOnConnect_ToggleOnBuildsOneChannelAsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto& graph = mc.getAudioEngine().getGraph();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    // The instrument already wired to the output BY HAND -- exactly the "no channel yet" starting
    // state T184 targets.
    juce::String instrumentUuid;
    auto* instrument = addPlainNodeCFT(graph, "Oscillator", {600, 600}, instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    graph.addConnection({{instrument->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrument->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    auto* trackInComp = compForCFT(mc.getGraphEditor(), trackIn->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackInComp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosBefore = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    dragRealMidiCableBetweenCFT(*trackInComp, *instrumentComp);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_FALSE(graph.isConnected({{instrument->nodeID, 0}, {output->nodeID, 0}}))
        << "the instrument's straight-to-output feed must have been re-routed through the new channel";

    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosAfter = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "one undo must remove BOTH the connection and the channel";
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosBefore);

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, AutoChannelOnConnect_ToggleOffOnlyConnectsNoChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.getGraphEditor().setAutoCreateChannelOnConnectEnabled(false);

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto& graph = mc.getAudioEngine().getGraph();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    juce::String instrumentUuid;
    auto* instrument = addPlainNodeCFT(graph, "Oscillator", {600, 600}, instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    graph.addConnection({{instrument->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrument->nodeID, 1}, {output->nodeID, 1}});
    mc.getGraphEditor().updateComponents();

    auto* trackInComp = compForCFT(mc.getGraphEditor(), trackIn->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackInComp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);

    const int nodesBefore = graph.getNodes().size();

    dragRealMidiCableBetweenCFT(*trackInComp, *instrumentComp);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_EQ(graph.getNodes().size(), nodesBefore) << "no new nodes — the toggle is OFF";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0);
    EXPECT_TRUE(graph.isConnected({{instrument->nodeID, 0}, {output->nodeID, 0}}))
        << "the pre-existing straight-to-output feed must be untouched";
    EXPECT_TRUE(graph.isConnected({{instrument->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, AutoChannelOnConnect_AlreadyChanneledInstrumentGetsNoNewStrip) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    // "+ Track -> Instrument" (T183) builds Track In -> Oscillator -> EQ -> Compressor -> Strip ->
    // Master in one step; the instrument this test's SECOND Track In targets already has a channel.
    addInstrumentTrack(mc, "Oscillator");
    auto& graph = mc.getAudioEngine().getGraph();
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    auto* instrument = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    ASSERT_NE(instrument, nullptr);

    auto& macros = mc.getGraphEditor().getMacros();
    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;
    // Expand: the instrument's own ModuleComponent must be a real, visible drop target for the
    // direct-jack drag below (a collapsed macro's hidden members are not "on the canvas" —
    // endConnectionDrag's own comment).
    mc.getGraphEditor().setMacroCollapsed(macroId, false);

    // A second, independent MIDI track — added directly (not through createTrackInNode, whose own
    // "exactly one instrument" auto-wire would otherwise wire it for us and never exercise this
    // gesture at all) — exactly what a user would drag onto the already-channeled instrument by
    // hand.
    juce::String trackIn2Uuid;
    auto* trackIn2 = addPlainNodeCFT(graph, "Track In", {50, 900}, trackIn2Uuid);
    ASSERT_NE(trackIn2, nullptr);
    // trackIn2 lives outside the instrument's macro, so a live macro-boundary crossing would
    // otherwise auto-mint a macro port here (T148) -- a real, separately-tested behaviour this
    // test isn't about. Disabled so the drag exercises T184's own direct-jack path in isolation.
    mc.getGraphEditor().setAutoCreateMacroPortsOnDragEnabled(false);
    mc.getGraphEditor().updateComponents();

    auto* trackIn2Comp = compForCFT(mc.getGraphEditor(), trackIn2->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackIn2Comp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);
    ASSERT_TRUE(instrumentComp->isVisible());

    dragRealMidiCableBetweenCFT(*trackIn2Comp, *instrumentComp);

    EXPECT_TRUE(graph.isConnected({{trackIn2->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1)
        << "the instrument already has a channel; nothing new should be created";
}

// Both T148 (macro-boundary auto-porting) and T184 (auto-channel) are ON here — the instrument
// lives inside a macro and the Track In driving it lives outside, so the drag crosses a macro
// boundary AND lands on an unchanneled instrument. This is the highest-risk combined path: a
// stray nested undo transaction in either feature would split "port + connection + channel" into
// more than one Cmd+Z step and every OTHER test in this file (which each disable one feature or
// the other, or avoid crossing a macro boundary) would still pass.
TEST_F(ChannelFlowTest, AutoChannelOnConnect_NewChainNodesJoinTheInstrumentsExistingMacro) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    ASSERT_TRUE(mc.getGraphEditor().getAutoCreateMacroPortsOnDragEnabled()) << "T148 stays ON for this test";
    ASSERT_TRUE(mc.getGraphEditor().getAutoCreateChannelOnConnectEnabled()) << "T184 stays ON for this test";

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    auto& graph = mc.getAudioEngine().getGraph();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    juce::String instrumentUuid;
    auto* instrument = addPlainNodeCFT(graph, "Oscillator", {600, 600}, instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    graph.addConnection({{instrument->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection({{instrument->nodeID, 1}, {output->nodeID, 1}});

    // Box the instrument ALONE, as an ORDINARY member, in its own macro (no ports) — a module
    // that's already someone's macro member, unrelated to a channel, is a realistic starting state.
    // Track In stays OUTSIDE this macro, which is exactly what makes the coming drag a
    // boundary-crossing one.
    const auto macroId = mc.getGraphEditor().addMacroForMembers({instrumentUuid}, "TestMacro", {600, 600});
    ASSERT_FALSE(macroId.isEmpty());
    mc.getGraphEditor().setMacroCollapsed(macroId, false); // expand: instrument becomes visible again

    auto* trackInComp = compForCFT(mc.getGraphEditor(), trackIn->nodeID);
    auto* instrumentComp = compForCFT(mc.getGraphEditor(), instrument->nodeID);
    ASSERT_NE(trackInComp, nullptr);
    ASSERT_NE(instrumentComp, nullptr);
    ASSERT_TRUE(instrumentComp->isVisible());

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosBefore = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    dragRealMidiCableBetweenCFT(*trackInComp, *instrumentComp);

    // (a) T148: the boundary crossing minted a macro MIDI inlet port, and Track In wires to it
    // rather than straight to the instrument.
    auto* port = findNodeOfTypeCFT(graph, ModuleType::MacroMidiInlet);
    ASSERT_NE(port, nullptr) << "the macro boundary crossing must have minted a port";
    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {port->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_TRUE(graph.isConnected({{port->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_FALSE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                    {instrument->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}))
        << "the drag crossed a macro boundary, so it must route through the port, not directly";

    // (b) T184: the strip was built.
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);

    auto* macro = mc.getGraphEditor().getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(instrumentUuid));
    EXPECT_TRUE(macro->hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ))));
    EXPECT_TRUE(macro->hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor))));
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(macro->hasMember(nodeUuid(strip)));

    // docs/mixer_implementation.md item 2: Master stays OUTSIDE the macro, and Strip -> Master is a PLAIN
    // graph edge, never a macro port — spliceMasterNode/ensureMasterNode classify Mix vs Direct by
    // checking whether the connection's SOURCE NODE is itself a ChannelStripModule, which a
    // MacroOutlet sitting in between would defeat.
    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);
    EXPECT_FALSE(macro->hasMember(nodeUuid(master))) << "Master must never join the instrument's macro";
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "Strip -> Master must be a plain edge landing on Mix, not routed through a macro outlet";
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    // (c) ONE Cmd+Z reverts the port, the connection AND the channel together.
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String macrosAfter = juce::JSON::toString(mc.getGraphEditor().getMacros().toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore)
        << "one undo must remove the port, the connection AND the channel together";
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosBefore);

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(mc.getGraphEditor().getMacros().toVar()), macrosAfter);
}
