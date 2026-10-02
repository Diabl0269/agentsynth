// Concern: New and Open start a fresh undo history (Cmd+Z right after either does nothing).
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <set>
#include <vector>

namespace {
// The dirty flag follows UndoManager's async change broadcast, so assertions need a dispatch pass.
void pumpMessageLoop() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }
} // namespace

// Before the fix, Cmd+Z after File > New restored the canvas without its macros or tracks.
TEST_F(MainComponentTest, NewPatchStartsAFreshUndoHistory) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();

    std::set<juce::AudioProcessorGraph::NodeID> before;
    for (auto* n : graph.getNodes())
        before.insert(n->nodeID);
    editor.addModuleAtCanvasPosition("Oscillator", {1440, 1040}, {});
    editor.addModuleAtCanvasPosition("LFO", {1400, 1000}, {});
    std::vector<juce::AudioProcessorGraph::NodeID> added;
    for (auto* n : graph.getNodes())
        if (before.count(n->nodeID) == 0)
            added.push_back(n->nodeID);
    ASSERT_EQ(added.size(), 2u);
    editor.setSelectedNodes({added[0], added[1]});
    ASSERT_FALSE(editor.getMacroController().groupSelectionIntoMacro().isEmpty());
    mc.simulateAddMidiTrackClick();
    pumpMessageLoop();
    ASSERT_FALSE(mc.getTimelineDoc().getTracks().empty());
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    // The document is dirty, so New goes through the unsaved-changes guard; answer Discard.
    mc.unsavedChangesPrompt = [](const juce::String&,
                                 std::function<void(MainComponent::UnsavedChangesChoice)> onChoice) {
        onChoice(MainComponent::UnsavedChangesChoice::Discard);
    };

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));
    pumpMessageLoop();

    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "New must start an empty undo history";
    EXPECT_FALSE(mc.isProjectDirty());
    const int nodesAfterNew = graph.getNumNodes();
    const auto tracksAfterNew = mc.getTimelineDoc().getTracks().size();
    mc.getUndoManager().undo();
    EXPECT_EQ(graph.getNumNodes(), nodesAfterNew) << "Cmd+Z after New must not restore the old graph";
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), tracksAfterNew) << "...nor the old tracks";
    EXPECT_TRUE(mc.getTimelineDoc().getTracks().empty());
}

TEST_F(MainComponentTest, OpeningAProjectStartsAFreshUndoHistory) {
    const auto bundleDir = tempRoot.getChildFile("FreshUndo.agsproj");
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().addModuleAtCanvasPosition("Oscillator", {1440, 1040}, {});
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));
    mc.simulateAddMidiTrackClick(); // an edit after the save: history is non-empty before the open
    pumpMessageLoop();
    ASSERT_TRUE(mc.getUndoManager().canUndo());

    ASSERT_TRUE(mc.openProjectForTest(bundleDir));
    pumpMessageLoop();

    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "Open must start an empty undo history";
    EXPECT_FALSE(mc.isProjectDirty());
}
