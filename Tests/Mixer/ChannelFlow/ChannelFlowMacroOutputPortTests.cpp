// ChannelFlowMacroOutputPortTests.cpp
//
// Every track's sound visibly leaves through its own macro's OUTPUT PORT into Master
// (docs/macros/auto-ports.md#track-outputs): Core builds Strip -> Master Mix as two plain edges, then the app moves
// them behind ONE outlet of the track's macro -- a single stereo jack by default, two jacks with the "Split Left/Right
// jacks" preference. Covers the track / instrument / preset / Make Channel / undo / save-and-reopen paths, solo through
// the port, and the right-click "Split into Left/Right Jacks" / "Join into One Stereo Jack" switch. Shared fixture
// helpers live in ChannelFlowTestFixture.h.

#include "../TrackPreset/TrackPresetTestFixture.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/SoloAudibleSet.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/MasterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

using Node = juce::AudioProcessorGraph::Node;
constexpr int kRight = ChannelStripModule::kRightBase;
constexpr int kPortRight = MacroOutletModule::kRightBase;

int dualIOOfNodeMOP(Node* node) {
    if (node == nullptr)
        return -2;
    for (auto* p : node->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            return p->getValue() > 0.5f ? 1 : 0;
    return -1;
}

MacroPortShape shapeOf(Node* outlet) {
    return dynamic_cast<MacroOutletModule*>(outlet->getProcessor())->getPortShape();
}

// The ONE outlet `strip` feeds, or nullptr (also nullptr when it feeds several).
Node* onlyOutlet(juce::AudioProcessorGraph& graph, Node* strip) {
    const auto outlets = outletsFedByStripCFT(graph, strip);
    return outlets.size() == 1 ? outlets.front() : nullptr;
}

// strip -> outlet -> Master Mix, collapsed shape: one stereo jack, raw legs 0 / 1.
void expectCollapsedOutletIntoMix(juce::AudioProcessorGraph& graph, Node* strip, Node* outlet, Node* master) {
    ASSERT_NE(outlet, nullptr);
    EXPECT_EQ(shapeOf(outlet), MacroPortShape::StereoCollapsed);
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {outlet->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, kRight}, {outlet->nodeID, 1}}));
    EXPECT_TRUE(graph.isConnected({{outlet->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected({{outlet->nodeID, 1}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_FALSE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "the strip no longer reaches Master by a plain edge";
    EXPECT_FALSE(graph.isConnected({{strip->nodeID, kRight}, {master->nodeID, MasterModule::kMixRight}}));
}

// strip -> outlet -> Master Mix, split shape: two jacks, raw legs 0 / kRightBase.
void expectSplitOutletIntoMix(juce::AudioProcessorGraph& graph, Node* strip, Node* outlet, Node* master) {
    ASSERT_NE(outlet, nullptr);
    EXPECT_EQ(shapeOf(outlet), MacroPortShape::Stereo);
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {outlet->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, kRight}, {outlet->nodeID, kPortRight}}));
    EXPECT_TRUE(graph.isConnected({{outlet->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected({{outlet->nodeID, kPortRight}, {master->nodeID, MasterModule::kMixRight}}));
}

int outletCount(juce::AudioProcessorGraph& graph) { return countNodesOfTypeCFT(graph, ModuleType::MacroOutlet); }

// Right-clicks `comp` like a mouse (mouseDown, right button) and returns the captured menu.
juce::PopupMenu rightClickPortMenu(ModuleComponent& comp) {
    juce::PopupMenu captured;
    comp.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> centre(comp.getWidth() / 2, comp.getHeight() / 2);
    comp.mouseDown(
        realMouseEventCFT(comp, centre, centre, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    comp.setShowContextMenuHookForTest(nullptr);
    return captured;
}

} // namespace

TEST_F(ChannelFlowTest, AudioTrackLeavesItsMacroThroughOneStereoOutletIntoMix) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    auto* outlet = onlyOutlet(graph, strip);
    expectCollapsedOutletIntoMix(graph, strip, outlet, master);
    ASSERT_NE(outlet, nullptr);

    const auto* macro = mc.getGraphEditor().getMacros().findByMember(nodeUuid(strip));
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->memberIsPort(nodeUuid(outlet))) << "the outlet is a member port of the track's macro";
    EXPECT_FALSE(macro->hasMember(nodeUuid(master)));
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports.front().isInput);
    EXPECT_EQ(macro->ports.front().name, "Output");
    EXPECT_EQ(outletCount(graph), 1);

    bool viaOutlet = false;
    EXPECT_TRUE(stripFeedsMasterMixCFT(graph, strip, master, &viaOutlet));
    EXPECT_TRUE(viaOutlet);
    // The mixer's Mix/Direct classification looks through the port.
    EXPECT_EQ(synth::resolveSourceThroughPorts(graph, graph.getConnections(), {outlet->nodeID, 0}).nodeID,
              strip->nodeID);
}

TEST_F(ChannelFlowTest, ThreeTracksGetThreeOutletsEachIntoMix) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);
    addAudioTrack(mc);
    addAudioTrack(mc);

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
    EXPECT_EQ(outletCount(graph), 3);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    int strips = 0;
    for (auto* node : graph.getNodes()) {
        if (!isModuleOfTypeCFT(node, ModuleType::ChannelStrip))
            continue;
        ++strips;
        expectCollapsedOutletIntoMix(graph, node, onlyOutlet(graph, node), master);
    }
    EXPECT_EQ(strips, 3);
}

TEST_F(ChannelFlowTest, SplitJacksPreferenceGivesATwoJackOutletAndADualMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);

    addAudioTrack(mc);

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    expectSplitOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    EXPECT_EQ(dualIOOfNodeMOP(master), 1) << "a Master created with the preference on is dual";
}

TEST_F(ChannelFlowTest, SingleJackPreferenceGivesACollapsedMaster) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(false);

    addAudioTrack(mc);

    EXPECT_EQ(dualIOOfNodeMOP(findNodeOfTypeCFT(graph, ModuleType::Master)), 0);
}

TEST_F(ChannelFlowTest, InstrumentTrackLeavesItsMacroThroughOneStereoOutletIntoMix) {
    // The plugin-instrument track shares buildInstrumentChannelAndMacro with this path, and its own tests assert
    // the strip reaches Master Mix (directly or through the port) with the same helper.
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    EXPECT_EQ(outletCount(graph), 1);
}

TEST_F(ChannelFlowTest, InstrumentTrackFollowsTheSplitJacksPreference) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);

    addInstrumentTrack(mc, "Sampler");

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    expectSplitOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
}

TEST_F(ChannelFlowTest, OneUndoRemovesTheTrackItsMacroAndItsPortNodeAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto before = snapshotCFT(mc);
    ASSERT_EQ(outletCount(graph), 0);

    addAudioTrack(mc);
    ASSERT_EQ(outletCount(graph), 1);

    expectOneUndoStepCFT(mc, before);
    // expectOneUndoStepCFT ends redone; undo again for the "gone" assertions.
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(outletCount(graph), 0) << "the port node goes with the track";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0);
    EXPECT_TRUE(mc.getGraphEditor().getMacros().getAll().empty());
    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(outletCount(graph), 1);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(strip, nullptr);
    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), findNodeOfTypeCFT(graph, ModuleType::Master));
}

TEST_F(ChannelFlowTest, SavedAndReopenedProjectKeepsTheOutletIntoMixAndSoloStillGates) {
    juce::var graphJson, macrosJson;
    {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        addAudioTrack(mc);
        addAudioTrack(mc);
        graphJson = synth::AIStateMapper::graphToJSON(mc.getAudioEngine().getGraph());
        macrosJson = mc.getGraphEditor().getMacros().toVar();
    }

    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(graphJson, graph, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_TRUE(editor.getMacros().fromVar(macrosJson));
    editor.updateComponents();

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    std::vector<Node*> strips;
    for (auto* node : graph.getNodes())
        if (isModuleOfTypeCFT(node, ModuleType::ChannelStrip))
            strips.push_back(node);
    ASSERT_EQ(strips.size(), 2u);
    EXPECT_EQ(outletCount(graph), 2);
    for (auto* strip : strips) {
        expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
        const auto* macro = editor.getMacros().findByMember(nodeUuid(strip));
        ASSERT_NE(macro, nullptr);
        EXPECT_TRUE(macro->memberIsPort(nodeUuid(onlyOutlet(graph, strip))));
    }

    // Solo track 1: its own strip stays fully audible, track 2's strip (behind its outlet) is gated.
    dynamic_cast<ChannelStripModule*>(strips[0]->getProcessor())->setSoloed(true);
    const auto masks = synth::computeSoloAudibleLegs(graph);
    EXPECT_EQ(masks.at(strips[0]->nodeID), ~0u);
    EXPECT_EQ(masks.at(strips[1]->nodeID) & ChannelStripModule::kMainLegBit, 0u)
        << "a strip behind a macro port is still silenced by another track's solo";
}

TEST_F(ChannelFlowTest, PresetInsertedTrackGetsExactlyOneOutletIntoMixAndNoDanglingPort) {
    constexpr const char* kPresetName = "__FRO510_Test_Outlet_Preset__";
    constexpr const char* kDefaultKey = "mixerDefaultTrackPresetAudio";
    const auto presetsDir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    synth::TrackPresetManager::deleteTrackPreset(presetsDir, kPresetName);

    // Save a preset from a channel that already leaves through an outlet (a preset never captures Master, so the
    // captured port is fed by the strip with nothing outside).
    {
        HostedPatchCFT patch;
        GraphEditor editor(patch.engine);
        const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
        ASSERT_NE(rig.macro, nullptr);
        auto* strip = findNodeOfTypeCFT(patch.engine.getGraph(), ModuleType::ChannelStrip);
        ASSERT_NE(strip, nullptr);
        ASSERT_NE(onlyOutlet(patch.engine.getGraph(), strip), nullptr) << "premise: the source channel has an outlet";
        auto preset = synth::TrackPresetManager::extractTrackPreset(
            patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Audio, kPresetName);
        ASSERT_TRUE(preset.isObject());
        ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(presetsDir, kPresetName, preset));
    }

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.getAppPropertiesForTest().getUserSettings()->setValue(kDefaultKey, kPresetName);
    auto& graph = mc.getAudioEngine().getGraph();

    addAudioTrack(mc);
    mc.getAppPropertiesForTest().getUserSettings()->removeValue(kDefaultKey);
    synth::TrackPresetManager::deleteTrackPreset(presetsDir, kPresetName);

    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    EXPECT_EQ(outletCount(graph), 1) << "the preset's captured outlet was swept, not kept beside the new one";
    const auto* macro = mc.getGraphEditor().getMacros().findByMember(nodeUuid(strip));
    ASSERT_NE(macro, nullptr);
    int outputPorts = 0;
    for (const auto& port : macro->ports)
        outputPorts += port.isInput ? 0 : 1;
    EXPECT_EQ(outputPorts, 1);
}

TEST_F(ChannelFlowTest, MakeChannelMovesEachStripReachingMasterIntoAPort) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto setup = buildMcRigCFT(mc, RigShapeCFT::Merge);
    ASSERT_NE(setup.rig.trackInA, nullptr);
    const auto before = snapshotCFT(mc);

    makeChannelFromHeaderCFT(mc, setup.trackA);

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    std::vector<Node*> strips;
    for (auto* node : graph.getNodes())
        if (isModuleOfTypeCFT(node, ModuleType::ChannelStrip))
            strips.push_back(node);
    ASSERT_EQ(strips.size(), 2u) << "the track's channel and the merge point's bus";
    // The merge point's bus is the one that reaches Master, through its own output port. The track's channel
    // reaches Master only through the bus, so it has no Master edge and no port of its own.
    int busStrips = 0;
    for (auto* strip : strips) {
        if (outletsFedByStripCFT(graph, strip).empty()) {
            EXPECT_FALSE(stripFeedsMasterMixCFT(graph, strip, master));
            continue;
        }
        ++busStrips;
        expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    }
    EXPECT_EQ(busStrips, 1);
    EXPECT_EQ(outletCount(graph), 1);

    expectOneUndoStepCFT(mc, before);
}

TEST_F(ChannelFlowTest, RightClickTogglesTheOutletBetweenOneAndTwoJacksKeepingBothLegsWired) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    addAudioTrack(mc);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    auto& editor = mc.getGraphEditor();

    auto chooseFromPortMenu = [&](const juce::String& text) {
        auto* outlet = onlyOutlet(graph, strip);
        ASSERT_NE(outlet, nullptr);
        auto* comp = compForCFT(editor, outlet->nodeID);
        ASSERT_NE(comp, nullptr) << "the port has a module component";
        const auto menu = rightClickPortMenu(*comp);
        const auto* item = findMenuItemByTextCFT(menu, text);
        ASSERT_NE(item, nullptr) << "the port menu offers " << text.toStdString();
        EXPECT_TRUE(item->isEnabled);
        ASSERT_TRUE(static_cast<bool>(item->action));
        item->action();
    };

    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);

    chooseFromPortMenu("Split into Left/Right Jacks");
    expectSplitOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    EXPECT_EQ(outletCount(graph), 1);

    chooseFromPortMenu("Join into One Stereo Jack");
    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    EXPECT_EQ(outletCount(graph), 1);

    // Each switch is ONE undo step.
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSplitOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
}

TEST_F(ChannelFlowTest, TheStereoPortTooltipExplainsSplitAndJoin) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    addAudioTrack(mc);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(strip, nullptr);
    auto* outlet = onlyOutlet(graph, strip);
    ASSERT_NE(outlet, nullptr);
    auto* comp = compForCFT(mc.getGraphEditor(), outlet->nodeID);
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->getTooltip().contains("Right-click to split or join the left/right jacks"));
}

// The collapsed track card is what a user sees: a right-click there opens the CARD's menu (the port widget sits
// inside the card), so the switch must be offered on that menu too, named after the port.
TEST_F(ChannelFlowTest, CollapsedTrackCardRightClickSplitsAndJoinsItsOutputPort) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    addAudioTrack(mc);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(master, nullptr);
    auto& editor = mc.getGraphEditor();
    const auto* macro = editor.getMacros().findByMember(strip->properties["uuid"].toString());
    ASSERT_NE(macro, nullptr);
    ASSERT_TRUE(macro->collapsed);
    const auto macroId = macro->id;

    auto chooseFromCardMenu = [&](const juce::String& text) {
        auto* card = editor.getMacroController().getMacroCardForTest(macroId);
        ASSERT_NE(card, nullptr) << "a collapsed track shows a macro card";
        juce::PopupMenu captured;
        card->setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
        const juce::Point<int> centre(card->getWidth() / 2, card->getHeight() / 2);
        card->mouseDown(
            realMouseEventCFT(*card, centre, centre, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
        card->setShowContextMenuHookForTest(nullptr);
        const auto* item = findMenuItemByTextCFT(captured, text);
        ASSERT_NE(item, nullptr) << "the card menu offers " << text.toStdString();
        ASSERT_TRUE(static_cast<bool>(item->action));
        item->action();
    };

    chooseFromCardMenu("Split Output into Left/Right Jacks");
    expectSplitOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    chooseFromCardMenu("Join Output into One Stereo Jack");
    expectCollapsedOutletIntoMix(graph, strip, onlyOutlet(graph, strip), master);
    EXPECT_EQ(outletCount(graph), 1);
}

// A two-jack Stereo port is TWO rows on the collapsed card ("Output L" / "Output R"), and each of its cables anchors on
// its own row's jack; a one-jack port stays one row.
TEST_F(ChannelFlowTest, SplitOutputPortShowsTwoRowsOnTheCollapsedCardAndEachCableAnchorsOnItsOwnRow) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    addAudioTrack(mc);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(strip, nullptr);
    auto& editor = mc.getGraphEditor();
    const auto* macro = editor.getMacros().findByMember(strip->properties["uuid"].toString());
    ASSERT_NE(macro, nullptr);
    const auto macroId = macro->id;

    auto outputRows = [&] {
        std::vector<GraphEditor::MacroCardPort> rows;
        for (const auto& p : editor.getMacroController().macroCardPortLayout(macroId))
            if (!p.isInput)
                rows.push_back(p);
        return rows;
    };
    auto chooseFromCardMenu = [&](const juce::String& text) {
        auto* card = editor.getMacroController().getMacroCardForTest(macroId);
        ASSERT_NE(card, nullptr);
        juce::PopupMenu captured;
        card->setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
        const juce::Point<int> centre(card->getWidth() / 2, card->getHeight() / 2);
        card->mouseDown(
            realMouseEventCFT(*card, centre, centre, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
        card->setShowContextMenuHookForTest(nullptr);
        const auto* item = findMenuItemByTextCFT(captured, text);
        ASSERT_NE(item, nullptr);
        item->action();
    };

    auto rows = outputRows();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].name, juce::String("Output"));
    EXPECT_EQ(rows[0].visibleJack, -1);
    const int heightOneJack = editor.getMacroController().getMacroCardForTest(macroId)->getHeight();

    chooseFromCardMenu("Split Output into Left/Right Jacks");
    rows = outputRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name, juce::String("Output L"));
    EXPECT_EQ(rows[1].name, juce::String("Output R"));
    EXPECT_EQ(rows[0].visibleJack, 0);
    EXPECT_EQ(rows[1].visibleJack, 1);
    EXPECT_EQ(rows[1].row, rows[0].row + 1);
    EXPECT_EQ(rows[1].jackPos.y - rows[0].jackPos.y, detail::kMacroPortRowHeight);
    // The card is sized from its rows (it never shrinks below its fixed footprint, so one extra row may fit inside it).
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getHeight(), detail::macroCardHeightFor(2));

    // Each cable out of the split port starts at its own row's jack.
    const auto cardPos = card->getBounds().getPosition();
    int leftSeen = 0, rightSeen = 0;
    for (const auto& c : editor.buildVisibleCables()) {
        if (c.id.srcUid == 0 || c.id.srcUid != onlyOutlet(graph, strip)->nodeID.uid)
            continue;
        const auto expected = (cardPos + rows[c.id.srcPort == kPortRight ? 1 : 0].jackPos).toFloat();
        EXPECT_EQ(c.p1, expected) << "source port " << c.id.srcPort;
        (c.id.srcPort == kPortRight ? rightSeen : leftSeen)++;
    }
    EXPECT_EQ(leftSeen, 1);
    EXPECT_EQ(rightSeen, 1);

    chooseFromCardMenu("Join Output into One Stereo Jack");
    rows = outputRows();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].name, juce::String("Output"));
    EXPECT_EQ(rows[0].visibleJack, -1);
    EXPECT_EQ(editor.getMacroController().getMacroCardForTest(macroId)->getHeight(), heightOneJack);
}
