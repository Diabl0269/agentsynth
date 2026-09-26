// MacroGroupControllerTests.cpp
//
// Controller-level coverage for MacroGroupController (FRO77 PR2): drives it directly against a
// real AudioEngine graph through GraphEditor::getCanvasHostForTest() (mirrors
// Tests/UI/Graph/SmartConnectionEngine/SmartConnectionEngineTests.cpp from PR1) — the test never
// goes through GraphEditor's own forwarders, it builds a SEPARATE MacroGroupController instance
// and drives its own API directly, proving the controller works in isolation from GraphEditor.
// The existing Tests/Macros/MacroContainer/*Tests.cpp files cover the same behaviour through
// GraphEditor's forwarders and are unchanged by this PR.

#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

/** Adds a module, lays out its ModuleComponent at a known position, and gives it a real
 *  persistent uuid up front — macro membership is keyed by uuid, and a plain graph.addNode() a
 *  bare test would use leaves it empty until something like graphToJSON assigns one lazily
 *  (mirrors Tests/Macros/MacroContainer/MacroContainerTestHelpers.h's addModuleAt). */
NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

} // namespace

TEST(MacroGroupControllerTest, GroupSelectionIntoMacroCreatesOneMacroWithBothMembers) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto uuidA = uuidOf(engine, a);
    auto uuidB = uuidOf(engine, b);

    editor.setSelectedNodes({a, b});

    MacroGroupController controllerUnderTest(editor.getCanvasHostForTest());
    auto macroId = controllerUnderTest.groupSelectionIntoMacro();

    ASSERT_FALSE(macroId.isEmpty());
    EXPECT_EQ(editor.getMacros().size(), 1);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u);
    EXPECT_TRUE(macro->hasMember(uuidA));
    EXPECT_TRUE(macro->hasMember(uuidB));
}

TEST(MacroGroupControllerTest, UngroupSelectionDissolvesTheMacroAndKeepsTheModules) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});

    MacroGroupController controllerUnderTest(editor.getCanvasHostForTest());
    auto macroId = controllerUnderTest.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // groupSelectionIntoMacro leaves the two members selected (matches
    // GraphEditor::groupSelectionIntoMacro's own contract) — ungroup reads the current selection.
    controllerUnderTest.ungroupSelection();

    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_NE(engine.getGraph().getNodeForId(a), nullptr) << "ungroup keeps the modules";
    EXPECT_NE(engine.getGraph().getNodeForId(b), nullptr);
}

TEST(MacroGroupControllerTest, CrossingPlanForABoundaryCrossingCableYieldsOnePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // a/b would become macro members; c stays outside and is wired to b, so the plan must expose
    // exactly one crossing group on b's input jack.
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);

    editor.connectPorts(a, 0, b, 0, false, false);
    editor.connectPorts(b, 0, c, 0, false, false);

    MacroGroupController controllerUnderTest(editor.getCanvasHostForTest());
    const auto plan = controllerUnderTest.buildMacroPortCrossingPlan(std::vector<NodeID>{a, b});

    ASSERT_EQ(plan.size(), 1u) << "only b->c crosses the would-be macro's boundary; a->b is interior";
    EXPECT_EQ(plan.front().internalNodeId, b);
    EXPECT_FALSE(plan.front().isInput) << "b's OUTPUT jack is what crosses out to c";
}

TEST(MacroGroupControllerTest, ToggleSelectionMacrosCollapsedFlipsTheMacrosCollapsedFlag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});

    MacroGroupController controllerUnderTest(editor.getCanvasHostForTest());
    auto macroId = controllerUnderTest.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const bool startedCollapsed = macro->collapsed;

    // groupSelectionIntoMacro leaves the members selected; toggle reads that selection.
    controllerUnderTest.toggleSelectionMacrosCollapsed();
    EXPECT_EQ(editor.getMacros().find(macroId)->collapsed, !startedCollapsed);

    controllerUnderTest.toggleSelectionMacrosCollapsed();
    EXPECT_EQ(editor.getMacros().find(macroId)->collapsed, startedCollapsed);
}
