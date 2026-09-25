// ChannelFlowTests.cpp
//
// T173a (FRO226): "+ Track -> Audio Track" now builds a WHOLE mixer channel in one undo step —
//
//     Track Audio -> Gate (bypassed) -> Parametric EQ (bypassed) -> Compressor (bypassed)
//                 -> Channel Strip (Stereo) -> Master (Mix)
//
// with {Track Audio, Gate, EQ, Compressor, Strip} boxed into ONE collapsed macro named after the track.
// Master stays OUTSIDE the macro and the Strip -> Master cable is a plain graph edge, never a macro
// port — see MainComponent::addAudioTrack's own comment and Source/Mixer/ChannelFlows/ChannelFlows.h for why.
//
// Drives the flow through a real MainComponent via the same headless seam
// AudioClipPlaybackTests.cpp's AddAudioTrackFlowTest uses (TimelinePanelComponent's
// applyAddTrackMenuChoice), so these tests exercise the whole app wiring, not just
// synth::buildDefaultAudioChannel in isolation.

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

TEST_F(ChannelFlowTest, AudioTrackBuildsDefaultChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::TimelineAudioSource), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Gate), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    auto* trackAudio = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* gate = findNodeOfTypeCFT(graph, ModuleType::Gate);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* comp = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackAudio, nullptr);
    ASSERT_NE(gate, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 0}, {gate->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 1}, {gate->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{gate->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{gate->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{eq->nodeID, 0}, {comp->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{eq->nodeID, 1}, {comp->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{comp->nodeID, 0}, {strip->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{comp->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}}))
        << "the right leg must land on kRightBase (4)";
    EXPECT_FALSE(graph.isConnected({{comp->nodeID, 1}, {strip->nodeID, 1}})) << "never ch1 for the right leg";

    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 1}, {output->nodeID, 1}}));

    EXPECT_FALSE(graph.isConnected({{trackAudio->nodeID, 0}, {output->nodeID, 0}}))
        << "Track Audio must no longer wire straight to the output";
    EXPECT_FALSE(graph.isConnected({{trackAudio->nodeID, 1}, {output->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, DefaultInsertsAreBypassedAndStripIsStereo) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    auto* gateNode = findNodeOfTypeCFT(graph, ModuleType::Gate);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(gateNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);

    auto* gateModule = dynamic_cast<ModuleBase*>(gateNode->getProcessor());
    auto* eqModule = dynamic_cast<ModuleBase*>(eqNode->getProcessor());
    auto* compModule = dynamic_cast<ModuleBase*>(compNode->getProcessor());
    auto* stripModule = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(gateModule, nullptr);
    ASSERT_NE(eqModule, nullptr);
    ASSERT_NE(compModule, nullptr);
    ASSERT_NE(stripModule, nullptr);

    EXPECT_TRUE(gateModule->isBypassed());
    EXPECT_TRUE(eqModule->isBypassed());
    EXPECT_TRUE(compModule->isBypassed());
    EXPECT_FALSE(stripModule->isBypassed());
    EXPECT_EQ(stripModule->getShape(), ChannelStripModule::Shape::Stereo);
}

TEST_F(ChannelFlowTest, ChannelIsOneCollapsedMacroNamedAfterTrack) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks().back();

    EXPECT_EQ(macro.name, track.name);
    EXPECT_TRUE(macro.collapsed);
    EXPECT_EQ(track.bindingUuid, nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource)));

    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Gate)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);

    EXPECT_FALSE(macro.hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Master))))
        << "Master must stay outside the macro";
}

TEST_F(ChannelFlowTest, OneUndoStepRevertsEverythingAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    addAudioTrack(mc);
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1) << "the channel must have been built";
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, SecondAudioTrackReusesMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);
    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1) << "Master is a singleton";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2);
    EXPECT_EQ(macros.size(), 2);

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    int stripsIntoMix = 0;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || module->getModuleType() != ModuleType::ChannelStrip)
            continue;
        if (graph.isConnected({{node->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}) &&
            graph.isConnected(
                {{node->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}))
            ++stripsIntoMix;
    }
    EXPECT_EQ(stripsIntoMix, 2) << "both strips must land on Mix";

    // One undo removes only the SECOND channel; Master (and the first channel) remain.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
    EXPECT_EQ(macros.size(), 1);
}

// T187: before this fix, GraphEditor::newPatch() left the graph with zero nodes, so
// synth::spliceMasterNode (called from buildDefaultAudioChannel) had no Audio Output to target
// and silently returned nullptr — a bare Track Audio in a brand-new project was unheard until the
// user manually added an Audio Output. newPatch() now seeds one as part of the same undo step.
TEST_F(ChannelFlowTest, AudioTrackOnFreshNewPatchGetsMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    mc.newPatchForTest();
    ASSERT_NE(findNodeNamedCFT(graph, "Audio Output"), nullptr) << "newPatch must seed an Audio Output";

    addAudioTrack(mc);

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr) << "the first channel must splice Master immediately, not need a manual Audio Output";

    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));

    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 0}, {output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{master->nodeID, 1}, {output->nodeID, 1}}));

    // T187 layout follow-up: a bare Audio Output left at the newPatch seed's canvas origin while
    // Master lands right of the freshly-built chain read as backwards wiring on screen (Master "on
    // the right", Audio Output "top left" with a cable snaking back across everything to reach it —
    // caught live via computer-use testing). addAudioTrack now relocates the seeded Audio Output to
    // sit right of Master once Master is first spliced, so the row reads left to right.
    const int outputX = static_cast<int>(output->properties.getWithDefault("x", 0));
    const int masterX = static_cast<int>(master->properties.getWithDefault("x", 0));
    EXPECT_GT(outputX, masterX) << "Audio Output must be relocated to terminate the row after Master, "
                                   "not left behind at the newPatch seed position";
}

// Same pinning pattern as AudioClipPlaybackTest.AbsentFromTheLibraryWithAPinnedSizeEstimate /
// RecordTapTest's own — but for two cards at once, since both were missing an estimateModuleSize
// entry (silently falling back to the generic {280, 360} default, which is wrong for either card
// and was part of why Master ended up hidden under the EQ card — see this file's header comment).
// The strip is measured Stereo, matching what buildDefaultAudioChannel always builds; width does
// not move between Mono/Stereo (only the input jack-row count would), so this also stands in for
// the Mono shape.
TEST_F(ChannelFlowTest, ChannelStripAndMasterHaveAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Channel Strip"))
        << "Channel Strip is internal-only and must stay out of the module library";
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Master"))
        << "Master is internal-only and must stay out of the module library";

    AudioEngine engine;
    GraphEditor editor(engine);

    auto stripProcessor = synth::AIStateMapper::createModule("Channel Strip");
    ASSERT_NE(stripProcessor, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    ModuleComponent stripComp(stripProcessor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
    const auto stripEstimate = GraphEditor::estimateModuleSize("Channel Strip");
    EXPECT_EQ(stripEstimate.x, stripComp.getWidth());
    EXPECT_EQ(stripEstimate.y, stripComp.getHeight());

    auto masterProcessor = synth::AIStateMapper::createModule("Master");
    ASSERT_NE(masterProcessor, nullptr);
    ModuleComponent masterComp(masterProcessor.get(), juce::AudioProcessorGraph::NodeID(2), editor);
    const auto masterEstimate = GraphEditor::estimateModuleSize("Master");
    EXPECT_EQ(masterEstimate.x, masterComp.getWidth());
    EXPECT_EQ(masterEstimate.y, masterComp.getHeight());
}

// The bug this file's header comment describes, reproduced against the REAL ModuleComponent
// bounds: on the old fixed-300px stride, Parametric EQ's double-width (560px) card overlapped the
// Compressor, and Master (placed at trackAudioPosition + kSingleWidth + gap, i.e. still inside the
// expanded chain) landed underneath the EQ card too. addAudioTrack now derives every card's x from
// GraphEditor::estimateModuleSize, so none of the six cards below should overlap and Master should
// sit to the right of everything else.
TEST_F(ChannelFlowTest, ChannelCardsDoNotOverlapAndMasterIsRightOfStrip) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(2400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addAudioTrack(mc);

    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;

    // Expand the macro through the same API the "Expand" menu item and the collapsed card's own
    // click use (GraphEditor::setMacroCollapsed) so member ModuleComponents are laid out for real
    // (applyMacroCollapsed calls updateComponents()) rather than inferring bounds ourselves.
    mc.getGraphEditor().getMacroController().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(macros.find(macroId)->collapsed);

    auto* trackAudioNode = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* gateNode = findNodeOfTypeCFT(graph, ModuleType::Gate);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* masterNode = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackAudioNode, nullptr);
    ASSERT_NE(gateNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);
    ASSERT_NE(masterNode, nullptr);

    auto findComp = [&mc](juce::AudioProcessorGraph::Node* node) -> ModuleComponent* {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                return comp;
        return nullptr;
    };

    auto* trackAudioComp = findComp(trackAudioNode);
    auto* gateComp = findComp(gateNode);
    auto* eqComp = findComp(eqNode);
    auto* compComp = findComp(compNode);
    auto* stripComp = findComp(stripNode);
    auto* masterComp = findComp(masterNode);
    // All six nodes are ordinary graph nodes with their own ModuleComponent (a hidden macro
    // member's component still exists — only setVisible(false) — and expanding just flips that
    // back on), so every lookup above must resolve; a silent nullptr here would make the
    // assertions below pass vacuously.
    ASSERT_NE(trackAudioComp, nullptr) << "Track Audio must have a real ModuleComponent once expanded";
    ASSERT_NE(gateComp, nullptr) << "Gate must have a real ModuleComponent once expanded";
    ASSERT_NE(eqComp, nullptr) << "Parametric EQ must have a real ModuleComponent once expanded";
    ASSERT_NE(compComp, nullptr) << "Compressor must have a real ModuleComponent once expanded";
    ASSERT_NE(stripComp, nullptr) << "Channel Strip must have a real ModuleComponent once expanded";
    ASSERT_NE(masterComp, nullptr) << "Master must have a real ModuleComponent (it is never boxed into the macro)";

    // Anchor the coordinate space once: content-component bounds should track the node's own
    // "x"/"y" properties directly (no zoom/scroll in a freshly-built headless MainComponent), so a
    // mismatch here means the two are in different coordinate spaces rather than a real overlap.
    EXPECT_EQ(trackAudioComp->getX(), static_cast<int>(trackAudioNode->properties.getWithDefault("x", -1)));
    EXPECT_EQ(trackAudioComp->getY(), static_cast<int>(trackAudioNode->properties.getWithDefault("y", -1)));

    const std::array<ModuleComponent*, 6> cards = {trackAudioComp, gateComp, eqComp, compComp, stripComp, masterComp};
    for (size_t i = 0; i < cards.size(); ++i) {
        for (size_t j = i + 1; j < cards.size(); ++j) {
            EXPECT_FALSE(cards[i]->getBounds().intersects(cards[j]->getBounds()))
                << "card " << i << " " << cards[i]->getBounds().toString().toStdString() << " overlaps card " << j
                << " " << cards[j]->getBounds().toString().toStdString();
        }
    }

    EXPECT_LT(trackAudioComp->getX(), gateComp->getX());
    EXPECT_LT(gateComp->getX(), eqComp->getX());
    EXPECT_LT(eqComp->getX(), compComp->getX());
    EXPECT_LT(compComp->getX(), stripComp->getX());
    EXPECT_LT(stripComp->getX(), masterComp->getX());
    EXPECT_GE(masterComp->getX(), stripComp->getRight()) << "Master must be fully clear of the Strip card";
}

TEST_F(ChannelFlowTest, RefusedAtMaxTracksCreatesNothing) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    // Fixture setup goes straight through the doc, not the undo-recording flow, so it pushes no
    // undo step of its own — see TimelinePanelTests.cpp's AddTrackAtTheCapAddsNoNode for the same
    // pattern.
    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(synth::TrackKind::Midi, "Filler").isValid());
    ASSERT_FALSE(mc.getUndoManager().canUndo());
    const int nodesBefore = graph.getNumNodes();

    addAudioTrack(mc);

    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a refused audio track must leave no orphan node";
    EXPECT_EQ(macros.size(), 0) << "a refused audio track must leave no macro";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing changed in any domain: no undo step";
}

// FRO226: an old saved project/preset's own JSON — authored before the Gate was added to the
// factory default chain — has no Gate node, and loading it must never inject one. This is the one
// test that actually covers "existing saved projects/presets load unchanged" (buildChannelChain
// only runs when a NEW channel is built; a trusted applyJSONToGraph load never calls it).
TEST_F(ChannelFlowTest, LoadingAnOldEqCompressorStripPatchInjectsNoGate) {
    // A bare graph, not a MainComponent's: this pins the loader alone, and clearing a live
    // MainComponent's graph under its module cards (without detachAllModuleComponents() first)
    // leaves the cards attached to deleted parameters -- a teardown hang on Linux CI.
    juce::AudioProcessorGraph graph;

    // A pre-FRO226 project's chain: Track Audio -> Parametric EQ -> Compressor -> Channel Strip
    // -> Master, no Gate anywhere. Trusted apply, exactly like ProjectBundle::load's own replaying
    // of a saved graph.
    const juce::String oldPatchJson = R"JSON(
{
  "schemaVersion": 1,
  "nodes": [
    { "id": 1, "type": "Track Audio", "uuid": "track-audio", "params": {} },
    { "id": 2, "type": "Parametric EQ", "uuid": "eq", "params": { "bypassed": true } },
    { "id": 3, "type": "Compressor", "uuid": "compressor", "params": { "bypassed": true } },
    { "id": 4, "type": "Channel Strip", "uuid": "strip",
      "params": { "bypassed": false, "gain": 0.0, "pan": 0.0, "muted": false },
      "state": { "shape": "stereo", "solo": false, "isBus": false, "sends": [] } },
    { "id": 5, "type": "Master", "uuid": "master", "params": { "bypassed": false, "gain": 0.0, "muted": false } },
    { "id": 6, "type": "Audio Output", "uuid": "audio-output", "params": {} }
  ],
  "connections": [
    { "src": 1, "srcPort": 0, "dst": 2, "dstPort": 0, "isMidi": false },
    { "src": 1, "srcPort": 1, "dst": 2, "dstPort": 1, "isMidi": false },
    { "src": 2, "srcPort": 0, "dst": 3, "dstPort": 0, "isMidi": false },
    { "src": 2, "srcPort": 1, "dst": 3, "dstPort": 1, "isMidi": false },
    { "src": 3, "srcPort": 0, "dst": 4, "dstPort": 0, "isMidi": false },
    { "src": 3, "srcPort": 1, "dst": 4, "dstPort": 4, "isMidi": false },
    { "src": 4, "srcPort": 0, "dst": 5, "dstPort": 0, "isMidi": false },
    { "src": 4, "srcPort": 4, "dst": 5, "dstPort": 1, "isMidi": false },
    { "src": 5, "srcPort": 0, "dst": 6, "dstPort": 0, "isMidi": false },
    { "src": 5, "srcPort": 1, "dst": 6, "dstPort": 1, "isMidi": false }
  ]
}
)JSON";
    const auto parsed = juce::JSON::parse(oldPatchJson);
    ASSERT_TRUE(parsed.isObject());
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(parsed, graph, /*clearExisting=*/true, /*trusted=*/true));

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Gate), 0) << "loading an old patch must never inject a Gate";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(graph.getNumNodes(), 6) << "the old chain's node count must be exactly what was saved, nothing added";

    // The graph must still play exactly as it did — the old chain re-wired straight from the
    // source into the EQ, with no new node spliced in front of it.
    auto* trackAudio = findNodeOfTypeCFT(graph, ModuleType::TimelineAudioSource);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(trackAudio, nullptr);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{trackAudio->nodeID, 1}, {eq->nodeID, 1}}));
}
