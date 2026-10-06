// OnCardEditorUndoTests.cpp -- Cmd+Z and Cmd+Shift+Z while Edit Layout is open: each change is its own undo
// step, the app's undo and redo step through them one at a time, and the editor re-syncs to the card each
// restore rebuilds without writing anything itself.

#include "OnCardTestHelpers.h"
#include "ShortcutManager/ShortcutManager.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

void drop(CardLayoutOnCardEditor& editor, const juce::String& key, juce::Point<int> by) {
    Pointer pointer(editor, key);
    pointer.moveBy(by, kCommand);
    pointer.release();
}

// What the app does on Cmd+Z / Cmd+Shift+Z: the editor sees the key first, then the app undoes or redoes, and
// the editor re-syncs on the next message loop turn.
bool undoWithEditorOpen(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& focused) {
    const juce::KeyPress key('z', juce::ModifierKeys(kCommand), 0);
    EXPECT_FALSE(editor.getOutlineForTest(focused)->keyPressed(key)) << "the key is left for the app";
    const bool did = rig.canvas.undo.undo();
    editor.runQueuedSyncForTest();
    return did;
}

bool redoWithEditorOpen(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& focused) {
    const juce::KeyPress key('z', juce::ModifierKeys(kCommand | juce::ModifierKeys::shiftModifier), 0);
    EXPECT_FALSE(editor.getOutlineForTest(focused)->keyPressed(key)) << "the key is left for the app";
    const bool did = rig.canvas.undo.redo();
    editor.runQueuedSyncForTest();
    return did;
}

// An unrelated graph change on the undo stack: the module moved on the canvas.
void recordModuleMove(OnCardRig& rig, NodeID id, int x, int y) {
    auto& graph = rig.canvas.engine.getGraph();
    rig.canvas.undo.recordStructuralChange(graph, [&] {
        auto* node = graph.getNodeForId(id);
        node->properties.set("x", x);
        node->properties.set("y", y);
    });
}

int moduleX(OnCardRig& rig, NodeID id) { return (int)rig.canvas.engine.getGraph().getNodeForId(id)->properties["x"]; }

} // namespace

TEST(OnCardEditorUndo, UndoStepsBackTheControlMoveThenTheEarlierChangeAndRedoRestoresBoth) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 100, 100);
    recordModuleMove(rig, id, 300, 100);
    rig.canvas.editor.updateComponents();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();
    const auto cellStart = editor->getCellRectForTest("cutoff");

    drop(*editor, "cutoff", {40, 0});
    const auto moved = widgetOf(*rig.card(id), "cutoff")->getPosition();
    ASSERT_NE(moved, start);

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start) << "only the control's move is reverted";
    EXPECT_EQ(moduleX(rig, id), 300) << "the earlier change stays";
    EXPECT_FALSE(editor->isClosed());
    EXPECT_EQ(editor->getCellRectForTest("cutoff"), cellStart) << "the editor re-synced to the restored card";

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(moduleX(rig, id), 100) << "the second undo takes the earlier change back";

    ASSERT_TRUE(redoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(moduleX(rig, id), 300);
    ASSERT_TRUE(redoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), moved) << "redo brings the move back";
    EXPECT_FALSE(rig.canvas.undo.canRedo());
}

TEST(OnCardEditorUndo, TwoMovesAreTwoUndoSteps) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();

    drop(*editor, "cutoff", {40, 0});
    const auto first = widgetOf(*rig.card(id), "cutoff")->getPosition();
    drop(*editor, "cutoff", {0, 30});
    ASSERT_NE(widgetOf(*rig.card(id), "cutoff")->getPosition(), first);

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), first);
    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start);
    EXPECT_FALSE(rig.canvas.undo.canUndo());
}

TEST(OnCardEditorUndo, UndoAndRedoKeepWorkingRepeatedlyWhileTheEditorStaysOpen) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const int outlines = editor->getOutlineCountForTest();
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();
    drop(*editor, "cutoff", {40, 0});
    const auto moved = widgetOf(*rig.card(id), "cutoff")->getPosition();

    for (int round = 0; round < 3; ++round) {
        ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff")) << round;
        EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start) << round;
        ASSERT_TRUE(redoWithEditorOpen(rig, *editor, "cutoff")) << round;
        EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), moved) << round;
        EXPECT_FALSE(editor->isClosed());
        EXPECT_EQ(editor->getOutlineCountForTest(), outlines);
    }
    drop(*editor, "cutoff", {0, 30}); // and the editor still writes after all that
    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), moved) << "a new step after a redo is its own";
}

TEST(OnCardEditorUndo, UndoingPastTheFirstChangeIntoTheModuleAddClosesTheEditorCleanly) {
    OnCardRig rig;
    auto& graph = rig.canvas.engine.getGraph();
    NodeID id;
    rig.canvas.undo.recordStructuralChange(graph, [&] { id = rig.canvas.add(std::make_unique<FilterModule>(), 0, 0); });
    rig.canvas.editor.updateComponents();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    drop(*editor, "cutoff", {40, 0});

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_FALSE(editor->isClosed()) << "the first undo only takes the move back, never the module add";
    EXPECT_NE(rig.card(id), nullptr);

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(rig.card(id), nullptr) << "the module add is undone: the card is gone";
    EXPECT_TRUE(editor->isClosed()) << "and the editor closes with it";

    ASSERT_TRUE(rig.canvas.undo.redo());
    EXPECT_NE(rig.card(id), nullptr);
}

TEST(OnCardEditorUndo, ANudgePendingWhenUndoIsPressedIsWrittenFirstAsItsOwnStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();
    editor->getOutlineForTest("cutoff")->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_TRUE(editor->hasPendingNudgeForTest());

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_FALSE(editor->hasPendingNudgeForTest());
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start) << "the nudge was its own step, now undone";
}

TEST(OnCardEditorUndo, APlainZIsNotUndo) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->getOutlineForTest("cutoff")->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    // A plain Z is not undo: it neither flushes the nudge nor is it consumed.
    EXPECT_FALSE(editor->getOutlineForTest("cutoff")->keyPressed(juce::KeyPress('z', juce::ModifierKeys(), 0)));
    EXPECT_TRUE(editor->hasPendingNudgeForTest());
}

namespace {

/** The rig with full motion (the editor is told to animate as if on screen by each test). */
struct UndoMotionRig : OnCardRig {
    UndoMotionRig() { synth::ui::setReducedMotionForTest(false); }
};

} // namespace

TEST(OnCardEditorUndoMotion, UndoGlidesTheControlBackFromWhereItWasToTheRestoredPlace) {
    UndoMotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto restored = editor->getCellRectForTest("cutoff");
    drop(*editor, "cutoff", {40, 0});
    const auto moved = editor->getCellRectForTest("cutoff");
    ASSERT_NE(moved, restored);
    editor->setForceAnimateForTest(true);

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(editor->getCellRectForTest("cutoff"), moved) << "it starts from where it was, not at the new place";
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), moved.getX()) << "the real widget moves with it";

    editor->finishMotionForTest();
    EXPECT_EQ(editor->getCellRectForTest("cutoff"), restored);
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), restored.getX());

    ASSERT_TRUE(redoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(editor->getCellRectForTest("cutoff"), restored) << "redo glides from the restored place";
    editor->finishMotionForTest();
    EXPECT_EQ(editor->getCellRectForTest("cutoff"), moved);
}

TEST(OnCardEditorUndoMotion, UnderReduceMotionOrOffScreenUndoJustSnapsBack) {
    UndoMotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto restored = editor->getCellRectForTest("cutoff");
    drop(*editor, "cutoff", {40, 0});

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(editor->getCellRectForTest("cutoff"), restored) << "not on screen: nothing glides";

    editor->setForceAnimateForTest(true);
    synth::ui::setReducedMotionForTest(true);
    ASSERT_TRUE(redoWithEditorOpen(rig, *editor, "cutoff"));
    EXPECT_EQ(editor->getCellRectForTest("cutoff").getX(), restored.getX() + 40) << "Reduce Motion: no glide";
}

TEST(OnCardEditorUndoMotion, AControlUndoBringsBackGrowsInAndOneItTakesAwayShrinksOut) {
    UndoMotionRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    hideThroughPanel(rig, *editor, "drive");
    ASSERT_EQ(editor->getOutlineForTest("drive"), nullptr);
    editor->setForceAnimateForTest(true);

    ASSERT_TRUE(undoWithEditorOpen(rig, *editor, "cutoff")); // the hide is undone: Drive is back
    auto* widget = widgetOf(*rig.card(id), "drive");
    ASSERT_NE(widget, nullptr);
    EXPECT_NE(editor->getOutlineForTest("drive"), nullptr);
    EXPECT_FLOAT_EQ(widget->getAlpha(), 0.0f) << "it grows in from nothing";
    editor->finishMotionForTest();
    EXPECT_FLOAT_EQ(widget->getAlpha(), 1.0f);
    EXPECT_TRUE(widget->getTransform().isIdentity());

    ASSERT_TRUE(redoWithEditorOpen(rig, *editor, "cutoff")); // the hide is redone: Drive leaves again
    EXPECT_EQ(editor->getOutlineForTest("drive"), nullptr);
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 1) << "from the card's picture of it before the redo";
    editor->finishMotionForTest();
    EXPECT_EQ(editor->getShrinkGhostCountForTest(), 0);
}
