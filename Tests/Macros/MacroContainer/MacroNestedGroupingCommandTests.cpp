// MacroNestedGroupingCommandTests.cpp
// Cmd+G through the app's real command table (AppCommands::groupSelection, MainComponent's
// canGroupSelection predicate and perform body), for the two selections whose Cmd+G result changed
// when grouping learned to nest: a whole macro plus a loose module, and two modules inside an open macro.
// The full matrix is in MacroNestedGroupingTests.cpp, driven one layer lower.

#include "../../App/FocusArbitration/FocusArbitrationTestFixture.h"

#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Drops a library module onto the canvas and returns the one oscillator-type node it added.
NodeID dropModule(MainComponent& mc, const juce::String& type, juce::Point<int> at) {
    auto& graph = mc.getAudioEngine().getGraph();
    std::set<juce::uint32> before;
    for (auto* node : graph.getNodes())
        before.insert(node->nodeID.uid);
    mc.getGraphEditor().itemDropped(juce::DragAndDropTarget::SourceDetails(type, &mc.getGraphEditor(), at));
    for (auto* node : graph.getNodes())
        if (before.count(node->nodeID.uid) == 0 && node->getProcessor() != nullptr &&
            node->getProcessor()->getName() == type)
            return node->nodeID;
    return {};
}

juce::String ownerOf(MainComponent& mc, NodeID id) {
    auto* node = mc.getAudioEngine().getGraph().getNodeForId(id);
    const auto* macro =
        node != nullptr ? mc.getGraphEditor().getMacros().findByMember(node->properties["uuid"].toString()) : nullptr;
    return macro != nullptr ? macro->id : juce::String();
}

} // namespace

TEST_F(FocusArbitrationTest, CmdGNestsAWholeMacroAndALooseModuleThroughTheCommandTable) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();
    editor.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);

    const auto a = dropModule(mc, "Oscillator", {200, 200});
    const auto b = dropModule(mc, "Oscillator", {200, 600});
    const auto c = dropModule(mc, "Oscillator", {900, 400});
    ASSERT_TRUE(a.uid != 0 && b.uid != 0 && c.uid != 0);

    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::groupSelection, false));
    const auto childId = ownerOf(mc, a);
    ASSERT_FALSE(childId.isEmpty()) << "two loose top-level modules group, as always";

    editor.getMacroController().selectMacro(childId, false);
    auto selection = editor.getSelectedNodes();
    selection.push_back(c);
    editor.setSelectedNodes(selection);
    ASSERT_TRUE(commandIsActive(mc, AppCommands::groupSelection));
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::groupSelection, false));

    const auto parentId = ownerOf(mc, c);
    ASSERT_FALSE(parentId.isEmpty()) << "a whole macro plus a loose module nests";
    EXPECT_EQ(editor.getMacros().parentOf(childId), parentId);
    EXPECT_EQ(ownerOf(mc, a), childId);
}

TEST_F(FocusArbitrationTest, CmdGGroupsTwoModulesInsideAnOpenMacroThroughTheCommandTable) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();
    editor.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);

    const auto a = dropModule(mc, "Oscillator", {200, 200});
    const auto b = dropModule(mc, "Oscillator", {200, 600});
    const auto c = dropModule(mc, "Oscillator", {900, 400});
    editor.setSelectedNodes({a, b, c});
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::groupSelection, false));
    const auto outerId = ownerOf(mc, a);
    ASSERT_FALSE(outerId.isEmpty());
    editor.getMacroController().setMacroCollapsed(outerId, false);

    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::groupSelection, false));

    const auto innerId = ownerOf(mc, a);
    ASSERT_NE(innerId, outerId) << "two of an open macro's modules group inside it";
    EXPECT_EQ(editor.getMacros().parentOf(innerId), outerId);
    EXPECT_EQ(ownerOf(mc, c), outerId);
    EXPECT_FALSE(editor.getMacros().find(outerId)->collapsed) << "the open macro is not collapsed";
}
