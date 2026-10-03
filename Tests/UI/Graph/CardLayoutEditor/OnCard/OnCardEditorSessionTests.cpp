// OnCardEditorSessionTests.cpp -- how an on-card editing session ends: Done keeps the result as ONE undo
// step, Cancel and Esc put the layout back as it opened and record none.

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

TEST(OnCardEditorSession, DoneKeepsEveryDropAsOneUndoStepThatUndoesThemAll) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    const auto start = widgetOf(*rig.card(id), "cutoff")->getPosition();
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);

    drop(*editor, "cutoff", {40, 0});
    drop(*editor, "outputLevel", {0, 30});
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "nothing is recorded while the session runs";

    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_TRUE(editor->isClosed());
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getX(), start.x + 40) << "Done keeps the layout";

    ASSERT_TRUE(rig.canvas.undo.canUndo());
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "the whole session was one step";
    EXPECT_FALSE(rig.storedLayout(id).has_value());
    EXPECT_EQ(widgetOf(*rig.card(id), "cutoff")->getPosition(), start) << "undo restores the original card";
}

TEST(OnCardEditorSession, CancelPutsTheOpeningLayoutBackAndRecordsNoUndoStep) {
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
    EXPECT_FALSE(rig.canvas.undo.canUndo());
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
    EXPECT_FALSE(rig.canvas.undo.canUndo());
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
