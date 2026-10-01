// CardLayoutEditorKeyboardTests.cpp
//
// The card layout editor without a mouse: Up/Down between rows, Space shows or hides, Cmd+Up/Down
// moves, Enter renames (the last three rebindable Layout Editor actions), every control named and
// tooltipped, and Escape left to the CallOutBox around it.

#include "../../Accessibility/AccessibilityAudit.h"
#include "CardLayoutEditorTestHelpers.h"
#include "Modules/FilterModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"

using namespace cardlayouteditor_test;

namespace {
const juce::KeyPress kDown(juce::KeyPress::downKey);
const juce::KeyPress kUp(juce::KeyPress::upKey);
const juce::KeyPress kSpace(juce::KeyPress::spaceKey);
const juce::KeyPress kReturn(juce::KeyPress::returnKey);
const juce::KeyPress kCmdDown(juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCmdUp(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0);
} // namespace

// The whole flow by keys alone: walk to Drive, hide it, move Resonance down, rename Cutoff.
TEST(CardLayoutEditorKeyboard, AKeyboardOnlySessionHidesMovesAndRenames) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);

    // Down walks the rows one at a time, clamped at the end.
    int row = 0;
    while (editor->getVisibleRowParamIdForTest(row) != "drive") {
        ASSERT_TRUE(editor->pressKeyOnRowForTest(row, kDown));
        ++row;
        ASSERT_EQ(editor->getFocusedRowKeyForTest(), editor->getVisibleRowParamIdForTest(row));
    }
    ASSERT_TRUE(editor->pressKeyOnRowForTest(row, kSpace));
    EXPECT_FALSE(rig.card(id)->getCardBody()->findWidget("drive")->isVisible());
    ASSERT_TRUE(editor->pressKeyOnRowForTest(row, kUp));
    EXPECT_EQ(editor->getFocusedRowKeyForTest(), editor->getVisibleRowParamIdForTest(row - 1));

    const int resonance = rowOf(*editor, "resonance");
    const auto below = editor->getVisibleRowParamIdForTest(resonance + 1);
    ASSERT_TRUE(editor->pressKeyOnRowForTest(resonance, kCmdDown));
    EXPECT_EQ(editor->getVisibleRowParamIdForTest(rowOf(*editor, "resonance") - 1), below);
    EXPECT_EQ(editor->getFocusedRowKeyForTest(), "resonance") << "the moved row keeps the focus";
    ASSERT_TRUE(editor->pressKeyOnRowForTest(rowOf(*editor, "resonance"), kCmdUp));
    EXPECT_EQ(editor->getVisibleRowParamIdForTest(rowOf(*editor, "resonance") + 1), below);

    const int cutoff = rowOf(*editor, "cutoff");
    ASSERT_TRUE(editor->pressKeyOnRowForTest(cutoff, kReturn));
    auto& name = editor->getRowForTest(cutoff)->getNameLabelForTest();
    auto* field = name.getCurrentTextEditor();
    ASSERT_NE(field, nullptr) << "Enter opened the name for editing";
    field->setText("Freq", false);
    name.hideEditor(false); // what Return in the field does
    EXPECT_EQ(captionOf(*rig.card(id), "cutoff"), "Freq");
}

TEST(CardLayoutEditorKeyboard, TheEditorKeysAreRebindableLayoutEditorActions) {
    for (const auto* id :
         {"layoutEditorToggleShown", "layoutEditorMoveUp", "layoutEditorMoveDown", "layoutEditorRename"})
        EXPECT_EQ(ShortcutManager::getCategory(id), ShortcutCategory::LayoutEditor) << id;

    ShortcutManager shortcuts;
    shortcuts.setBinding("layoutEditorToggleShown", juce::KeyPress('h', juce::ModifierKeys::noModifiers, 0));
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    rig.canvas.editor.getCardKeyboard().setShortcutManager(&shortcuts);
    auto* editor = rig.openFromControl(id, "drive");
    ASSERT_NE(editor, nullptr);

    const int drive = rowOf(*editor, "drive");
    EXPECT_FALSE(editor->pressKeyOnRowForTest(drive, kSpace)) << "the old key no longer hides";
    EXPECT_TRUE(editor->pressKeyOnRowForTest(drive, juce::KeyPress('h', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_FALSE(rig.card(id)->getCardBody()->findWidget("drive")->isVisible());
    EXPECT_TRUE(editor->getRowForTest(drive)->getTooltip().contains("Show or hide: h,"))
        << editor->getRowForTest(drive)->getTooltip() << " names the key as bound now";
    rig.close();
    rig.canvas.editor.getCardKeyboard().setShortcutManager(nullptr);
}

TEST(CardLayoutEditorKeyboard, EscapeIsLeftForTheCallOutBoxToClose) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(editor->pressKeyOnRowForTest(0, juce::KeyPress(juce::KeyPress::escapeKey)));
}

TEST(CardLayoutEditorKeyboard, EveryRowIsAFocusStopWithAFocusRingNameAndTooltip) {
    EditorCanvas rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openFromControl(id, "cutoff");
    ASSERT_NE(editor, nullptr);
    editor->triggerAddGroupForTest();
    for (int i = 0; i < editor->getVisibleRowCountForTest(); ++i) {
        auto* row = editor->getRowForTest(i);
        SCOPED_TRACE(row->getKey().toStdString());
        EXPECT_TRUE(row->getWantsKeyboardFocus());
        EXPECT_TRUE(row->getTitle().isNotEmpty());
        EXPECT_TRUE(row->getTooltip().isNotEmpty());
    }
    const auto gaps = synth::test::auditAccessibility(*editor);
    for (const auto& gap : gaps)
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name: " : "missing tooltip: ")
                      << gap.path;
}
