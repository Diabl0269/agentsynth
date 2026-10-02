// AutomationLanesShapeTests.cpp -- the Draw tool's shapes on an automation lane: the box stamp, the
// shape strip beside the Draw button, the Shift+digit shape keys, and the Range tool's lane range with
// the verbs that act on it. Real events on the real panel's children.

#include "AutomationLanesTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::DrawShape;
using synth::ui::EditTool;
using synth::ui::TimelineViewState;

namespace {

const juce::KeyPress kShift3('3', juce::ModifierKeys::shiftModifier, 0);

// An open Bass track with one [0, 100] lane, its editor laid out at its row.
struct ShapeLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    ShapeLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    float xAt(double beat) { return (float)panel.getViewState().beatToX(beat); }
    float yAt(double value) { return (float)editor->valueToY(value); }
    synth::ui::LaneRangeSelection& laneRange() { return panel.getAutomationLanes().getLaneRange(); }

    void pickSine() {
        panel.setActiveTool(EditTool::Draw);
        panel.setDrawShape(DrawShape::Sine);
    }
    // A Range-tool drag on the lane from `from` to `to` beats.
    void selectRange(double from, double to) {
        panel.setActiveTool(EditTool::Range);
        dragAcross(*editor, {xAt(from), 10.0f}, {xAt(to), 10.0f}, 6);
    }
};

} // namespace

//==============================================================================
// ---- The box stamp ----

TEST(AutomationLanesShapeTest, SineBoxOverOneBarAtQuarterSnapIsFourCyclesInOneUndoStep) {
    ShapeLane f;
    ASSERT_NE(f.editor, nullptr);
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 6.0, 40.0)); // outside the box: must survive untouched
    f.undo.clearUndoHistory();
    f.pickSine();

    // Pressed a hair after beat 0 and released a hair before beat 4: both edges snap to the quarter grid.
    const juce::Point<float> from{f.xAt(0.0) + 3.0f, f.yAt(80.0)}, to{f.xAt(4.0) - 3.0f, f.yAt(20.0)};
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_TRUE(f.editor->isDragActiveForTest());
    EXPECT_EQ(f.editor->getShapeGesture().getChipText(),
              juce::String(juce::CharPointer_UTF8("4 cycles \xc2\xb7 1 bar")));
    EXPECT_TRUE(f.theLane().points.size() == 1u) << "nothing commits before the release";
    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));

    const auto& pts = f.theLane().points;
    ASSERT_EQ(pts.size(), 4u * synth::ui::kSinePointsPerCycle + 1u + 1u) << "4 sine cycles, the close, the old point";
    EXPECT_DOUBLE_EQ(pts.front().beat, 0.0);
    EXPECT_DOUBLE_EQ(pts[pts.size() - 2].beat, 4.0);
    EXPECT_DOUBLE_EQ(pts.back().beat, 6.0);
    EXPECT_DOUBLE_EQ(pts.back().value, 40.0);
    double lo = 100.0, hi = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        lo = std::min(lo, pts[i].value);
        hi = std::max(hi, pts[i].value);
    }
    EXPECT_NEAR(lo, 20.0, 3.0) << "the box's bottom edge is the swing's low";
    EXPECT_NEAR(hi, 80.0, 3.0) << "the box's top edge is the swing's high";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    ASSERT_EQ(f.theLane().points.size(), 1u) << "the whole stamp is ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesShapeTest, WithSnapOffTheEdgesAreFreeAndEachCycleIsOneBeat) {
    ShapeLane f;
    f.panel.getViewState().snapEnabled = false;
    f.pickSine();
    dragAcross(*f.editor, {f.xAt(0.5), f.yAt(90.0)}, {f.xAt(3.5), f.yAt(10.0)}, 6);
    const auto& pts = f.theLane().points;
    ASSERT_EQ(pts.size(), 3u * synth::ui::kSinePointsPerCycle + 1u) << "three one-beat cycles";
    EXPECT_NEAR(pts.front().beat, 0.5, 1e-9);
    EXPECT_NEAR(pts.back().beat, 3.5, 1e-9);
    EXPECT_NEAR(pts[synth::ui::kSinePointsPerCycle].beat, 1.5, 1e-9) << "the second cycle starts a beat later";
}

TEST(AutomationLanesShapeTest, EighthSnapStampsEightCyclesPerBar) {
    ShapeLane f;
    f.panel.getViewState().snap = TimelineViewState::Snap::Eighth;
    f.panel.setActiveTool(EditTool::Draw);
    f.panel.setDrawShape(DrawShape::Square);
    const juce::Point<float> from{f.xAt(0.0), f.yAt(100.0)}, to{f.xAt(4.0), f.yAt(0.0)};
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_EQ(f.editor->getShapeGesture().getChipText(),
              juce::String(juce::CharPointer_UTF8("8 cycles \xc2\xb7 1 bar")));
    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_EQ(f.theLane().points.size(), 8u * 2u + 1u);
}

TEST(AutomationLanesShapeTest, TheChipCountsBarsAndCycles) {
    ShapeLane f;
    f.pickSine();
    const juce::Point<float> from{f.xAt(0.0), f.yAt(100.0)}, to{f.xAt(8.0), f.yAt(0.0)};
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_EQ(f.editor->getShapeGesture().getChipText(),
              juce::String(juce::CharPointer_UTF8("8 cycles \xc2\xb7 2 bars")));
    const juce::Point<float> shorter{f.xAt(3.0), f.yAt(0.0)};
    f.editor->mouseDrag(makeDragEvent(*f.editor, shorter, from, leftButton()));
    EXPECT_EQ(f.editor->getShapeGesture().getChipText(),
              juce::String(juce::CharPointer_UTF8("3 cycles \xc2\xb7 3 beats")));
}

TEST(AutomationLanesShapeTest, EscapeCancelsTheBoxWithNothingCommitted) {
    ShapeLane f;
    f.pickSine();
    const juce::Point<float> from{f.xAt(0.0), f.yAt(80.0)}, to{f.xAt(4.0), f.yAt(20.0)};
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_TRUE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.editor->isDragActiveForTest());
    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
    EXPECT_TRUE(f.theLane().points.empty());
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesShapeTest, FreeAndLineKeepThePenAndTheLine) {
    ShapeLane f;
    f.panel.setActiveTool(EditTool::Draw);
    EXPECT_EQ(f.editor->getTool(), AutomationLaneEditor::Tool::Pencil) << "Free is the pen";
    f.panel.setDrawShape(DrawShape::Line);
    EXPECT_EQ(f.editor->getTool(), AutomationLaneEditor::Tool::Line);
    dragAcross(*f.editor, {f.xAt(2.0), 30.0f}, {f.xAt(4.0), 10.0f}, 6);
    ASSERT_EQ(f.theLane().points.size(), 2u) << "Line draws a straight line without Shift";
}

//==============================================================================
// ---- The shape strip and keys ----

TEST(AutomationLanesShapeTest, TheShapeStripIsOutOnlyWhileDrawIsTheTool) {
    ShapeLane f;
    auto& strip = f.panel.getDrawShapeStrip();
    EXPECT_FALSE(strip.isVisible());
    EXPECT_EQ(strip.getWidth(), 0);

    // Off screen there is no frame to wait for: the slide lands at once.
    f.panel.setActiveTool(EditTool::Draw);
    EXPECT_TRUE(strip.isVisible());
    EXPECT_EQ(strip.getWidth(), strip.getOpenWidth());
    const auto drawButton = f.panel.getToolButton(EditTool::Draw)->getBounds();
    EXPECT_GE(strip.getX(), drawButton.getRight()) << "it sits right of the Draw button";
    for (auto shape : synth::ui::kAllDrawShapes) {
        auto* button = strip.getButton(shape);
        EXPECT_FALSE(button->getBounds().isEmpty());
        EXPECT_EQ(button->getTitle(), juce::String(synth::ui::drawShapeName(shape)) + " shape");
        EXPECT_TRUE(button->getTooltip().startsWith(button->getTitle()));
    }
    EXPECT_TRUE(strip.getButton(DrawShape::Free)->getToggleState()) << "the active shape is lit";

    f.panel.setActiveTool(EditTool::Select);
    EXPECT_FALSE(strip.isVisible());
    EXPECT_EQ(strip.getWidth(), 0);
}

TEST(AutomationLanesShapeTest, ClickingAShapeButtonPicksIt) {
    ShapeLane f;
    f.panel.setActiveTool(EditTool::Draw);
    clickButton(*f.panel.getDrawShapeStrip().getButton(DrawShape::Saw));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Saw);
    EXPECT_EQ(f.editor->getDrawShape(), DrawShape::Saw);
    EXPECT_TRUE(f.panel.getDrawShapeStrip().getButton(DrawShape::Saw)->getToggleState());
    EXPECT_FALSE(f.panel.getDrawShapeStrip().getButton(DrawShape::Free)->getToggleState());
}

TEST(AutomationLanesShapeTest, ShiftThreePicksDrawAndSine) {
    ShapeLane f;
    ASSERT_EQ(f.panel.getActiveTool(), EditTool::Select);
    EXPECT_TRUE(f.panel.keyPressed(kShift3));
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw) << "not the bare 3 (Split)";
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Sine);
    EXPECT_EQ(f.editor->getDrawShape(), DrawShape::Sine);
}

TEST(AutomationLanesShapeTest, ShapeKeysAreRebindableAndMatchTheShiftedGlyphMacOSDelivers) {
    ShapeLane f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('#', juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Sine);
    EXPECT_TRUE(f.panel.getDrawShapeStrip().getButton(DrawShape::Sine)->getTooltip().contains("3"));

    shortcuts.setBinding("timelineShapeSquare", juce::KeyPress('q', juce::ModifierKeys::altModifier, 0));
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('q', juce::ModifierKeys::altModifier, 0)));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Square);
    f.panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesShapeTest, TheDrawKeyAgainStepsToTheNextShapeAndWraps) {
    ShapeLane f;
    const juce::KeyPress eight('8', juce::ModifierKeys::noModifiers, '8');
    EXPECT_TRUE(f.panel.keyPressed(eight));
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw);
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Free) << "the first press only picks Draw";
    EXPECT_TRUE(f.panel.keyPressed(eight));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Line);
    for (int i = 0; i < 5; ++i)
        f.panel.keyPressed(eight);
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Free) << "wraps after Square";
}

//==============================================================================
// ---- The lane range ----

TEST(AutomationLanesShapeTest, ARangeDragSelectsASnappedSpanOnThatLane) {
    ShapeLane f;
    f.selectRange(1.1, 2.9);
    ASSERT_TRUE(f.laneRange().hasWidth());
    EXPECT_EQ(f.laneRange().getLane(), f.lane);
    EXPECT_DOUBLE_EQ(f.laneRange().getStartBeat(), 1.0);
    EXPECT_DOUBLE_EQ(f.laneRange().getEndBeat(), 3.0);

    // Esc clears it: the idle editor passes the key on and the panel takes it.
    EXPECT_FALSE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.laneRange().isActive());
}

TEST(AutomationLanesShapeTest, AClickElsewhereClearsTheLaneRange) {
    ShapeLane f;
    f.selectRange(1.0, 3.0);
    ASSERT_TRUE(f.laneRange().isActive());
    auto& clips = f.panel.getClipLaneArea();
    clips.mouseDown(makeClickEvent(clips, {5.0f, 5.0f}, leftButton()));
    clips.mouseUp(makeClickEvent(clips, {5.0f, 5.0f}, leftButton()));
    EXPECT_FALSE(f.laneRange().isActive()) << "a press in the clip lanes";

    f.selectRange(1.0, 3.0);
    f.panel.setActiveTool(EditTool::Select);
    ASSERT_TRUE(f.laneRange().isActive()) << "picking a tool keeps it";
    dragAcross(*f.editor, {f.xAt(6.0), 10.0f}, {f.xAt(6.0), 10.0f}, 1);
    EXPECT_FALSE(f.laneRange().isActive()) << "a press with another tool on a lane";
}

TEST(AutomationLanesShapeTest, ARangeThenAShapeButtonStampsAtTheLanesFullHeight) {
    ShapeLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 8.0, 40.0));
    f.undo.clearUndoHistory();
    f.selectRange(1.0, 3.0);
    // The button path: Draw brings the strip out, the range stays, a click stamps.
    f.panel.keyPressed(juce::KeyPress('8', juce::ModifierKeys::noModifiers, '8'));
    clickButton(*f.panel.getDrawShapeStrip().getButton(DrawShape::Triangle));

    const auto& pts = f.theLane().points;
    ASSERT_EQ(pts.size(), 2u * 2u + 1u + 1u) << "two one-beat triangles, the close, the old point";
    EXPECT_DOUBLE_EQ(pts[0].beat, 1.0);
    EXPECT_DOUBLE_EQ(pts[0].value, 0.0) << "the lane's minimum";
    EXPECT_DOUBLE_EQ(pts[1].beat, 1.5);
    EXPECT_DOUBLE_EQ(pts[1].value, 100.0) << "the lane's maximum";
    EXPECT_DOUBLE_EQ(pts[4].beat, 3.0);
    EXPECT_DOUBLE_EQ(pts.back().beat, 8.0);
    EXPECT_TRUE(f.laneRange().isActive()) << "the range stays for the next verb";

    f.undo.undo();
    EXPECT_EQ(f.theLane().points.size(), 1u) << "one undo step";
}

TEST(AutomationLanesShapeTest, ALaneRangeBringsTheStripOutUnderTheRangeToolSoOneClickStamps) {
    ShapeLane f;
    auto& strip = f.panel.getDrawShapeStrip();
    f.selectRange(1.0, 3.0);
    ASSERT_EQ(f.panel.getActiveTool(), EditTool::Range);
    EXPECT_TRUE(strip.isVisible()) << "a lane range shows the shapes whatever the tool";

    clickButton(*strip.getButton(DrawShape::Square));
    EXPECT_EQ(f.theLane().points.size(), 2u * 2u + 1u) << "two one-beat squares and the close";
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw) << "a shape pick is a Draw pick";

    f.panel.setActiveTool(EditTool::Range);
    EXPECT_TRUE(strip.isVisible()) << "the range is still there";
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(strip.isVisible()) << "no range and not Draw: the strip goes back";
}

TEST(AutomationLanesShapeTest, AShapeKeyWithALaneRangeStampsIt) {
    ShapeLane f;
    f.selectRange(0.0, 2.0);
    EXPECT_TRUE(f.panel.keyPressed(kShift3));
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw);
    EXPECT_EQ(f.theLane().points.size(), 2u * synth::ui::kSinePointsPerCycle + 1u);
}

TEST(AutomationLanesShapeTest, LineOnARangeRampsBetweenTheCurveValuesAtItsEdges) {
    ShapeLane f;
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 0.0, 20.0));
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 30.0)); // inside the range: replaced
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 4.0, 60.0));
    f.selectRange(1.0, 3.0);
    f.panel.pickDrawShape(DrawShape::Line);
    const auto& pts = f.theLane().points;
    ASSERT_EQ(pts.size(), 4u);
    EXPECT_DOUBLE_EQ(pts[1].beat, 1.0);
    EXPECT_NEAR(pts[1].value, 25.0, 1e-6) << "the curve's value at the range start";
    EXPECT_DOUBLE_EQ(pts[2].beat, 3.0);
    EXPECT_NEAR(pts[2].value, 45.0, 1e-6) << "the curve's value at the range end";

    f.panel.pickDrawShape(DrawShape::Free);
    EXPECT_EQ(f.theLane().points.size(), 4u) << "Free has no meaning on a range";
}

TEST(AutomationLanesShapeTest, DeleteRemovesThePointsInsideTheRangeInOneUndoStep) {
    ShapeLane f;
    for (double beat : {0.0, 1.5, 2.0, 2.5, 5.0})
        ASSERT_TRUE(f.doc.addBreakpoint(f.lane, beat, 50.0));
    f.undo.clearUndoHistory();
    f.selectRange(1.0, 3.0);
    const juce::KeyPress del(juce::KeyPress::deleteKey);
    EXPECT_FALSE(f.editor->keyPressed(del)) << "the editor passes it on";
    EXPECT_TRUE(f.panel.keyPressed(del));
    ASSERT_EQ(f.theLane().points.size(), 2u);
    EXPECT_DOUBLE_EQ(f.theLane().points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(f.theLane().points[1].beat, 5.0);
    f.undo.undo();
    EXPECT_EQ(f.theLane().points.size(), 5u);
}

TEST(AutomationLanesShapeTest, AStampOverTheLanePointCapIsRefusedWithNoUndoEntry) {
    ShapeLane f;
    f.panel.getViewState().snap = TimelineViewState::Snap::HundredTwentyEighth;
    f.panel.getViewState().pixelsPerBeat = 10.0;
    f.selectRange(0.0, 40.0); // 1280 cycles of 16 sine points: past the per-lane cap
    ASSERT_DOUBLE_EQ(f.laneRange().getEndBeat(), 40.0);
    f.panel.pickDrawShape(DrawShape::Sine);
    EXPECT_TRUE(f.theLane().points.empty());
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_TRUE(f.editor->getShapeGesture().getStatusText().isNotEmpty()) << "the lane says why";
}

TEST(AutomationLanesShapeTest, StampedPointsAreOrdinaryPoints) {
    ShapeLane f;
    f.selectRange(0.0, 2.0);
    f.panel.pickDrawShape(DrawShape::Square); // 0 hi, 0.5 lo, 1 hi, 1.5 lo, 2 close
    ASSERT_EQ(f.theLane().points.size(), 5u);
    f.laneRange().clear();

    // Select moves one.
    f.panel.setActiveTool(EditTool::Select);
    const auto handle = f.editor->getHandleRectForTest(1.0).getCentre().toFloat();
    ASSERT_FALSE(handle.isOrigin());
    dragAcross(*f.editor, handle, {handle.x, f.yAt(50.0)}, 4);
    bool moved = false;
    for (const auto& bp : f.theLane().points)
        if (bp.beat == 1.0)
            moved = std::abs(bp.value - 50.0) < 3.0;
    EXPECT_TRUE(moved) << "the stamped point at beat 1 was dragged to the middle";

    // A double-click adds one.
    const juce::Point<float> empty{f.xAt(3.0), f.yAt(50.0)};
    f.editor->mouseDoubleClick(makeClickEvent(*f.editor, empty, leftButton()));
    EXPECT_EQ(f.theLane().points.size(), 6u);

    // The eraser removes one.
    f.panel.setActiveTool(EditTool::Erase);
    const auto victim = f.editor->getHandleRectForTest(0.5).getCentre().toFloat();
    dragAcross(*f.editor, victim, victim, 1);
    EXPECT_EQ(f.theLane().points.size(), 5u);
}
