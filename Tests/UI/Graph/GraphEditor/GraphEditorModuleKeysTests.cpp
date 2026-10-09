// GraphEditorModuleKeysTests.cpp
//
// The keyboard actions on the one selected module card (CanvasCardKeyboard): Rename opens the card's inline
// title editor, Replace opens the "Replace with..." picker. Both act only on a single selected card, and
// refuse exactly where the card's context menu does.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/AudioInputModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include <gtest/gtest.h>

namespace {

struct ModuleKeysFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::AudioProcessorGraph::NodeID oscId, filterId, inputId, attId;

    ModuleKeysFixture() {
        editor.setSize(900, 600);
        auto& graph = engine.getGraph();
        oscId = graph.addNode(std::make_unique<OscillatorModule>())->nodeID;
        filterId = graph.addNode(std::make_unique<FilterModule>())->nodeID;
        inputId = graph.addNode(std::make_unique<AudioInputModule>())->nodeID;
        attId = graph.addNode(std::make_unique<AttenuverterModule>())->nodeID;
        graph.getNodeForId(oscId)->properties.set("x", 120);
        graph.getNodeForId(oscId)->properties.set("y", 80);
        graph.getNodeForId(filterId)->properties.set("x", 500);
        graph.getNodeForId(filterId)->properties.set("y", 80);
        graph.getNodeForId(inputId)->properties.set("x", 120);
        graph.getNodeForId(inputId)->properties.set("y", 350);
        graph.getNodeForId(attId)->properties.set("x", 500);
        graph.getNodeForId(attId)->properties.set("y", 350);
        engine.updateModuleNames();
        editor.updateComponents();
    }

    CanvasCardKeyboard& keys() { return editor.getCardKeyboard(); }

    ModuleComponent* card(juce::AudioProcessorGraph::NodeID id) {
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->getNodeId() == id)
                return c;
        return nullptr;
    }
};

} // namespace

TEST(GraphEditorModuleKeys, RenameOpensTheSelectedCardsTitleEditorAndACommitChangesTheTitle) {
    ModuleKeysFixture f;
    f.editor.selectModule(f.oscId, false);
    ASSERT_TRUE(f.keys().canRenameSelectedModule());
    ASSERT_TRUE(f.keys().renameSelectedModule());

    auto* osc = f.card(f.oscId);
    ASSERT_NE(osc, nullptr);
    ASSERT_TRUE(osc->isRenamingTitle());
    auto* ed = dynamic_cast<juce::TextEditor*>(osc->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    ed->setText("Lead Saw", juce::dontSendNotification);
    osc->finishTitleRename(true);
    EXPECT_EQ(osc->cardTitle(), "Lead Saw");
}

TEST(GraphEditorModuleKeys, ReplaceOpensThePickerAndChoosingARowReplacesTheModule) {
    ModuleKeysFixture f;
    f.editor.selectModule(f.oscId, false);
    ASSERT_TRUE(f.keys().canReplaceSelectedModule());

    std::unique_ptr<synth::ui::ModMatrixPicker> captured;
    synth::ui::test_hooks::replacePickerHookForTest() = [&captured](std::unique_ptr<synth::ui::ModMatrixPicker> p) {
        captured = std::move(p);
    };
    const bool opened = f.keys().replaceSelectedModule();
    synth::ui::test_hooks::replacePickerHookForTest() = nullptr;
    ASSERT_TRUE(opened);
    ASSERT_NE(captured, nullptr);

    captured->setSearchTextForTest("Chorus");
    EXPECT_TRUE(captured->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.engine.getGraph().getNodeForId(f.oscId), nullptr) << "the oscillator was swapped out";
}

TEST(GraphEditorModuleKeys, BothAreRefusedWithNoSelectionOrSeveralCards) {
    ModuleKeysFixture f;
    EXPECT_EQ(f.keys().getSingleSelectedCard(), nullptr);
    EXPECT_FALSE(f.keys().canRenameSelectedModule());
    EXPECT_FALSE(f.keys().canReplaceSelectedModule());
    EXPECT_FALSE(f.keys().renameSelectedModule());
    EXPECT_FALSE(f.keys().replaceSelectedModule());

    f.editor.selectModule(f.oscId, false);
    f.editor.selectModule(f.filterId, true);
    EXPECT_EQ(f.keys().getSingleSelectedCard(), nullptr);
    EXPECT_FALSE(f.keys().canRenameSelectedModule());
    EXPECT_FALSE(f.keys().canReplaceSelectedModule());
    EXPECT_FALSE(f.card(f.oscId)->isRenamingTitle());
}

TEST(GraphEditorModuleKeys, ReplaceIsRefusedForTheAudioInputSingletonButRenameIsNot) {
    ModuleKeysFixture f;
    f.editor.selectModule(f.inputId, false);
    EXPECT_FALSE(f.keys().canReplaceSelectedModule());
    EXPECT_FALSE(f.keys().replaceSelectedModule());
    EXPECT_TRUE(f.keys().canRenameSelectedModule());
}

TEST(GraphEditorModuleKeys, RenameIsRefusedForAnAttenuverterWhichHasNoHeader) {
    ModuleKeysFixture f;
    f.editor.selectModule(f.attId, false);
    EXPECT_FALSE(f.keys().canRenameSelectedModule());
    EXPECT_FALSE(f.keys().renameSelectedModule());
}

TEST(GraphEditorModuleKeys, TheContextMenuOffersRenameAndReplaceWithTheLiveKeyBindings) {
    ModuleKeysFixture f;
    ShortcutManager shortcuts;
    f.keys().setShortcutManager(&shortcuts);
    f.editor.selectModule(f.oscId, false);

    ASSERT_NE(f.card(f.oscId), nullptr);
    auto menu = f.card(f.oscId)->buildModuleContextMenu();
    juce::String renameKey, replaceKey;
    bool sawRename = false, sawReplace = false;
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next()) {
        if (it.getItem().text == "Rename") {
            sawRename = true;
            renameKey = it.getItem().shortcutKeyDescription;
        }
        if (it.getItem().text == "Replace with...") {
            sawReplace = true;
            replaceKey = it.getItem().shortcutKeyDescription;
        }
    }
    EXPECT_TRUE(sawRename);
    EXPECT_TRUE(sawReplace);
    EXPECT_EQ(renameKey, shortcuts.getBinding("renameSelectedModule").getTextDescriptionWithIcons());
    EXPECT_EQ(replaceKey, shortcuts.getBinding("replaceSelectedModule").getTextDescriptionWithIcons());
    EXPECT_TRUE(renameKey.isNotEmpty());

    f.keys().setShortcutManager(nullptr);
}
