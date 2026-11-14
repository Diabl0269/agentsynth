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

/** The editor as painted (children included, software renderer so it reads real pixels on every platform). */
juce::Image paintedEditor(CardLayoutOnCardEditor& editor) {
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    editor.paintEntireComponent(g, false);
    return image;
}

/** How many of the group's four corner points (just inside the wash's rounded rectangle, clear of the controls and
 *  of the picture that glides to the hide area) differ from the picture at rest: a group washed as the drop target
 *  changes them, a group left alone none. */
int changedIn(const juce::Image& now, const juce::Image& atRest, const juce::Rectangle<int>& group) {
    const auto inner = group.reduced(5);
    int changed = 0;
    for (const auto& p :
         {inner.getTopLeft(), inner.getTopRight() - juce::Point<int>(1, 0),
          inner.getBottomLeft() - juce::Point<int>(0, 1), inner.getBottomRight() - juce::Point<int>(1, 1)})
        changed += now.getPixelAt(p.x, p.y) != atRest.getPixelAt(p.x, p.y) ? 1 : 0;
    return changed;
}

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

// Crossing another group lights it as the drop target; once the pointer is below the card only the
// "Drop to hide" area may be lit, and the group's wash returns when the pointer goes back up. Checked on the
// painted pixels of that group in every phase of the drag, not only on the state.
TEST(OnCardEditorHideDrag, BelowTheCardNoGroupIsWashedAndTheGroupWashReturnsGoingBackUp) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto foreign = open.editor->getSectionAreaOfForTest("mix");
    ASSERT_FALSE(foreign.isEmpty());
    ASSERT_NE(foreign, open.editor->getSectionAreaOfForTest("cutoff")) << "mix is in another group than cutoff";
    const auto atRest = paintedEditor(*open.editor);

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({4, 4}, kCommand);
    EXPECT_EQ(open.editor->getDropSectionForTest(), -1) << "inside its own group nothing is washed";
    EXPECT_EQ(changedIn(paintedEditor(*open.editor), atRest, foreign), 0);

    const int toForeign = foreign.getCentreY() - open.editor->getOutlineForTest("cutoff")->getBounds().getCentreY();
    pointer.moveBy({0, toForeign}, kCommand);
    ASSERT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_GE(open.editor->getDropSectionForTest(), 0) << "the group under the pointer is the drop target";
    EXPECT_GT(changedIn(paintedEditor(*open.editor), atRest, foreign), 0) << "and it is washed";

    // Past the card's bottom edge, with a further move below it (the event that used to return early).
    pointer.moveBy({0, open.dropBelowCard("mix") + 10}, kCommand);
    ASSERT_TRUE(open.editor->isHidingDragForTest());
    pointer.moveBy({0, 6}, kCommand);
    EXPECT_EQ(open.editor->getDropSectionForTest(), -1) << "below the card no group is the drop target";
    EXPECT_EQ(changedIn(paintedEditor(*open.editor), atRest, foreign), 0) << "no group wash is painted";

    pointer.moveBy({0, -(open.dropBelowCard("mix") + 16)}, kCommand);
    ASSERT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_GE(open.editor->getDropSectionForTest(), 0) << "back above the edge the wash returns";
    EXPECT_GT(changedIn(paintedEditor(*open.editor), atRest, foreign), 0);

    pointer.moveBy({0, open.dropBelowCard("mix") + 16}, kCommand);
    ASSERT_TRUE(open.editor->isHidingDragForTest());
    pointer.release();
    ASSERT_TRUE(open.rig.storedLayout(open.id).has_value());
    EXPECT_TRUE(open.rig.storedLayout(open.id)->hidden.contains("cutoff")) << "a release below the card hides it";
    EXPECT_EQ(open.editor->getDropSectionForTest(), -1);
    ASSERT_TRUE(open.rig.canvas.undo.undo());
    open.editor->runQueuedSyncForTest();
    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value()) << "one Cmd+Z puts it back";
    EXPECT_NE(open.editor->getOutlineForTest("cutoff"), nullptr);
}
