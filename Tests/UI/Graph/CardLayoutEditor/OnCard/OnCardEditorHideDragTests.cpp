// OnCardEditorHideDragTests.cpp -- dragging a control below the card to hide it: the "Drop to hide" area that
// shows only during a drag, a release below the card hiding the control as one undo step, and moving back up
// cancelling that. Real mouse events on the outline.

#include "OnCardTestHelpers.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

struct Opened {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    /** How far down the pointer must go from `key`'s outline centre to be `below` px under the card's edge. */
    int dropBelowCard(const juce::String& key, int below = 12) const {
        const int cardBottom = editor->getHeight() - CardLayoutOnCardEditor::kAddStripHeight;
        return cardBottom - editor->getOutlineForTest(key)->getBounds().getCentreY() + below;
    }
};

} // namespace

TEST(OnCardEditorHideDrag, TheDropZoneIsThereOnlyWhileAControlIsBeingDragged) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto& zone = open.editor->getHideZoneForTest();
    EXPECT_FALSE(zone.isVisible());

    Pointer pointer(*open.editor, "cutoff");
    EXPECT_FALSE(zone.isVisible()) << "a press alone shows nothing";
    pointer.moveBy({10, 10}, kCommand);
    EXPECT_TRUE(zone.isVisible());
    EXPECT_FLOAT_EQ(zone.getAlpha(), 1.0f) << "no fade under Reduce Motion";
    EXPECT_FALSE(zone.isActive()) << "the pointer is still on the card";
    EXPECT_FALSE(open.editor->isHidingDragForTest());
    pointer.release();
    EXPECT_FALSE(zone.isVisible());
}

TEST(OnCardEditorHideDrag, ReleasingBelowTheCardHidesTheControlAndOneUndoBringsItBack) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    ASSERT_NE(open.editor->getOutlineForTest("cutoff"), nullptr);

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({0, open.dropBelowCard("cutoff")}, kCommand);
    EXPECT_TRUE(open.editor->isHidingDragForTest());
    EXPECT_TRUE(open.editor->getHideZoneForTest().isActive());
    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value()) << "nothing is written until the release";
    pointer.release();

    ASSERT_TRUE(open.rig.storedLayout(open.id).has_value());
    EXPECT_TRUE(open.rig.storedLayout(open.id)->hidden.contains("cutoff"));
    EXPECT_EQ(open.editor->getOutlineForTest("cutoff"), nullptr) << "it left the card";
    EXPECT_FALSE(widgetOf(*open.rig.card(open.id), "cutoff")->isVisible());
    EXPECT_EQ(open.editor->getLastAnnouncementForTest(), "Cutoff hidden");
    EXPECT_FALSE(open.editor->getHideZoneForTest().isVisible());
    EXPECT_FALSE(open.editor->isDraggingForTest());
    EXPECT_FALSE(open.editor->isHidingDragForTest());

    ASSERT_TRUE(open.rig.canvas.undo.undo());
    open.editor->runQueuedSyncForTest();
    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value()) << "one Cmd+Z puts it back";
    EXPECT_NE(open.editor->getOutlineForTest("cutoff"), nullptr);
    EXPECT_TRUE(widgetOf(*open.rig.card(open.id), "cutoff")->isVisible());
}

TEST(OnCardEditorHideDrag, MovingBackAboveTheEdgeCancelsTheHideAndTheControlFollowsThePointerAgain) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto start = open.editor->getCellRectForTest("cutoff");

    Pointer pointer(*open.editor, "cutoff");
    const int down = open.dropBelowCard("cutoff");
    pointer.moveBy({0, down}, kCommand);
    ASSERT_TRUE(open.editor->isHidingDragForTest());
    pointer.moveBy({40, -down}, kCommand);
    EXPECT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_FALSE(open.editor->getHideZoneForTest().isActive());
    EXPECT_EQ(open.editor->getCellRectForTest("cutoff").getX(), start.getX() + 40) << "following the pointer again";
    pointer.release();

    const auto layout = open.rig.storedLayout(open.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(layout->hidden.isEmpty()) << "an ordinary move";
    ASSERT_NE(storedItem(*layout, "cutoff"), nullptr);
    EXPECT_TRUE(storedItem(*layout, "cutoff")->at.has_value());
    EXPECT_NE(open.editor->getOutlineForTest("cutoff"), nullptr);
}

TEST(OnCardEditorHideDrag, EscapeBelowTheCardPutsTheControlBackAndHidesNothing) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto start = open.editor->getCellRectForTest("cutoff");

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({30, 20}, kCommand);
    pointer.moveBy({0, open.dropBelowCard("cutoff")}, kCommand);
    ASSERT_TRUE(open.editor->isHidingDragForTest());
    EXPECT_TRUE(open.editor->sendEscapeToDragForTest());
    open.editor->finishMotionForTest();
    EXPECT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_FALSE(open.editor->getHideZoneForTest().isVisible());
    pointer.release();

    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value());
    EXPECT_EQ(open.editor->getCellRectForTest("cutoff"), start);
}
