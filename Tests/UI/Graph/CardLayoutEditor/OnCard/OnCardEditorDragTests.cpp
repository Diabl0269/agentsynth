// OnCardEditorDragTests.cpp -- dragging a control on the card through the real mouse handlers: where the
// drop writes it, guides and Cmd, the neighbour a drop lands on being pushed aside, Esc mid-drag.

#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardLayoutMath.h"

using namespace oncard_test;
using synth::ui::oncard::tooClose;

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
    // x follows the pointer; y stays on the control's row (a row's controls share a top).
    EXPECT_EQ(open.cell("cutoff").getPosition(), start.getPosition() + juce::Point<int>(30, 0));
    EXPECT_EQ(knob->getPosition(), knobStart + juce::Point<int>(30, 0)) << "the control itself moved";
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
        for (const auto& guide : open.editor->getGuidesForTest())
            EXPECT_FALSE(guide.vertical) << "Cmd draws no snap guide";
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
    EXPECT_EQ(cutoffAfter.getY(), resonanceAfter.getY()) << "it joined Resonance's row, on the row's top";
    EXPECT_EQ(cutoffAfter.getY(), cutoff.getY()) << "the row it left was empty, so Resonance's row closed up to it";
    EXPECT_FALSE(cutoffAfter.intersects(resonanceAfter));
    EXPECT_FALSE(cutoffAfter.intersects(open.cell("drive")));
    EXPECT_FALSE(resonanceAfter.intersects(open.cell("drive")));
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
    const int dx = detune.getCentreX() - unison.getCentreX() + 6; // past Detune's centre: they swap places

    {
        Pointer pointer(*editor, "unison");
        pointer.moveBy({dx, 0}, kCommand);
        pointer.release();
    }
    EXPECT_EQ(editor->getCellRectForTest("unison").getY(), unison.getY()) << "still on its row";
    EXPECT_GE(editor->getCellRectForTest("unison").getX(), editor->getCellRectForTest("detune").getRight())
        << "Unison now stands after Detune";
    EXPECT_EQ(editor->getHomeRectForTest("detune"), detune) << "Detune's home is where it stood";

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

    int groupBottom = 0;
    for (const auto* key : {"unison", "blend", "width", "pulseWidth"})
        groupBottom = std::max(groupBottom, editor->getCellRectForTest(key).getBottom());
    {
        Pointer pointer(*editor, "detune"); // down below the group's last row: a row of its own
        pointer.moveBy({0, groupBottom + 3 - detune.getCentreY()}, kCommand);
        pointer.release();
    }
    const auto moved = editor->getCellRectForTest("detune");
    ASSERT_NE(moved.getPosition(), detune.getPosition());
    EXPECT_EQ(editor->getHomeRectForTest("detune").getPosition(), moved.getPosition()) << "the user's own move";

    {
        Pointer pointer(*editor, "unison"); // dropped near it, Unison makes Detune give way
        pointer.moveBy({unisonDx, 0}, kCommand);
        pointer.release();
    }
    EXPECT_EQ(editor->getHomeRectForTest("detune").getPosition(), moved.getPosition())
        << "pushed, not moved by the user: the home stays";
}

// ---- The row model: out and back, a new row, a full group, preview == commit, a style change ----

namespace {

struct OscOpen {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<OscillatorModule>());
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    juce::Rectangle<int> cell(const juce::String& key) const { return editor->getCellRectForTest(key); }
    // The cells of `key`'s group by key (the plan's section of that control).
    std::vector<juce::String> groupOf(const juce::String& key) {
        const auto& plan = rig.card(id)->getCardBody()->getPlan();
        const int section = plan.items[(size_t)plan.findParam(key)].section;
        std::vector<juce::String> keys;
        for (int i : plan.sections[(size_t)section].items)
            if (plan.items[(size_t)i].param != nullptr &&
                editor->getOutlineForTest(plan.items[(size_t)i].param->paramID))
                keys.push_back(plan.items[(size_t)i].param->paramID);
        return keys;
    }
    // layout-overlap: nothing overlaps (cells no closer than the gap, or flush as the default has them) and
    // every cell is inside the card's content width.
    void expectTidy(const std::vector<juce::String>& keys) {
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(rig.card(id)->getWidth());
        for (size_t i = 0; i < keys.size(); ++i) {
            EXPECT_GE(cell(keys[i]).getX(), g.contentX) << keys[i];
            EXPECT_LE(cell(keys[i]).getRight(), g.contentX + g.contentW) << keys[i];
            for (size_t j = i + 1; j < keys.size(); ++j)
                EXPECT_FALSE(cell(keys[i]).intersects(cell(keys[j]))) << keys[i] << " and " << keys[j];
        }
    }
};

} // namespace

// Regression test for FRO720: a control dragged out of a full row and back pushed the others below.
TEST(OnCardEditorRows, ADragOutOfTheRowAndBackLeavesTheRowAsItWas) {
    OscOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto keys = open.groupOf("unison");
    ASSERT_GE(keys.size(), 2u);
    std::map<juce::String, juce::Rectangle<int>> before;
    for (const auto& k : keys)
        before[k] = open.cell(k);

    const juce::Point<int> away(0, 140);
    {
        Pointer pointer(*open.editor, "unison");
        pointer.moveBy(away, kCommand);
        pointer.release();
        open.expectTidy(keys);
    }
    {
        Pointer pointer(*open.editor, "unison");
        pointer.moveBy(-away, kCommand);
        pointer.release();
    }
    open.expectTidy(keys);
    for (const auto& k : keys)
        EXPECT_EQ(open.cell(k), before[k]) << k << " is back on its row";
}

TEST(OnCardEditorRows, DroppingBelowTheGroupsLastRowStartsANewRowAndShowsAGuideThere) {
    OscOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto keys = open.groupOf("unison");
    int bottom = 0;
    for (const auto& k : keys)
        if (k != "unison")
            bottom = std::max(bottom, open.cell(k).getBottom());

    Pointer pointer(*open.editor, "unison");
    // The pointer, not the cell, picks the group and the row: just under the last row, still in the group.
    pointer.moveBy({0, bottom + 3 - open.cell("unison").getCentreY()}, kCommand);
    ASSERT_FALSE(open.editor->getGuidesForTest().empty()) << "the new-row band shows a guide";
    EXPECT_FALSE(open.editor->getGuidesForTest().front().vertical);
    EXPECT_EQ(open.editor->getGuidesForTest().front().position, bottom + synth::ui::oncard::kControlGap);
    pointer.release();

    EXPECT_EQ(open.cell("unison").getY(), bottom + synth::ui::oncard::kControlGap);
    open.expectTidy(keys);
}

// A drop into another group: the group it joins stays tidy. (Level into the Oscillator's Unison group.)
TEST(OnCardEditorRows, DroppingAControlIntoAFullGroupLeavesTheGroupTidy) {
    OscOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto unisonKeys = open.groupOf("unison");
    const juce::String mover = "level";
    ASSERT_NE(open.editor->getOutlineForTest(mover), nullptr);
    const auto to = open.cell("detune").getCentre() - open.cell(mover).getCentre();

    Pointer pointer(*open.editor, mover);
    pointer.moveBy(to, kCommand);
    pointer.release();

    auto keys = unisonKeys;
    keys.push_back(mover);
    open.expectTidy(keys);
    EXPECT_EQ(open.groupOf("unison").size(), keys.size()) << "Level joined the group";
}

// Regression test for FRO720: the committed layout is what the drag showed last.
TEST(OnCardEditorRows, TheCommittedLayoutEqualsTheLastPreview) {
    OscOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto keys = open.groupOf("unison");
    const auto to = open.cell("detune").getCentre() - open.cell("unison").getCentre();

    Pointer pointer(*open.editor, "unison");
    pointer.moveBy(to + juce::Point<int>(-12, 4), kCommand);
    std::map<juce::String, juce::Rectangle<int>> preview;
    for (const auto& k : keys)
        preview[k] = open.cell(k);
    pointer.release();
    for (const auto& k : keys)
        EXPECT_EQ(open.cell(k), preview[k]) << k;
}

// Regression test for FRO720: a fader to a knob and back could no longer share one row; the repack is part of
// the change's own undo step.
TEST(OnCardEditorRows, AStyleChangeRepacksItsRowInTheSameUndoStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    {
        Pointer pointer(*editor, "cutoff"); // freezes the group to positions
        pointer.moveBy({20, 0}, kCommand);
        pointer.release();
    }
    const auto positioned = rig.storedLayout(id);
    ASSERT_TRUE(positioned.has_value());

    auto* panel = openControlPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(static_cast<bool>(panel->onShowAs));
    const auto before = editor->getCellRectForTest("resonance");
    panel->onShowAs(synth::CardWidget::FaderH);
    ASSERT_NE(editor->getCellRectForTest("resonance").getWidth(), before.getWidth()) << "the control changed size";

    const std::vector<juce::String> keys{"cutoff", "resonance", "drive"};
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(rig.card(id)->getWidth());
    for (size_t i = 0; i < keys.size(); ++i) {
        EXPECT_LE(editor->getCellRectForTest(keys[i]).getRight(), g.contentX + g.contentW) << keys[i];
        for (size_t j = i + 1; j < keys.size(); ++j)
            EXPECT_FALSE(editor->getCellRectForTest(keys[i]).intersects(editor->getCellRectForTest(keys[j])))
                << keys[i] << " and " << keys[j];
    }

    ASSERT_TRUE(rig.canvas.undo.undo()) << "one step undoes the style change and its repack";
    editor->runQueuedSyncForTest();
    EXPECT_EQ(rig.storedLayout(id), positioned) << "back to the layout before the change, not half of it";
    EXPECT_EQ(editor->getCellRectForTest("resonance"), before);
}

// ---- A restyled control's row, and a row the move left empty ----

namespace {

struct AdsrOpen {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<ADSRModule>());
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);
    const std::vector<juce::String> stages{"attack", "hold", "decay", "sustain", "release"};

    juce::Rectangle<int> cell(const juce::String& key) const { return editor->getCellRectForTest(key); }
    // layout-overlap: no two stages overlap (the default row stands flush, so touching is fine), and every one is
    // inside the card's content width.
    void expectOneTidyRow() {
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(rig.card(id)->getWidth());
        for (size_t i = 0; i < stages.size(); ++i) {
            EXPECT_GE(cell(stages[i]).getX(), g.contentX) << stages[i];
            EXPECT_LE(cell(stages[i]).getRight(), g.contentX + g.contentW) << stages[i];
            EXPECT_LT(cell(stages[i]).getY(), cell(stages[0]).getBottom()) << stages[i] << " is on the first row";
            EXPECT_GT(cell(stages[i]).getBottom(), cell(stages[0]).getY()) << stages[i] << " is on the first row";
            for (size_t j = i + 1; j < stages.size(); ++j)
                EXPECT_FALSE(cell(stages[i]).intersects(cell(stages[j]))) << stages[i] << " and " << stages[j];
        }
    }
    int stageSectionBottom() {
        const auto& plan = rig.card(id)->getCardBody()->getPlan();
        return plan.sections[(size_t)plan.items[(size_t)plan.findParam("attack")].section].cellBottom;
    }
};

} // namespace

// Regression test for FRO720: Sustain as a knob wraps Release to a row of its own; dragging Sustain back into the
// stage row brings Release back with it, all five on one row inside the card, with no empty band left below.
TEST(OnCardEditorRows, ASustainKnobDraggedBackIntoTheStageRowBringsReleaseBackToo) {
    AdsrOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto fresh = open.rig.storedLayout(open.id);
    auto* panel = openControlPanel(open.rig, *open.editor, "sustain");
    ASSERT_NE(panel, nullptr);
    panel->onShowAs(synth::CardWidget::Knob);
    EXPECT_GT(open.cell("release").getY(), open.cell("attack").getY()) << "the knob pushed Release off the row";

    const auto to = juce::Point<int>(open.cell("decay").getRight() + 20, open.cell("decay").getCentreY()) -
                    open.cell("sustain").getCentre();
    Pointer pointer(*open.editor, "sustain");
    pointer.moveBy(to, kCommand);
    pointer.release();

    open.expectOneTidyRow();
    EXPECT_EQ(open.stageSectionBottom(), std::max({open.cell("sustain").getBottom(), open.cell("release").getBottom(),
                                                   open.cell("attack").getBottom()}))
        << "no empty band under the row";

    // The style change is one undo step on its own.
    ASSERT_TRUE(open.rig.canvas.undo.undo()); // the drag
    open.editor->runQueuedSyncForTest();
    ASSERT_TRUE(open.rig.canvas.undo.undo()); // the style change
    open.editor->runQueuedSyncForTest();
    EXPECT_EQ(open.rig.storedLayout(open.id), fresh);
}

// A row the control left empty closes up: the rows below it take its place, and no other row moves.
TEST(OnCardEditorRows, ARowLeftEmptyByAMoveClosesUpAndNoOtherRowMoves) {
    OscOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto keys = open.groupOf("unison");
    ASSERT_TRUE(std::find(keys.begin(), keys.end(), juce::String("pulseWidth")) != keys.end());
    int bottom = 0;
    for (const auto& k : keys)
        bottom = std::max(bottom, open.cell(k).getBottom());
    { // Blend to a row of its own under Pulse Width: three rows, Pulse Width alone in the middle one.
        Pointer pointer(*open.editor, "blend");
        pointer.moveBy({0, bottom + 3 - open.cell("blend").getCentreY()}, kCommand);
        pointer.release();
    }
    const auto middle = open.cell("pulseWidth");
    const auto last = open.cell("blend");
    ASSERT_GT(last.getY(), middle.getBottom());
    const auto first = open.cell("unison");

    Pointer pointer(*open.editor, "pulseWidth");
    pointer.moveBy({first.getCentreX() + 70 - middle.getCentreX(), first.getCentreY() - middle.getCentreY()}, kCommand);
    pointer.release();

    EXPECT_EQ(open.cell("pulseWidth").getY(), first.getY()) << "Pulse Width joined the first row";
    EXPECT_EQ(open.cell("blend").getY(), middle.getY()) << "the row under the empty one moved up to take its place";
    EXPECT_EQ(open.cell("unison").getY(), first.getY()) << "the first row did not move";
    open.expectTidy(keys);
}

// The first drop turns a flowing group into a positioned one; that must not make the card taller.
TEST(OnCardEditorRows, TurningAGroupIntoAPositionedOneDoesNotChangeTheCardsHeight) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const int height = rig.card(id)->getHeight();
    Pointer pointer(*editor, "cutoff");
    pointer.moveBy({20, 0}, kCommand);
    pointer.release();
    EXPECT_EQ(rig.card(id)->getHeight(), height);
}

// Regression test for FRO720: a cell is the box the card gives its control, so a position written for it puts the
// control where it stood (a fader is narrower than its box and sits centred in it).
TEST(OnCardEditorRows, AFaderCellIsTheBoxTheCardGivesItAndTheFirstDropMovesNoStage) {
    AdsrOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(open.rig.card(open.id)->getWidth());
    EXPECT_EQ(open.cell("attack").getX(), g.contentX);
    EXPECT_EQ(open.cell("attack").getWidth(), g.contentW / 5);
    std::map<juce::String, juce::Rectangle<int>> before;
    for (const auto& k : open.stages)
        before[k] = open.cell(k);

    Pointer pointer(*open.editor, "decay");
    pointer.moveBy({0, 30}, kCommand); // below its row: a row of its own
    pointer.release();
    for (const auto& k : open.stages)
        if (k != "decay")
            EXPECT_EQ(open.cell(k), before[k]) << k << " stayed where it was";
}
