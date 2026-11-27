// OnCardEditorHideDragTests.cpp -- dragging a control below the card to hide it: the "Drop to hide" area that
// shows only during a drag, a release below the card hiding the control as one undo step, and moving back up
// cancelling that; and where a dragged control will land (the dashed slot, the dimmed groups, the chip). Real mouse
// events on the outline.

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

/** How many of the group's four corner points (just inside the dim's rounded rectangle, clear of the controls and
 *  of the picture that glides to the hide area) differ from the picture at rest: a group that gives way changes
 *  them, the group at full strength none. */
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
    const auto start = open.editor->getCellRectForTest("outputLevel");

    Pointer pointer(*open.editor, "outputLevel");
    const int down = open.dropBelowCard("outputLevel");
    pointer.moveBy({0, down}, kCommand);
    ASSERT_TRUE(open.editor->isHidingDragForTest());
    pointer.moveBy({40, -down}, kCommand);
    EXPECT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_FALSE(open.editor->getHideZoneForTest().isActive());
    EXPECT_EQ(open.editor->getCellRectForTest("outputLevel").getX(), start.getX() + 40)
        << "following the pointer again";
    pointer.release();

    const auto layout = open.rig.storedLayout(open.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(layout->hidden.isEmpty()) << "an ordinary move";
    ASSERT_NE(storedItem(*layout, "outputLevel"), nullptr);
    EXPECT_TRUE(storedItem(*layout, "outputLevel")->at.has_value());
    EXPECT_NE(open.editor->getOutlineForTest("outputLevel"), nullptr);
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

// Crossing another group makes it the drop target (the one group at full strength, the others dim); once the pointer is
// below the card only the "Drop to hide" area may be lit, so every group dims, and the group stands at full strength
// again when the pointer goes back up. Checked on the painted pixels of that group in every phase of the drag, not
// only on the state.
TEST(OnCardEditorHideDrag, BelowTheCardNoGroupIsLitAndTheGroupIsLitAgainGoingBackUp) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto foreign = open.editor->getSectionAreaOfForTest("mix");
    ASSERT_FALSE(foreign.isEmpty());
    ASSERT_NE(foreign, open.editor->getSectionAreaOfForTest("cutoff")) << "mix is in another group than cutoff";
    const auto atRest = paintedEditor(*open.editor);

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({4, 4}, kCommand);
    EXPECT_EQ(open.editor->getDropSectionForTest(), -1) << "inside its own group the others are not the target";
    EXPECT_GT(changedIn(paintedEditor(*open.editor), atRest, foreign), 0) << "so another group dims";

    const int toForeign = foreign.getCentreY() - open.editor->getOutlineForTest("cutoff")->getBounds().getCentreY();
    pointer.moveBy({0, toForeign}, kCommand);
    ASSERT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_GE(open.editor->getDropSectionForTest(), 0) << "the group under the pointer is the drop target";
    EXPECT_EQ(changedIn(paintedEditor(*open.editor), atRest, foreign), 0) << "and it stands at full strength";

    // Past the card's bottom edge, with a further move below it (the event that used to return early).
    pointer.moveBy({0, open.dropBelowCard("mix") + 10}, kCommand);
    ASSERT_TRUE(open.editor->isHidingDragForTest());
    pointer.moveBy({0, 6}, kCommand);
    EXPECT_EQ(open.editor->getDropSectionForTest(), -1) << "below the card no group is the drop target";
    EXPECT_GT(changedIn(paintedEditor(*open.editor), atRest, foreign), 0) << "every group dims, none is lit";

    pointer.moveBy({0, -(open.dropBelowCard("mix") + 16)}, kCommand);
    ASSERT_FALSE(open.editor->isHidingDragForTest());
    EXPECT_GE(open.editor->getDropSectionForTest(), 0) << "back above the edge the group is the target again";
    EXPECT_EQ(changedIn(paintedEditor(*open.editor), atRest, foreign), 0);

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

// -- The drop target: where a dragged control will land (the target group opens a slot, the others dim) --

namespace {

juce::String sectionIdOf(const synth::CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId == paramId)
                return section.id;
    return {};
}

struct Oscillator {
    OnCardRig rig;
    NodeID id = addRowOscillator(rig, 100, 100);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    /** The pointer move that takes `key`'s outline centre to the middle of `otherKey`'s outline. */
    juce::Point<int> toward(const juce::String& key, const juce::String& otherKey) const {
        return editor->getOutlineForTest(otherKey)->getBounds().getCentre() -
               editor->getOutlineForTest(key)->getBounds().getCentre();
    }
};

void expectOneLandingSpotLit(CardLayoutOnCardEditor& editor, const char* when) {
    EXPECT_EQ(editor.getLitSlotCountForTest(), 1) << when;
    EXPECT_EQ(editor.getUndimmedSectionCountForTest(), 1) << when << ": only the target group stands at full strength";
}

} // namespace

// Regression test for the drop-target-unclear kind: a drop between groups gave no clear target. Exactly one slot is lit
// at every step of a drag through every group and out below the card, whichever the group's line count.
TEST(OnCardEditorDropTarget, ExactlyOneLandingSpotIsLitAtEveryStepOfADrag) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    EXPECT_EQ(osc.editor->getLitSlotCountForTest(), 0) << "nothing is lit at rest";

    Pointer pointer(*osc.editor, "detune");
    EXPECT_EQ(osc.editor->getLitSlotCountForTest(), 0) << "a press alone lights nothing";
    pointer.moveBy({6, 6}, kCommand);
    expectOneLandingSpotLit(*osc.editor, "inside its own group");
    for (const char* other : {"octave", "level", "detune"}) {
        pointer.moveBy(osc.toward("detune", other), kCommand);
        expectOneLandingSpotLit(*osc.editor, other);
        EXPECT_FALSE(osc.editor->getDropSlotForTest().isEmpty()) << other;
    }
    pointer.moveBy({0, 400}, kCommand);
    ASSERT_TRUE(osc.editor->isHidingDragForTest());
    EXPECT_EQ(osc.editor->getLitSlotCountForTest(), 1) << "below the card only the hide area is lit";
    EXPECT_TRUE(osc.editor->getDropSlotForTest().isEmpty());
    pointer.moveBy({0, -400}, kCommand);
    expectOneLandingSpotLit(*osc.editor, "back above the edge");
    pointer.release();
    EXPECT_EQ(osc.editor->getLitSlotCountForTest(), 0);
    EXPECT_FLOAT_EQ(osc.editor->getDragFadeForTest(), 0.0f);
    EXPECT_TRUE(osc.editor->getDropSlotForTest().isEmpty());
}

TEST(OnCardEditorDropTarget, TheTargetGroupOpensASlotAndTheOtherGroupsDim) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    Pointer pointer(*osc.editor, "detune");
    pointer.moveBy(osc.toward("detune", "octave"), kCommand);

    EXPECT_FLOAT_EQ(osc.editor->getDragFadeForTest(), 1.0f);
    EXPECT_EQ(osc.editor->getDimmedSectionCountForTest(), 3)
        << "Waveform, Unison and Output give way, Pitch is the target";
    const auto slot = osc.editor->getDropSlotForTest();
    ASSERT_FALSE(slot.isEmpty());
    EXPECT_EQ(slot, osc.editor->getCellRectForTest("detune")) << "the slot is where the control would land";
    EXPECT_TRUE(osc.editor->getSectionAreaOfForTest("octave").contains(slot));
    pointer.release();
    EXPECT_EQ(osc.editor->getDimmedSectionCountForTest(), 0);
}

TEST(OnCardEditorDropTarget, TheChipNamesTheGroupAndTheLine) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    Pointer pointer(*osc.editor, "detune");
    pointer.moveBy(osc.toward("detune", "octave"), kCommand);
    EXPECT_EQ(osc.editor->getDropSlotNameForTest(), "Pitch, line 1");

    // Just under the Pitch group's last row, still in the group, the drop is a new line.
    const auto octave = osc.editor->getCellRectForTest("octave");
    const int down = octave.getBottom() + 3 - osc.editor->getCellRectForTest("detune").getCentreY();
    pointer.moveBy({0, down}, kCommand);
    EXPECT_EQ(osc.editor->getDropSlotNameForTest(), "Pitch, line 2");
    pointer.release();
    EXPECT_TRUE(osc.editor->getDropSlotNameForTest().isEmpty());
}

TEST(OnCardEditorDropTarget, EscapeFadesTheDropTargetOutAndWritesNothing) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    Pointer pointer(*osc.editor, "detune");
    pointer.moveBy(osc.toward("detune", "level"), kCommand);
    ASSERT_EQ(osc.editor->getLitSlotCountForTest(), 1);
    EXPECT_TRUE(osc.editor->sendEscapeToDragForTest());
    osc.editor->finishMotionForTest();
    EXPECT_EQ(osc.editor->getLitSlotCountForTest(), 0);
    EXPECT_EQ(osc.editor->getDimmedSectionCountForTest(), 0);
    pointer.release();
    EXPECT_TRUE(isUntouchedRowOscillator(osc.rig.storedLayout(osc.id)) || !osc.rig.storedLayout(osc.id).has_value());
}

// A group keeps its title when its last control leaves, and shows an empty slot to drop one on.
TEST(OnCardEditorDropTarget, AGroupThatLostItsLastControlKeepsItsTitleAndAnEmptySlot) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    EXPECT_TRUE(osc.editor->getEmptySlotsForTest().empty());
    hideThroughPanel(osc.rig, *osc.editor, "level");
    hideThroughPanel(osc.rig, *osc.editor, "pan");
    osc.editor->finishMotionForTest();

    const auto* body = osc.rig.card(osc.id)->getCardBody();
    ASSERT_NE(body, nullptr);
    const synth::CardBodyPlan::Section* output = nullptr;
    for (const auto& s : body->getPlan().sections)
        if (s.id == "output")
            output = &s;
    ASSERT_NE(output, nullptr) << "the group is still there";
    EXPECT_TRUE(output->hasHeader());
    ASSERT_NE(output->header, nullptr);
    EXPECT_TRUE(output->header->isVisible());
    EXPECT_EQ(osc.editor->getEmptySlotsForTest().size(), 1u);

    // And a control can be dropped on it with the real mouse path.
    Pointer pointer(*osc.editor, "octave");
    const auto slot = osc.editor->getEmptySlotsForTest().front();
    pointer.moveBy(slot.getCentre() - osc.editor->getOutlineForTest("octave")->getBounds().getCentre(), kCommand);
    EXPECT_EQ(osc.editor->getDropSlotNameForTest(), "Output, line 1");
    pointer.release();
    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(sectionIdOf(*layout, "octave"), "output");
}
