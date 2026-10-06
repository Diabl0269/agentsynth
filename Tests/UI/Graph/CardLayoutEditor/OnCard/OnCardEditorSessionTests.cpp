// OnCardEditorSessionTests.cpp -- how an on-card editing session ends: Done keeps the result (every change
// was already its own undo step), Cancel and Esc put the layout back as it opened as one more undo step.

#include "OnCardTestHelpers.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

void drop(CardLayoutOnCardEditor& editor, const juce::String& key, juce::Point<int> by) {
    Pointer pointer(editor, key);
    pointer.moveBy(by, kCommand);
    pointer.release();
}

juce::String overrideJson(OnCardRig& rig, NodeID id) {
    return juce::JSON::toString(synth::getCardLayoutOverride(rig.canvas.engine.getGraph(), id));
}

} // namespace

TEST(OnCardEditorSession, DoneKeepsEveryDropAndEachWasItsOwnUndoStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);

    drop(*editor, "cutoff", {40, 0});
    drop(*editor, "outputLevel", {0, 30});
    ASSERT_TRUE(rig.canvas.undo.canUndo()) << "each drop was recorded as it was made";
    const int serial = rig.canvas.undo.getEditSerial();

    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_TRUE(editor->isClosed());
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), start.x + 40) << "Done keeps the layout";

    EXPECT_EQ(rig.canvas.undo.getEditSerial(), serial) << "closing records nothing of its own";

    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), start.x + 40) << "the first drop is still there";
    ASSERT_TRUE(rig.canvas.undo.canUndo());
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.canvas.undo.canUndo());
    EXPECT_FALSE(rig.storedLayout(id).has_value());
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start) << "undo restores the original card";
}

TEST(OnCardEditorSession, CancelPutsTheOpeningLayoutBackAsOneMoreUndoStepThatBringsTheEditsBack) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    drop(*editor, "cutoff", {40, 60});
    ASSERT_TRUE(rig.storedLayout(id).has_value());

    editor->getEditBarForTest().getCancelButton().onClick();
    EXPECT_TRUE(editor->isClosed());
    EXPECT_FALSE(rig.storedLayout(id).has_value()) << "the card had no layout of its own at open";
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start);
    ASSERT_TRUE(rig.canvas.undo.canUndo());

    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_TRUE(rig.storedLayout(id).has_value()) << "Cmd+Z after Cancel brings the move back";
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start + juce::Point<int>(40, 60));
}

TEST(OnCardEditorSession, CancelRestoresAnExistingOverrideExactly) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    synth::setCardLayoutOverride(rig.canvas.engine.getGraph(), nullptr, id,
                                 cardbody_test::automaticLayoutHiding(*rig.canvas.processor(id), {"drive"}));
    rig.canvas.editor.updateComponents();
    const auto opened = overrideJson(rig, id);
    ASSERT_NE(opened, "void");
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    drop(*editor, "cutoff", {30, 30});
    ASSERT_NE(overrideJson(rig, id), opened);

    EXPECT_TRUE(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(overrideJson(rig, id), opened);
    EXPECT_FALSE(widgetOf(*rig.card(id), "drive")->isVisible()) << "Drive is still in the More row";
}

TEST(OnCardEditorSession, DoneWithNothingChangedRecordsNothing) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    editor->done();
    EXPECT_FALSE(rig.canvas.undo.canUndo());
    EXPECT_FALSE(rig.storedLayout(id).has_value());
}

TEST(OnCardEditorSession, ClosingTheOwnerOfAnOpenSessionKeepsItsLayoutAsDoneDoes) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    drop(*editor, "cutoff", {30, 0});
    rig.close();
    EXPECT_TRUE(rig.storedLayout(id).has_value());
    EXPECT_TRUE(rig.canvas.undo.canUndo());
}

TEST(OnCardEditorSession, ANudgePendingWhenDoneIsPressedIsWrittenFirst) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto start = editor->getCellRectForTest("cutoff");
    editor->getOutlineForTest("cutoff")->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    editor->done();
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), start.getX() + 1);
}
