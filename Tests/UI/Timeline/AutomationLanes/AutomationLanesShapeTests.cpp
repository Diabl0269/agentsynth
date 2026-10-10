// AutomationLanesShapeTests.cpp -- the Draw tool's shapes on an automation lane: the box stamp, the
// pen's shape flyout, the Shift+digit shape keys, and the Range tool's lane range with
// the verbs that act on it. Real events on the real panel's children.

#include "AutomationLanesTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::DrawShape;
using synth::ui::DrawShapeFlyout;
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

    // The flyout the pen opened (the hook stands in for the CallOutBox), null until one opens.
    std::unique_ptr<DrawShapeFlyout> flyout;
    int flyoutsOpened = 0;

    ~ShapeLane() { synth::ui::test_hooks::drawShapeFlyoutHookForTest() = nullptr; }
    void captureFlyouts() {
        synth::ui::test_hooks::drawShapeFlyoutHookForTest() = [this](std::unique_ptr<DrawShapeFlyout> opened) {
            flyout = std::move(opened);
            flyout->setTopLeftPosition(4000, 4000); // clear of the pen, so a release on the pen is not on a row
            ++flyoutsOpened;
        };
    }
    synth::ui::DrawPenButton& pen() { return panel.getDrawPenButton(); }
    juce::Point<float> penCorner() { return {(float)pen().getWidth() - 4.0f, (float)pen().getHeight() - 4.0f}; }
    void pressPen(juce::Point<float> at) {
        auto& comp = static_cast<juce::Component&>(pen());
        comp.mouseDown(makeClickEvent(comp, at, leftButton()));
    }
    void releasePen(juce::Point<float> at) {
        auto& comp = static_cast<juce::Component&>(pen());
        comp.mouseUp(makeClickEvent(comp, at, leftButton()));
    }
    void clickRow(DrawShape shape) {
        auto* row = flyout->getRow(shape);
        const auto centre = row->getLocalBounds().getCentre().toFloat();
        row->mouseDown(makeClickEvent(*row, centre, leftButton()));
        row->mouseUp(makeClickEvent(*row, centre, leftButton()));
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
// ---- The pen button and its flyout ----

TEST(AutomationLanesShapeTest, TheTransportRowIsTheSameWhateverTheTool) {
    ShapeLane f;
    auto rowBounds = [&] {
        return std::vector<juce::Rectangle<int>>{
            f.panel.getFollowPlayheadButtonForTest().getBounds(), f.panel.getSnapToggleButton().getBounds(),
            f.panel.getSnapCombo().getBounds(), f.panel.getToolButton(EditTool::Draw)->getBounds(),
            f.panel.getTransportBar().getBounds()};
    };
    const auto select = rowBounds();
    f.panel.setActiveTool(EditTool::Draw);
    EXPECT_EQ(rowBounds(), select) << "no strip slides out any more";
}

TEST(AutomationLanesShapeTest, ThePenShowsTheCurrentShapeAndHasACornerTriangleHitArea) {
    ShapeLane f;
    EXPECT_EQ(f.pen().getShape(), DrawShape::Free);
    f.panel.setDrawShape(DrawShape::Saw);
    EXPECT_EQ(f.pen().getShape(), DrawShape::Saw);
    EXPECT_FLOAT_EQ(f.pen().getCrossfade(), 1.0f) << "off screen the icon swaps at once";
    f.panel.keyPressed(kShift3);
    EXPECT_EQ(f.pen().getShape(), DrawShape::Sine) << "a shape key moves the pen's icon";

    EXPECT_TRUE(f.pen().isInCorner({f.pen().getWidth() - 2, f.pen().getHeight() - 2}));
    EXPECT_FALSE(f.pen().isInCorner(f.pen().getLocalBounds().getCentre()));
    EXPECT_FALSE(f.pen().isInCorner({f.pen().getWidth() - 2, 2}));
}

TEST(AutomationLanesShapeTest, TheCornerClickOpensTheFlyoutAndDoesNotPickTheTool) {
    ShapeLane f;
    f.captureFlyouts();
    f.pressPen(f.penCorner());
    f.releasePen(f.penCorner());
    ASSERT_NE(f.flyout, nullptr);
    EXPECT_EQ(f.flyoutsOpened, 1);
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Select) << "the corner opens the menu, it is not a click";
    EXPECT_EQ(f.flyout->getCurrentShape(), DrawShape::Free);
}

TEST(AutomationLanesShapeTest, APlainClickOnTheButtonPicksDrawAndOpensNothing) {
    ShapeLane f;
    f.captureFlyouts();
    clickButton(f.pen());
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw);
    EXPECT_EQ(f.flyoutsOpened, 0);
    EXPECT_FALSE(f.pen().isHoldPendingForTest()) << "the release cancelled the hold";
}

TEST(AutomationLanesShapeTest, PressingAndHoldingOpensTheFlyoutWithoutReleaseAndTheReleaseDoesNotPickDraw) {
    ShapeLane f;
    f.captureFlyouts();
    const auto centre = f.pen().getLocalBounds().getCentre().toFloat();
    f.pressPen(centre);
    EXPECT_TRUE(f.pen().isHoldPendingForTest());
    EXPECT_EQ(f.flyoutsOpened, 0) << "not before the hold time";
    f.pen().holdElapsedForTest();
    ASSERT_NE(f.flyout, nullptr) << "opens on the hold, before any release";
    f.releasePen(centre);
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Select) << "the release after a hold is not a click";
    EXPECT_EQ(f.flyoutsOpened, 1);
}

TEST(AutomationLanesShapeTest, TheHoldGestureIsDownFromTheHoldUntilTheRelease) {
    ShapeLane f;
    f.captureFlyouts();
    const auto centre = f.pen().getLocalBounds().getCentre().toFloat();
    f.pressPen(centre);
    EXPECT_FALSE(f.pen().isHoldGestureDown()) << "a plain press is not a hold yet";
    f.pen().holdElapsedForTest();
    EXPECT_TRUE(f.pen().isHoldGestureDown());
    f.releasePen(centre);
    EXPECT_FALSE(f.pen().isHoldGestureDown());
    EXPECT_NE(f.flyout, nullptr);
}

TEST(AutomationLanesShapeTest, ReleasingAHoldOverARowPicksIt) {
    ShapeLane f;
    f.captureFlyouts();
    const auto centre = f.pen().getLocalBounds().getCentre().toFloat();
    f.pressPen(centre);
    f.pen().holdElapsedForTest();
    ASSERT_NE(f.flyout, nullptr);
    auto* row = f.flyout->getRow(DrawShape::Sine);
    const auto onRow = f.pen().getLocalPoint(nullptr, row->localPointToGlobal(row->getLocalBounds().getCentre()));
    f.releasePen(onRow.toFloat());
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Sine);
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw);
}

TEST(AutomationLanesShapeTest, ReleasingAHoldOffTheListPicksNothing) {
    ShapeLane f;
    f.captureFlyouts();
    const auto centre = f.pen().getLocalBounds().getCentre().toFloat();
    f.pressPen(centre);
    f.pen().holdElapsedForTest();
    f.releasePen(centre);
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Free);
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Select);
}

TEST(AutomationLanesShapeTest, TheFlyoutBoxIgnoresOutsideInputWhileTheHoldIsDownAndClosesOnItAfter) {
    juce::Component parent;
    parent.setSize(600, 400);
    DrawShapeFlyout content(DrawShape::Free, {}, {});
    bool holdDown = true;
    synth::ui::DrawShapeCallOutBox box(content, {280, 20, 20, 20}, &parent, [&holdDown] { return holdDown; });
    box.inputAttemptWhenModal();
    EXPECT_EQ(box.getDismissAttemptsForTest(), 0) << "the release that ends the opening press must not close it";
    holdDown = false;
    box.inputAttemptWhenModal();
    EXPECT_EQ(box.getDismissAttemptsForTest(), 1) << "a later click outside closes it";
}

TEST(AutomationLanesShapeTest, DraggingOffThePenBeforeTheHoldTimeCancelsIt) {
    ShapeLane f;
    const auto centre = f.pen().getLocalBounds().getCentre().toFloat();
    f.pressPen(centre);
    auto& comp = static_cast<juce::Component&>(f.pen());
    comp.mouseDrag(makeDragEvent(comp, centre + juce::Point<float>(12.0f, 0.0f), centre, leftButton()));
    EXPECT_FALSE(f.pen().isHoldPendingForTest());
    f.releasePen(centre);
}

TEST(AutomationLanesShapeTest, TheShapeMenuKeyOpensTheFlyoutAndIsRebindable) {
    ShapeLane f;
    f.captureFlyouts();
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('8', juce::ModifierKeys::shiftModifier, 0)));
    ASSERT_NE(f.flyout, nullptr);
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Select) << "opening the menu is not picking a tool";

    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('*', juce::ModifierKeys::shiftModifier, 0)))
        << "the glyph macOS delivers for Shift+8";
    EXPECT_EQ(f.flyoutsOpened, 2);
    shortcuts.setBinding("timelineShapeMenu", juce::KeyPress('m', juce::ModifierKeys::altModifier, 0));
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('m', juce::ModifierKeys::altModifier, 0)));
    EXPECT_EQ(f.flyoutsOpened, 3);
    EXPECT_TRUE(f.pen().getTooltip().contains("shapes:")) << f.pen().getTooltip();
    f.panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesShapeTest, TheDrawButtonTooltipNamesTheFlyoutKey) {
    ShapeLane f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    const auto tip = f.pen().getTooltip();
    EXPECT_TRUE(tip.startsWith("Draw")) << tip;
    EXPECT_TRUE(tip.contains("8")) << tip;
    EXPECT_TRUE(tip.contains("shapes: Shift + 8")) << tip;
    f.panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesShapeTest, EachFlyoutRowHasANameATooltipWithItsShortcutAndTheCurrentOneIsKnown) {
    ShapeLane f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    f.captureFlyouts();
    f.panel.setDrawShape(DrawShape::Triangle);
    f.panel.openShapeFlyout();
    ASSERT_NE(f.flyout, nullptr);
    EXPECT_EQ(f.flyout->getCurrentShape(), DrawShape::Triangle);
    EXPECT_EQ(f.flyout->getFocusedShape(), DrawShape::Triangle) << "focus starts on the current shape";
    for (auto shape : synth::ui::kAllDrawShapes) {
        auto* row = f.flyout->getRow(shape);
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->getTitle(), juce::String(synth::ui::drawShapeName(shape)) + " shape");
        EXPECT_TRUE(f.flyout->getRowTooltip(shape).startsWith(row->getTitle()));
        EXPECT_TRUE(
            f.flyout->getRowTooltip(shape).contains("Shift + " + juce::String(synth::ui::drawShapeKeyDigit(shape))))
            << f.flyout->getRowTooltip(shape);
        EXPECT_TRUE(row->getWantsKeyboardFocus());
    }
    f.panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesShapeTest, ARowShowsARebindOfItsShortcut) {
    ShapeLane f;
    ShortcutManager shortcuts;
    shortcuts.setBinding("timelineShapeSquare", juce::KeyPress('q', juce::ModifierKeys::altModifier, 0));
    f.panel.setShortcutManager(&shortcuts);
    f.captureFlyouts();
    f.panel.openShapeFlyout();
    ASSERT_NE(f.flyout, nullptr);
    const auto tip = f.flyout->getRowTooltip(DrawShape::Square);
    EXPECT_FALSE(tip.contains("Shift + 6")) << tip;
    EXPECT_TRUE(tip.contains("Q") || tip.contains("q")) << tip;
    f.panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesShapeTest, ClickingARowPicksThatShapeAndDraw) {
    ShapeLane f;
    f.captureFlyouts();
    f.panel.openShapeFlyout();
    ASSERT_NE(f.flyout, nullptr);
    f.clickRow(DrawShape::Saw);
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Saw);
    EXPECT_EQ(f.editor->getDrawShape(), DrawShape::Saw);
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw);
    EXPECT_EQ(f.pen().getShape(), DrawShape::Saw);
}

TEST(AutomationLanesShapeTest, TheFlyoutKeysMoveWrapPickAndClose) {
    ShapeLane f;
    f.captureFlyouts();
    f.panel.openShapeFlyout();
    ASSERT_NE(f.flyout, nullptr);
    auto& fly = *f.flyout;
    ASSERT_EQ(fly.getFocusedShape(), DrawShape::Free);
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(fly.getFocusedShape(), DrawShape::Square) << "Up wraps from the first row to the last";
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(fly.getFocusedShape(), DrawShape::Free) << "Down wraps back";
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_EQ(fly.getFocusedShape(), DrawShape::Square);
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    EXPECT_EQ(fly.getFocusedShape(), DrawShape::Free);
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    ASSERT_EQ(fly.getFocusedShape(), DrawShape::Sine);

    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Free) << "Escape picks nothing";
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Select);

    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Sine) << "Enter picks the focused row";
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw);

    f.panel.setActiveTool(EditTool::Select);
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_TRUE(fly.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_EQ(f.panel.getDrawShape(), DrawShape::Line) << "Space picks too";
    EXPECT_FALSE(fly.keyPressed(juce::KeyPress('x')));
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
    // The button path: Draw, the flyout, a click on a row stamps.
    f.captureFlyouts();
    f.panel.keyPressed(juce::KeyPress('8', juce::ModifierKeys::noModifiers, '8'));
    f.panel.openShapeFlyout();
    f.clickRow(DrawShape::Triangle);

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

TEST(AutomationLanesShapeTest, ALaneRangeUnderTheRangeToolStampsFromTheFlyoutInOnePick) {
    ShapeLane f;
    f.captureFlyouts();
    f.selectRange(1.0, 3.0);
    ASSERT_EQ(f.panel.getActiveTool(), EditTool::Range);

    f.panel.openShapeFlyout();
    ASSERT_NE(f.flyout, nullptr);
    f.clickRow(DrawShape::Square);
    EXPECT_EQ(f.theLane().points.size(), 2u * 2u + 1u) << "two one-beat squares and the close";
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Draw) << "a shape pick is a Draw pick";
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
