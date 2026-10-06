// OnCardEditorKeyboardTests.cpp -- nudging a focused control with the arrow keys, Esc, and the
// shortcut actions the keys are bound through.

#include "OnCardTestHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardLayoutMath.h"

using namespace oncard_test;

namespace {

bool press(CardLayoutOnCardEditor& editor, const juce::String& key, int keyCode, int mods = 0) {
    return editor.getOutlineForTest(key)->keyPressed(juce::KeyPress(keyCode, juce::ModifierKeys(mods), 0));
}

} // namespace

TEST(OnCardEditorKeyboard, RightArrowMovesTheControlOnePixelAndShiftRightEightWrittenOnceTheKeysSettle) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto start = editor->getCellRectForTest("cutoff");

    EXPECT_TRUE(press(*editor, "cutoff", juce::KeyPress::rightKey));
    EXPECT_EQ(editor->getCellRectForTest("cutoff").getX(), start.getX() + 1) << "the control moves under the key";
    EXPECT_EQ(editor->getLastAnnouncementForTest(), "Cutoff moved right 1");
    EXPECT_TRUE(editor->hasPendingNudgeForTest());
    EXPECT_FALSE(rig.storedLayout(id).has_value()) << "nothing is written until the keys settle";

    editor->flushNudgeForTest();
    EXPECT_FALSE(editor->hasPendingNudgeForTest());
    const auto layout = rig.storedLayout(id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(editor->getCellRectForTest("cutoff").getX(), start.getX() + 1);
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), start.getX() + 1);

    EXPECT_TRUE(press(*editor, "cutoff", juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(editor->getLastAnnouncementForTest(), "Cutoff moved right 8");
    editor->flushNudgeForTest();
    EXPECT_EQ(editor->getCellRectForTest("cutoff").getX(), start.getX() + 9);
}

TEST(OnCardEditorKeyboard, HeldArrowsAreOneWriteAndEveryDirectionMoves) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto start = editor->getCellRectForTest("outputLevel");

    for (int i = 0; i < 3; ++i)
        press(*editor, "outputLevel", juce::KeyPress::downKey);
    press(*editor, "outputLevel", juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier);
    press(*editor, "outputLevel", juce::KeyPress::upKey);
    EXPECT_EQ(editor->getCellRectForTest("outputLevel").getPosition(), start.getPosition() + juce::Point<int>(-8, 2));
    editor->flushNudgeForTest();
    EXPECT_EQ(editor->getCellRectForTest("outputLevel").getPosition(), start.getPosition() + juce::Point<int>(-8, 2));
}

TEST(OnCardEditorKeyboard, NudgingOntoANeighbourPushesItAsideButANudgeBesideFlushRowsMovesNothingElse) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto resonance = editor->getCellRectForTest("resonance");

    press(*editor, "cutoff", juce::KeyPress::rightKey);
    editor->flushNudgeForTest();
    EXPECT_EQ(editor->getCellRectForTest("resonance"), resonance) << "the row under Cutoff did not budge";

    press(*editor, "cutoff", juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier);
    editor->flushNudgeForTest();
    EXPECT_NE(editor->getCellRectForTest("resonance"), resonance) << "Cutoff now overlaps it";
    EXPECT_FALSE(
        synth::ui::oncard::tooClose(editor->getCellRectForTest("cutoff"), editor->getCellRectForTest("resonance")));
}

TEST(OnCardEditorKeyboard, EscapeWithNoDragCancelsTheSessionAndOtherKeysAreLeftAlone) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(press(*editor, "cutoff", 'x'));
    EXPECT_TRUE(press(*editor, "cutoff", juce::KeyPress::escapeKey));
    EXPECT_TRUE(editor->isClosed());
}

TEST(OnCardEditorKeyboard, ReturnOnAControlAsksForItsOptions) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    juce::String asked;
    editor->onControlOptions = [&](const juce::String& paramId) { asked = paramId; };
    EXPECT_TRUE(press(*editor, "resonance", juce::KeyPress::returnKey));
    EXPECT_EQ(asked, "resonance");
}

TEST(OnCardEditorKeyboard, TheNudgeKeysAreRebindableActionsBoundToTheArrowsByDefault) {
    ShortcutManager shortcuts;
    for (const auto* action : {"layoutEditorNudgeLeft", "layoutEditorNudgeRight", "layoutEditorNudgeUp",
                               "layoutEditorNudgeDown", "layoutEditorNudgeLeftBig", "layoutEditorNudgeRightBig",
                               "layoutEditorNudgeUpBig", "layoutEditorNudgeDownBig"}) {
        SCOPED_TRACE(action);
        EXPECT_NE(shortcuts.getBinding(action), juce::KeyPress()) << "has a default";
        EXPECT_NE(ShortcutManager::getActionDescription(action), juce::String(action)) << "has a plain-language name";
    }
    EXPECT_EQ(shortcuts.getBinding("layoutEditorNudgeRightBig"),
              juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0));
}

TEST(OnCardEditorKeyboard, BackspaceRemovesTheFocusedControlLikeHideFromCardAndUndoBringsItBack) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(editor->getOutlineForTest("drive"), nullptr);

    EXPECT_TRUE(press(*editor, "drive", juce::KeyPress::backspaceKey));
    ASSERT_TRUE(rig.storedLayout(id).has_value());
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    EXPECT_EQ(editor->getOutlineForTest("drive"), nullptr) << "it left the card";
    EXPECT_FALSE(widgetOf(*rig.card(id), "drive")->isVisible());
    EXPECT_EQ(editor->getLastAnnouncementForTest(), "Drive removed");

    ASSERT_TRUE(rig.canvas.undo.undo());
    editor->runQueuedSyncForTest();
    EXPECT_FALSE(rig.storedLayout(id).has_value()) << "one Cmd+Z puts it back";
    EXPECT_NE(editor->getOutlineForTest("drive"), nullptr);
    EXPECT_TRUE(widgetOf(*rig.card(id), "drive")->isVisible());
}

TEST(OnCardEditorKeyboard, ForwardDeleteRemovesToo) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(press(*editor, "resonance", juce::KeyPress::deleteKey));
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("resonance"));
}

TEST(OnCardEditorKeyboard, ABackspaceWithCommandHeldIsLeftAlone) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(press(*editor, "drive", juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier));
    EXPECT_FALSE(rig.storedLayout(id).has_value());
}

TEST(OnCardEditorKeyboard, RemoveControlIsARebindableActionBoundToBackspace) {
    ShortcutManager shortcuts;
    EXPECT_TRUE(ShortcutManager::keyPressMatches(shortcuts.getBinding("layoutEditorRemoveControl"),
                                                 juce::KeyPress(juce::KeyPress::backspaceKey)));
    EXPECT_NE(ShortcutManager::getActionDescription("layoutEditorRemoveControl"),
              juce::String("layoutEditorRemoveControl"));
}
