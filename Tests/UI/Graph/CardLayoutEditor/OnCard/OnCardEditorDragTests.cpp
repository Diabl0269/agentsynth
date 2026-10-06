// OnCardEditorDragTests.cpp -- dragging a control on the card through the real mouse handlers: where the
// drop writes it, guides and Cmd, the neighbour a drop lands on being pushed aside, Esc mid-drag.

#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardLayoutMath.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

struct Opened {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    juce::Rectangle<int> cell(const juce::String& key) const { return editor->getCellRectForTest(key); }
};

} // namespace

TEST(OnCardEditorDrag, DroppingCutoffSixtyPixelsRightWritesItsPositionAndGivesEveryControlOfTheSectionOne) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto start = open.cell("cutoff");
    const int contentX = synth::cardbody::BodyGeometry::forCardWidth(open.rig.card(open.id)->getWidth()).contentX;

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({60, 0}, kCommand);
    EXPECT_TRUE(open.editor->isDraggingForTest());
    pointer.release();
    EXPECT_FALSE(open.editor->isDraggingForTest());

    const auto layout = open.rig.storedLayout(open.id);
    ASSERT_TRUE(layout.has_value()) << "the drop wrote the node's layout";
    const auto* cutoff = storedItem(*layout, "cutoff");
    ASSERT_NE(cutoff, nullptr);
    ASSERT_TRUE(cutoff->at.has_value());
    EXPECT_EQ(cutoff->at->x, start.getX() + 60 - contentX);
    for (const auto* sibling : {"resonance", "drive"}) {
        const auto* item = storedItem(*layout, sibling);
        ASSERT_NE(item, nullptr);
        EXPECT_TRUE(item->at.has_value()) << sibling << " is in Cutoff's group and got a position too";
    }
    EXPECT_FALSE(storedItem(*layout, "keyTrack")->at.has_value()) << "another group is left as it was";

    EXPECT_EQ(open.cell("cutoff").getX(), start.getX() + 60) << "the rebuilt card draws the knob there";
    EXPECT_EQ(widgetOf(*open.rig.card(open.id), "cutoff")->getX(), start.getX() + 60);
}

// A group that holds a view (the ADSR's velocity fader and Threshold meter): writing the group's controls
// must not move the view.
TEST(OnCardEditorDrag, AViewInTheDraggedGroupStaysWhereItWasAfterTheWrite) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* card = rig.card(id);
    const auto& plan = card->getCardBody()->getPlan();
    const int velocity = plan.findParam("velocity");
    ASSERT_GE(velocity, 0);
    const int section = plan.items[(size_t)velocity].section;
    const auto views = synth::ui::collectViews(*card, section);
    ASSERT_FALSE(views.empty()) << "the group holds the Threshold view";
    const int top = plan.sections[(size_t)section].cellTop;
    const int relativeY = views.front().rect.getY() - top;

    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    Pointer pointer(*editor, "velocity");
    pointer.moveBy({0, 30}, kCommand); // a full-width fader can only move down
    pointer.release();

    auto* after = rig.card(id);
    const auto viewsAfter = synth::ui::collectViews(*after, section);
    ASSERT_EQ(viewsAfter.size(), views.size());
    EXPECT_EQ(viewsAfter.front().rect.getX(), views.front().rect.getX());
    EXPECT_EQ(viewsAfter.front().rect.getY() - after->getCardBody()->getPlan().sections[(size_t)section].cellTop,
              relativeY);
    EXPECT_EQ(viewsAfter.front().rect.getWidth(), views.front().rect.getWidth());
    const auto layout = rig.storedLayout(id);
    ASSERT_TRUE(layout.has_value());
}

TEST(OnCardEditorDrag, TheRealWidgetAndItsOutlineFollowThePointerWhileDragging) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto start = open.cell("cutoff");
    auto* knob = widgetOf(*open.rig.card(open.id), "cutoff");
    const auto knobStart = knob->getPosition();

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({30, 20}, kCommand);
    EXPECT_EQ(open.cell("cutoff").getPosition(), start.getPosition() + juce::Point<int>(30, 20));
    EXPECT_EQ(knob->getPosition(), knobStart + juce::Point<int>(30, 20)) << "the control itself moved";
    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value()) << "nothing is written until the drop";
    pointer.release();
}

TEST(OnCardEditorDrag, ALeftEdgeThreePixelsOffAnotherControlsSnapsToItAndCommandPlacesFreely) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto cutoff = open.cell("cutoff");
    const auto drive = open.cell("drive");
    const int dx = drive.getX() + 3 - cutoff.getX();
    const int dy = 0;

    {
        Pointer pointer(*open.editor, "cutoff");
        pointer.moveBy({dx, dy});
        EXPECT_EQ(open.cell("cutoff").getX(), drive.getX()) << "pulled onto the other's left edge";
        EXPECT_FALSE(open.editor->getGuidesForTest().empty());
        pointer.moveBy({0, 0});
        EXPECT_TRUE(open.editor->sendEscapeToDragForTest());
    }
    {
        Pointer pointer(*open.editor, "cutoff");
        pointer.moveBy({dx, dy}, kCommand);
        EXPECT_EQ(open.cell("cutoff").getX(), drive.getX() + 3) << "Cmd places it freely";
        EXPECT_TRUE(open.editor->getGuidesForTest().empty());
        EXPECT_TRUE(open.editor->sendEscapeToDragForTest());
    }
}

TEST(OnCardEditorDrag, DroppingOnResonancePushesItOutSoTheTwoKeepAGap) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto cutoff = open.cell("cutoff");
    const auto resonance = open.cell("resonance");

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy(resonance.getPosition() - cutoff.getPosition() + juce::Point<int>(10, 6), kCommand);
    pointer.release();

    const auto cutoffAfter = open.cell("cutoff");
    const auto resonanceAfter = open.cell("resonance");
    EXPECT_EQ(cutoffAfter.getPosition(), resonance.getPosition() + juce::Point<int>(10, 6));
    EXPECT_NE(resonanceAfter, resonance) << "Resonance was pushed";
    EXPECT_FALSE(synth::ui::oncard::tooClose(cutoffAfter, resonanceAfter));
    EXPECT_FALSE(synth::ui::oncard::tooClose(cutoffAfter, open.cell("drive")));
    EXPECT_FALSE(synth::ui::oncard::tooClose(resonanceAfter, open.cell("drive")));
}

TEST(OnCardEditorDrag, DroppingBackWhereItStartedWritesNothing) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({40, 40}, kCommand);
    pointer.moveBy({-40, -40}, kCommand);
    pointer.release();
    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value());
}

TEST(OnCardEditorDrag, EscapeMidDragPutsTheControlBackAndWritesNothing) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto start = open.cell("cutoff");
    auto* knob = widgetOf(*open.rig.card(open.id), "cutoff");
    const auto knobStart = knob->getPosition();

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({50, 70}, kCommand);
    ASSERT_NE(open.cell("cutoff"), start);
    EXPECT_TRUE(open.editor->sendEscapeToDragForTest());
    open.editor->finishMotionForTest();

    EXPECT_EQ(open.cell("cutoff"), start);
    EXPECT_EQ(knob->getPosition(), knobStart);
    EXPECT_FALSE(open.editor->isDraggingForTest());
    pointer.release();
    EXPECT_FALSE(open.rig.storedLayout(open.id).has_value()) << "the release after Esc commits nothing";
    EXPECT_FALSE(open.editor->isClosed()) << "Esc during a drag only cancels the drag";
}

TEST(OnCardEditorDrag, AControlStaysInsideItsGroupsContentWidthAndBelowItsTop) {
    Opened open;
    ASSERT_NE(open.editor, nullptr);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(open.rig.card(open.id)->getWidth());

    Pointer pointer(*open.editor, "cutoff");
    pointer.moveBy({2000, -2000}, kCommand);
    const auto rect = open.cell("cutoff");
    EXPECT_LE(rect.getRight(), g.contentX + g.contentW);
    const int sectionTop = open.rig.card(open.id)->getCardBody()->getPlan().sections[1].cellTop;
    EXPECT_GE(rect.getY(), sectionTop) << "Cutoff's group is the second section";
    pointer.release();
}

// Regression test for FRO639: a neighbour pushed out of a full row stayed out for good.
TEST(OnCardEditorDrag, DraggingUnisonBackReturnsThePushedNeighboursToTheirOriginalRects) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<OscillatorModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto unison = editor->getCellRectForTest("unison");
    const auto detune = editor->getCellRectForTest("detune");
    ASSERT_EQ(unison.getY(), detune.getY()) << "one row";
    const int dx = unison.getWidth() / 2 + 6;

    {
        Pointer pointer(*editor, "unison");
        pointer.moveBy({dx, 0}, kCommand);
        pointer.release();
    }
    EXPECT_EQ(editor->getCellRectForTest("unison").getX(), unison.getX() + dx);
    EXPECT_NE(editor->getCellRectForTest("detune"), detune) << "Detune made way";
    EXPECT_EQ(editor->getHomeRectForTest("detune"), detune) << "but its home is where it stood";

    {
        Pointer pointer(*editor, "unison");
        pointer.moveBy({-dx, 0}, kCommand);
        pointer.release();
    }
    EXPECT_EQ(editor->getCellRectForTest("unison"), unison);
    EXPECT_EQ(editor->getCellRectForTest("detune"), detune) << "back on the row, where it was";
}

TEST(OnCardEditorDrag, AControlTheUserMovedKeepsItsNewHomeWhenAnotherIsDroppedOnIt) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<OscillatorModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto detune = editor->getCellRectForTest("detune");
    const int unisonDx = detune.getX() - editor->getCellRectForTest("unison").getX();

    {
        Pointer pointer(*editor, "detune");
        pointer.moveBy({0, 30}, kCommand);
        pointer.release();
    }
    const auto moved = editor->getCellRectForTest("detune");
    ASSERT_EQ(moved.getY(), detune.getY() + 30);
    EXPECT_EQ(editor->getHomeRectForTest("detune").getPosition(), moved.getPosition()) << "the user's own move";

    {
        Pointer pointer(*editor, "unison"); // dropped near it, Unison makes Detune give way
        pointer.moveBy({unisonDx, 0}, kCommand);
        pointer.release();
    }
    EXPECT_EQ(editor->getHomeRectForTest("detune").getPosition(), moved.getPosition())
        << "pushed, not moved by the user: the home stays";
}
