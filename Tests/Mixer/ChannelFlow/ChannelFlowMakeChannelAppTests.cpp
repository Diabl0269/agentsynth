// ChannelFlowMakeChannelAppTests.cpp
//
// FRO25 (P9-3d, docs/mixer/mixer.md#make-channel-and-shared-modules): "Make channel", real app wiring — the track header,
// canvas selection and module right-click menus, each as one undo step. Core render-identity
// coverage (standalone GraphEditor + HostedPatchCFT) lives in ChannelFlowMakeChannelCoreTests.cpp;
// shared rig helpers (LegacyRigCFT, HostedPatchCFT, McRigCFT, ...) live in
// ChannelFlowTestFixture.h.

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
// The real app wiring: header / canvas / module right-click menus, one undo step each.
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, TrackHeaderMakeChannelThroughTheRealRightClickIsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::SharedLfo);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, setup.trackA);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    const auto* macro = mc.getGraphEditor().getMacros().findByMember(nodeUuid(setup.rig.trackInA));
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->name, "Lead") << "the channel macro is named after the track";
    EXPECT_FALSE(macro->hasMember(nodeUuid(setup.rig.sharedLfo)));
    const auto after = snapshotCFT(mc);

    // Already channeled: the entry stays in place but disabled, and choosing it anyway is a no-op.
    auto* header = headerForCFT(mc, setup.trackA);
    ASSERT_NE(header, nullptr);
    const auto menu = rightClickHeaderMenuCFT(*header);
    const auto* item = findMenuItemByTextCFT(menu, "Make Channel");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
    header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kMakeChannelMenuId);
    expectSameSnapshotCFT(snapshotCFT(mc), after);

    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0) << "the ONE undo step reverts everything";
    ASSERT_TRUE(mc.getUndoManager().redo());
    expectSameSnapshotCFT(snapshotCFT(mc), after);
}

TEST_F(ChannelFlowTest, CanvasSelectionMakeChannelThroughTheRealRightClick) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::SharedLfo);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    auto& editor = mc.getGraphEditor();

    // The module card's own menu offers it for the chain it belongs to...
    auto* filterComp = compForCFT(editor, setup.rig.filterA->nodeID);
    ASSERT_NE(filterComp, nullptr);
    const auto moduleMenu = rightClickModuleMenuCFT(*filterComp);
    const auto* moduleItem = findMenuItemByTextCFT(moduleMenu, "Make Channel");
    ASSERT_NE(moduleItem, nullptr) << "a module card's menu offers Make Channel for its chain";
    EXPECT_TRUE(moduleItem->isEnabled);

    // ...and so does the canvas menu for a selected chain (the real empty-canvas right-click).
    editor.setSelectedNodes({setup.rig.oscA->nodeID, setup.rig.filterA->nodeID});
    juce::PopupMenu canvasMenu;
    bool shown = false;
    editor.setShowCanvasContextMenuHookForTest([&](juce::PopupMenu& menu) {
        canvasMenu = menu;
        shown = true;
    });
    const juce::Point<int> emptyCanvas(editor.getWidth() - 20, 20);
    editor.mouseDown(realMouseEventCFT(editor, emptyCanvas, emptyCanvas,
                                       juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    editor.setShowCanvasContextMenuHookForTest(nullptr);
    ASSERT_TRUE(shown) << "the right-click must reach the canvas menu";
    const auto* item = findMenuItemByTextCFT(canvasMenu, "Make Channel");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    ASSERT_TRUE(item->action != nullptr);

    const auto before = snapshotCFT(mc);
    item->action();
    const auto* macro = editor.getMacros().findByMember(nodeUuid(setup.rig.filterA));
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->name, "Lead") << "the selection resolves to its track, whose name the channel takes";
    EXPECT_TRUE(macro->hasMember(nodeUuid(setup.rig.trackInA)));
    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, MergePointBecomesABusChannelInOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::Merge);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, setup.trackA);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2);
    const auto* bus = mc.getGraphEditor().getMacros().findByMember(nodeUuid(setup.rig.filterB));
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->name, "Filter Bus");
    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, PolyChainGetsAVoiceMixerAheadOfTheStripInOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    ASSERT_NE(output, nullptr);

    juce::String trackInUuid, oscUuid;
    auto* trackIn = addPlainNodeCFT(graph, "Track In", {0, 0}, trackInUuid);
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {200, 0}, oscUuid);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(osc, nullptr);
    ASSERT_TRUE(setPolyParamCFT(osc->getProcessor(), true));
    graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    graph.addConnection({{osc->nodeID, 0}, {output->nodeID, 0}});
    graph.addConnection(
        {{osc->nodeID, dynamic_cast<ModuleBase*>(osc->getProcessor())->rightAudioLegChannel()}, {output->nodeID, 1}});
    auto& doc = mc.getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Poly Lead");
    ASSERT_TRUE(doc.setTrackBinding(track, trackInUuid));
    mc.getGraphEditor().updateComponents();
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, track);

    const auto* macro = mc.getGraphEditor().getMacros().findByMember(trackInUuid);
    ASSERT_NE(macro, nullptr);
    auto* voiceMixer = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::VoiceMixer);
    auto* eq = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ParametricEQ);
    ASSERT_NE(voiceMixer, nullptr) << "a chain ending poly gets a Voice Mixer ahead of the strip";
    ASSERT_NE(eq, nullptr);
    for (int voice = 0; voice < 8; ++voice)
        EXPECT_TRUE(graph.isConnected({{osc->nodeID, voice}, {voiceMixer->nodeID, voice}}));
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {eq->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{osc->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(synth::isProcessorPoly(osc->getProcessor())) << "poly is never forced on or off";
    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, DuplicateIntoChannelGivesAnIndependentCopyAndLeavesTheOtherTrackOnTheOriginal) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::SharedLfo);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto& rig = setup.rig;
    makeChannelFromHeaderCFT(mc, setup.trackA);
    auto& editor = mc.getGraphEditor();
    const auto* macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);

    auto* lfoComp = compForCFT(editor, rig.sharedLfo->nodeID);
    ASSERT_NE(lfoComp, nullptr);
    const auto menu = rightClickModuleMenuCFT(*lfoComp);
    const auto* item = findMenuItemByTextCFT(menu, "Duplicate into Channel: Lead");
    ASSERT_NE(item, nullptr) << "a module shared into a channel from outside offers Duplicate into Channel";
    ASSERT_TRUE(item->action != nullptr);

    // Every hidden attenuverter fed by `source`'s output, with its "amount" parameter.
    auto amountsFedBy = [&graph](const juce::AudioProcessorGraph::Node* source) {
        std::vector<juce::RangedAudioParameter*> amounts;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID != source->nodeID)
                continue;
            auto* dest = graph.getNodeForId(c.destination.nodeID);
            if (!isModuleOfTypeCFT(dest, ModuleType::Attenuverter))
                continue;
            for (auto* p : dest->getProcessor()->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
                    if (ranged->getParameterID() == "amount")
                        amounts.push_back(ranged);
        }
        return amounts;
    };
    // A non-default routing depth, so a silently reset amount on the copy would show.
    constexpr float kAmount = 0.37f;
    for (auto* amount : amountsFedBy(rig.sharedLfo))
        amount->setValueNotifyingHost(amount->convertTo0to1(kAmount));

    const int lfosBefore = countNodesOfTypeCFT(graph, ModuleType::LFO);
    const auto before = snapshotCFT(mc);
    item->action();

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::LFO), lfosBefore + 1);
    macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);
    juce::AudioProcessorGraph::Node* copy = nullptr;
    for (auto* node : graph.getNodes())
        if (isModuleOfTypeCFT(node, ModuleType::LFO) && node != rig.ownLfo && macro->hasMember(nodeUuid(node)))
            copy = node;
    ASSERT_NE(copy, nullptr) << "the copy lives inside this channel's macro";
    EXPECT_FALSE(macro->hasMember(nodeUuid(rig.sharedLfo))) << "the original stays outside";

    EXPECT_TRUE(modulatesCFT(graph, copy, rig.filterA, rig.cutoffChannel)) << "this channel is rewired to the copy";
    EXPECT_FALSE(modulatesCFT(graph, rig.sharedLfo, rig.filterA, rig.cutoffChannel));
    EXPECT_TRUE(modulatesCFT(graph, rig.sharedLfo, rig.filterB, rig.cutoffChannel))
        << "the other track stays on the original";

    // The copy's modulation keeps the original routing's depth.
    const auto copyAmounts = amountsFedBy(copy);
    ASSERT_FALSE(copyAmounts.empty()) << "the copy modulates through its own attenuverter";
    for (auto* amount : copyAmounts)
        EXPECT_NEAR(amount->convertFrom0to1(amount->getValue()), kAmount, 1e-4f);

    // Independent: retuning the copy leaves the original alone.
    auto* originalParam = rig.sharedLfo->getProcessor()->getParameters()[0];
    auto* copyParam = copy->getProcessor()->getParameters()[0];
    EXPECT_FLOAT_EQ(copyParam->getValue(), originalParam->getValue()) << "the copy starts with the same settings";
    const float originalValue = originalParam->getValue();
    copyParam->setValueNotifyingHost(originalValue > 0.5f ? 0.1f : 0.9f);
    EXPECT_FLOAT_EQ(originalParam->getValue(), originalValue);
    copyParam->setValueNotifyingHost(originalValue);

    expectOneUndoStepCFT(mc, before);
}
