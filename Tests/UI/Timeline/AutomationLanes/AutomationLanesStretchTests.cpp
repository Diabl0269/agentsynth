// AutomationLanesStretchTests.cpp -- stretching and squeezing a point selection through the real lane editor: the
// box and its four handles, the live preview that touches nothing until mouse-up, pushing, one undo step, Escape, the
// keyboard path, the cursor and hint. Real mouse and key events on the real panel's editor.

#include "AutomationLanesTestFixture.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/ReducedMotion.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::StretchHandle;

namespace {

using Beats = std::vector<double>;

// Four points at beats 1..4 (values 20, 60, 40, 80) and an unselected one at beat 6; snap is a quarter note.
struct StretchLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    StretchLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane, 1.0, 20.0);
        doc.addBreakpoint(lane, 2.0, 60.0);
        doc.addBreakpoint(lane, 3.0, 40.0);
        doc.addBreakpoint(lane, 4.0, 80.0);
        doc.addBreakpoint(lane, 6.0, 50.0);
        select({1.0, 2.0, 3.0, 4.0});
    }

    void select(const Beats& beats) { editor->getPointSelection().setSelection(beats); }
    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    juce::Point<float> at(double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)editor->valueToY(value)};
    }
    juce::Point<float> handle(StretchHandle h) { return editor->getStretchHandleRectForTest(h).getCentre(); }
    juce::Point<float> beatsRight(juce::Point<float> p, double beats) {
        return {p.x + (float)(beats * panel.getViewState().pixelsPerBeat), p.y};
    }

    void press(juce::Point<float> p) { editor->mouseDown(makeClickEvent(*editor, p, leftButton())); }
    void dragTo(juce::Point<float> from, juce::Point<float> to, int steps = 5) {
        for (int i = 1; i <= steps; ++i)
            editor->mouseDrag(
                makeDragEvent(*editor, from + (to - from) * ((float)i / (float)steps), from, leftButton()));
    }
    void release(juce::Point<float> from, juce::Point<float> to) {
        editor->mouseUp(makeDragEvent(*editor, to, from, leftButton()));
    }
    void drag(juce::Point<float> from, juce::Point<float> to) { dragAcross(*editor, from, to, 6, leftButton()); }
    bool key(int code, int mods) { return editor->keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0)); }

    Beats beats() const {
        Beats out;
        for (const auto& p : theLane().points)
            out.push_back(p.beat);
        return out;
    }
    Beats values() const {
        Beats out;
        for (const auto& p : theLane().points)
            out.push_back(p.value);
        return out;
    }
    Beats selected() const { return editor->getPointSelection().getSelected(); }
};

constexpr int kAltShift = juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier;

struct ReducedMotionGuard {
    ReducedMotionGuard() { synth::ui::setReducedMotionForTest(true); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(AutomationLanesStretchTest, TheBoxAndItsFourHandlesExistOnlyWithTwoOrMorePointsSelected) {
    StretchLane f;
    EXPECT_FALSE(f.editor->getStretchBoxForTest().isEmpty());
    for (auto h : {StretchHandle::Left, StretchHandle::Right, StretchHandle::Top, StretchHandle::Bottom})
        EXPECT_FALSE(f.editor->getStretchHandleRectForTest(h).isEmpty());

    const auto box = f.editor->getStretchBoxForTest();
    EXPECT_FLOAT_EQ(f.handle(StretchHandle::Left).x, box.getX());
    EXPECT_FLOAT_EQ(f.handle(StretchHandle::Right).x, box.getRight());
    EXPECT_FLOAT_EQ(f.handle(StretchHandle::Left).y, box.getCentreY());
    EXPECT_LE(box.getX(), f.at(1.0, 20.0).x) << "the box surrounds the selection";
    EXPECT_GE(box.getRight(), f.at(4.0, 80.0).x);

    f.select({2.0});
    EXPECT_TRUE(f.editor->getStretchBoxForTest().isEmpty()) << "one point has nothing to stretch";
    EXPECT_TRUE(f.editor->getStretchHandleRectForTest(StretchHandle::Right).isEmpty());
    f.select({});
    EXPECT_TRUE(f.editor->getStretchBoxForTest().isEmpty());

    f.select({1.0, 2.0});
    f.editor->setTool(AutomationLaneEditor::Tool::Pencil);
    EXPECT_TRUE(f.editor->getStretchBoxForTest().isEmpty()) << "only the Pointer tool shows it";
}

TEST(AutomationLanesStretchTest, DraggingTheRightHandleRightSpreadsThePointsAndCommitsOnlyOnMouseUp) {
    StretchLane f;
    f.doc.removeBreakpoint(f.lane, 6.0);
    const auto from = f.handle(StretchHandle::Right);
    const auto to = f.beatsRight(from, 3.0) + juce::Point<float>(13.0f, 0.0f); // 3.3 beats: snaps to 3
    const auto rev = f.doc.getRevision();

    f.press(from);
    EXPECT_TRUE(f.editor->isDragActiveForTest());
    f.dragTo(from, to);
    EXPECT_EQ(f.doc.getRevision(), rev) << "the doc is not touched during the drag";
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4}));
    ASSERT_TRUE(f.editor->getStretchForTest().isActive());
    const auto& preview = f.editor->getStretchForTest().result().moved;
    ASSERT_EQ(preview.size(), 4u);
    EXPECT_DOUBLE_EQ(preview.back().beat, 7.0) << "the edge snapped to the grid";
    EXPECT_DOUBLE_EQ(preview[1].beat, 3.0);
    EXPECT_DOUBLE_EQ(preview[2].beat, 5.0) << "proportional about the left edge";

    f.release(from, to);
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one doc mutation for the whole gesture";
    EXPECT_EQ(f.beats(), Beats({1, 3, 5, 7}));
    EXPECT_EQ(f.values(), Beats({20, 60, 40, 80})) << "only the beats change";
    EXPECT_EQ(f.selected(), Beats({1, 3, 5, 7})) << "the same points, at their new beats";
    EXPECT_FALSE(f.editor->isDragActiveForTest());

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4})) << "one Cmd+Z restores them";
}

TEST(AutomationLanesStretchTest, GrowingIntoTheNextPointPushesItOnInTheSameUndoStep) {
    StretchLane f;
    f.doc.addBreakpoint(f.lane, 8.0, 70.0);
    const auto from = f.handle(StretchHandle::Right);
    const auto rev = f.doc.getRevision();

    f.drag(from, f.beatsRight(from, 3.0)); // the edge to beat 7, past the point at 6
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_EQ(f.selected(), Beats({1, 3, 5, 7}));
    EXPECT_EQ(f.beats(), Beats({1, 3, 5, 7, 8, 10})) << "6 and 8 moved out of the way by one step beyond the edge";
    EXPECT_EQ(f.values(), Beats({20, 60, 40, 80, 50, 70}));

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4, 6, 8})) << "one Cmd+Z restores the stretched and the pushed";
}

TEST(AutomationLanesStretchTest, SqueezingNeverPushes) {
    StretchLane f;
    const auto from = f.handle(StretchHandle::Right);
    f.drag(from, f.beatsRight(from, -1.0)); // the edge back to beat 3
    ASSERT_EQ(f.selected().size(), 4u);
    EXPECT_DOUBLE_EQ(f.selected().back(), 3.0);
    EXPECT_NEAR(f.selected()[1], 1.0 + 2.0 / 3.0, 1e-9);
    EXPECT_DOUBLE_EQ(f.theLane().points.back().beat, 6.0) << "the point beyond stays where it was";
}

TEST(AutomationLanesStretchTest, TheLeftHandleSqueezesTowardsTheRightEdge) {
    StretchLane f;
    const auto from = f.handle(StretchHandle::Left);
    f.drag(from, f.beatsRight(from, 2.0)); // the left edge from beat 1 to beat 3
    const auto beats = f.selected();
    ASSERT_EQ(beats.size(), 4u);
    EXPECT_DOUBLE_EQ(beats.front(), 3.0);
    EXPECT_DOUBLE_EQ(beats.back(), 4.0) << "the right edge is the anchor";
    EXPECT_NEAR(beats[1], 3.0 + 1.0 / 3.0, 1e-9);
    EXPECT_NEAR(beats[2], 3.0 + 2.0 / 3.0, 1e-9);
}

TEST(AutomationLanesStretchTest, TheTopHandleScalesTheValuesAboutTheLowestOne) {
    StretchLane f;
    const auto from = f.handle(StretchHandle::Top);
    const auto to = from + juce::Point<float>(0.0f, 12.0f); // down: the highest value shrinks
    f.drag(from, to);
    const double delta = f.editor->yToValue(to.y) - f.editor->yToValue(from.y);
    const double scale = (80.0 + delta - 20.0) / 60.0;
    ASSERT_LT(scale, 1.0);
    const auto values = f.values();
    ASSERT_EQ(values.size(), 5u);
    EXPECT_NEAR(values[0], 20.0, 1e-4) << "the lowest value is the anchor";
    EXPECT_NEAR(values[1], 20.0 + 40.0 * scale, 1e-4);
    EXPECT_NEAR(values[2], 20.0 + 20.0 * scale, 1e-4);
    EXPECT_NEAR(values[3], 20.0 + 60.0 * scale, 1e-4);
    EXPECT_DOUBLE_EQ(values[4], 50.0) << "an unselected point is not touched";
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4, 6})) << "values only: beats and order stay";
    EXPECT_EQ(f.selected(), Beats({1, 2, 3, 4}));
}

TEST(AutomationLanesStretchTest, TheBottomHandleScalesTheValuesAboutTheHighestOneAndStaysInRange) {
    StretchLane f;
    const auto from = f.handle(StretchHandle::Bottom);
    const auto rev = f.doc.getRevision();
    f.drag(from, from + juce::Point<float>(0.0f, -8.0f)); // up: the lowest value rises
    const auto values = f.values();
    EXPECT_NEAR(values[3], 80.0, 1e-6) << "the highest value is the anchor";
    EXPECT_GT(values[0], 20.0);
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    for (double v : values) {
        EXPECT_GE(v, 0.0);
        EXPECT_LE(v, 100.0);
    }
    f.undo.undo();
    EXPECT_EQ(f.values(), Beats({20, 60, 40, 80, 50}));
}

TEST(AutomationLanesStretchTest, EscapeMidDragCancelsWithNothingCommitted) {
    StretchLane f;
    const auto from = f.handle(StretchHandle::Right);
    const auto to = f.beatsRight(from, 3.0);
    const auto rev = f.doc.getRevision();

    f.press(from);
    f.dragTo(from, to);
    ASSERT_TRUE(f.editor->getStretchForTest().isActive());
    EXPECT_TRUE(f.key(juce::KeyPress::escapeKey, 0)) << "the editor consumes the key";
    EXPECT_FALSE(f.editor->isDragActiveForTest());
    EXPECT_FALSE(f.editor->getStretchForTest().isActive());

    f.release(from, to);
    EXPECT_EQ(f.doc.getRevision(), rev);
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4, 6}));
    EXPECT_EQ(f.selected(), Beats({1, 2, 3, 4})) << "the selection survives the cancel";
}

TEST(AutomationLanesStretchTest, ADragThatSnapsBackToTheStartCommitsNothing) {
    StretchLane f;
    const auto from = f.handle(StretchHandle::Right);
    const auto rev = f.doc.getRevision();
    f.drag(from, from + juce::Point<float>(4.0f, 0.0f)); // a tenth of a beat: snaps to where the edge was
    EXPECT_EQ(f.doc.getRevision(), rev);
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4, 6}));
}

TEST(AutomationLanesStretchTest, AHandleTakesThePressBeforeAPointUnderIt) {
    StretchLane f;
    const auto spot = f.handle(StretchHandle::Right);
    // An unselected point exactly under the right handle.
    const double beat = f.panel.getViewState().xToBeat((double)spot.x);
    f.doc.addBreakpoint(f.lane, beat, f.editor->yToValue(spot.y));
    f.select({1.0, 2.0, 3.0, 4.0});
    ASSERT_FALSE(f.editor->getStretchBoxForTest().isEmpty());

    f.press(spot);
    EXPECT_TRUE(f.editor->getStretchForTest().isActive()) << "the handle won, not the point";
    EXPECT_EQ(f.selected(), Beats({1, 2, 3, 4}));
    f.release(spot, spot);
}

TEST(AutomationLanesStretchTest, TheInsideOfTheBoxTakesNoClicksSoAPointInsideStillMovesTheSelection) {
    StretchLane f;
    const auto rev = f.doc.getRevision();
    const auto inside = f.at(2.0, 60.0);

    f.press(inside);
    EXPECT_FALSE(f.editor->getStretchForTest().isActive());
    f.release(inside, inside);

    f.drag(inside, inside + juce::Point<float>(40.0f, 0.0f));
    EXPECT_GT(f.doc.getRevision(), rev);
    EXPECT_EQ(f.selected(), Beats({2, 3, 4, 5})) << "a plain move of the whole group, not a stretch";
}

TEST(AutomationLanesStretchTest, AltShiftLeftAndRightStretchOrSqueezeByOneGridStepInOneUndoStep) {
    StretchLane f;
    const auto rev = f.doc.getRevision();

    EXPECT_TRUE(f.key(juce::KeyPress::rightKey, kAltShift));
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one key press, one undo step";
    EXPECT_EQ(f.selected().size(), 4u);
    EXPECT_DOUBLE_EQ(f.selected().front(), 1.0);
    EXPECT_DOUBLE_EQ(f.selected().back(), 5.0) << "the right edge moved one grid step";
    EXPECT_NEAR(f.selected()[1], 1.0 + 4.0 / 3.0, 1e-9);

    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4, 6}));

    f.select({1, 2, 3, 4});
    EXPECT_TRUE(f.key(juce::KeyPress::leftKey, kAltShift));
    EXPECT_DOUBLE_EQ(f.selected().back(), 3.0) << "Left squeezes";
}

TEST(AutomationLanesStretchTest, AltShiftRightPushesTheNextPointLikeTheDrag) {
    StretchLane f;
    f.select({1.0, 2.0, 3.0, 4.0});
    EXPECT_TRUE(f.key(juce::KeyPress::rightKey, kAltShift));
    EXPECT_EQ(f.beats().back(), 6.0) << "a step short of the point at 6: it stays";
    EXPECT_TRUE(f.key(juce::KeyPress::rightKey, kAltShift));
    EXPECT_DOUBLE_EQ(f.selected().back(), 6.0);
    EXPECT_DOUBLE_EQ(f.beats().back(), 7.0) << "the point it reached is pushed one step beyond the edge";
}

TEST(AutomationLanesStretchTest, AltShiftUpAndDownScaleTheValuesByFivePercentAboutTheLowest) {
    StretchLane f;
    const auto rev = f.doc.getRevision();
    EXPECT_TRUE(f.key(juce::KeyPress::upKey, kAltShift));
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    const auto up = f.values();
    EXPECT_NEAR(up[0], 20.0, 1e-9);
    EXPECT_NEAR(up[1], 62.0, 1e-9);
    EXPECT_NEAR(up[2], 41.0, 1e-9);
    EXPECT_NEAR(up[3], 83.0, 1e-9);
    EXPECT_EQ(f.selected(), Beats({1, 2, 3, 4}));

    EXPECT_TRUE(f.key(juce::KeyPress::downKey, kAltShift));
    EXPECT_LT(f.values()[3], 83.0);
}

TEST(AutomationLanesStretchTest, TheStretchKeysNeedTwoSelectedPoints) {
    StretchLane f;
    f.select({2.0});
    const auto rev = f.doc.getRevision();
    EXPECT_FALSE(f.key(juce::KeyPress::rightKey, kAltShift));
    EXPECT_FALSE(f.key(juce::KeyPress::upKey, kAltShift));
    EXPECT_EQ(f.doc.getRevision(), rev);
}

TEST(AutomationLanesStretchTest, HoveringAHandleShowsTheResizeCursorAndItsHint) {
    StretchLane f;
    const auto arrow = juce::MouseCursor(juce::MouseCursor::NormalCursor);
    f.editor->mouseMove(makeClickEvent(*f.editor, f.handle(StretchHandle::Right)));
    EXPECT_TRUE(f.editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor));
    EXPECT_EQ(f.editor->getTooltip(), "Drag to stretch the selected points in time");

    f.editor->mouseMove(makeClickEvent(*f.editor, f.handle(StretchHandle::Left)));
    EXPECT_TRUE(f.editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor));

    f.editor->mouseMove(makeClickEvent(*f.editor, f.handle(StretchHandle::Top)));
    EXPECT_TRUE(f.editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::UpDownResizeCursor));
    EXPECT_EQ(f.editor->getTooltip(), "Drag to scale their values");

    f.editor->mouseMove(makeClickEvent(*f.editor, f.handle(StretchHandle::Bottom)));
    EXPECT_TRUE(f.editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::UpDownResizeCursor));

    f.editor->mouseMove(makeClickEvent(*f.editor, {600.0f, 20.0f}));
    EXPECT_TRUE(f.editor->getMouseCursor() == arrow);
    EXPECT_NE(f.editor->getTooltip(), "Drag to scale their values");

    f.press(f.handle(StretchHandle::Right));
    EXPECT_TRUE(f.editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor))
        << "the resize cursor stays for the whole drag";
    f.release(f.handle(StretchHandle::Right), f.handle(StretchHandle::Right));
}

TEST(AutomationLanesStretchTest, TheBoxFollowsTheSelectionAndLandsAtOnceUnderReducedMotion) {
    ReducedMotionGuard noMotion;
    StretchLane f;
    EXPECT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 1.0f);
    f.select({3.0});
    EXPECT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 0.0f);
    f.select({1.0, 3.0});
    EXPECT_FLOAT_EQ(f.editor->getStretchForTest().boxAlpha(), 1.0f);
}

TEST(AutomationLanesStretchTest, TheScreenReaderDescriptionMentionsTheStretchKeysOnlyWithTwoOrMoreSelected) {
    StretchLane f;
    EXPECT_TRUE(f.editor->getDescription().containsIgnoreCase("stretch"));
    f.select({2.0});
    EXPECT_FALSE(f.editor->getDescription().containsIgnoreCase("stretch"));
}

TEST(AutomationLanesStretchTest, DraggingInsideTheBoxMovesTheWholeSelectionAsOneUndoStep) {
    StretchLane f;
    // Between the points of the selection, off every dot and handle, where the box (not the curve) owns the press.
    const auto inside = f.at(2.5, 50.0);
    ASSERT_TRUE(f.editor->getStretchBoxForTest().contains(inside));
    const auto rev = f.doc.getRevision();
    const auto to = f.beatsRight(inside, 1.0);

    f.press(inside);
    EXPECT_TRUE(f.editor->isDragActiveForTest());
    EXPECT_EQ(f.editor->getMouseCursor(), synth::ui::dragGrabCursor()) << "the hand once the drag has started";
    f.dragTo(inside, to);
    EXPECT_EQ(f.doc.getRevision(), rev) << "the doc is not touched during the drag";
    f.release(inside, to);

    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one doc mutation for the whole gesture";
    EXPECT_EQ(f.beats(), Beats({2, 3, 4, 5, 6})) << "every selected point moved by a beat";
    EXPECT_EQ(f.values(), Beats({20, 60, 40, 80, 50})) << "values stay";
    EXPECT_EQ(f.selected(), Beats({2, 3, 4, 5})) << "the same points stay selected";
    f.undo.undo();
    EXPECT_EQ(f.beats(), Beats({1, 2, 3, 4, 6}));
}

TEST(AutomationLanesStretchTest, HoveringInsideTheBoxShowsNoHand) {
    StretchLane f;
    f.editor->mouseMove(makeClickEvent(*f.editor, f.at(2.5, 50.0)));
    EXPECT_TRUE(f.editor->getMouseCursor() == juce::MouseCursor(juce::MouseCursor::NormalCursor));
}

TEST(AutomationLanesStretchTest, TheBoxStaysInsideTheLaneWhenPointsSitAtItsFarLeftAndTop) {
    StretchLane f;
    f.doc.removeBreakpoint(f.lane, 6.0);
    const auto& range = f.theLane().range;
    f.doc.addBreakpoint(f.lane, 0.0, range.maxValue);
    f.doc.addBreakpoint(f.lane, 0.5, range.maxValue);
    f.select({0.0, 0.5});
    const auto box = f.editor->getStretchBoxForTest();
    const auto bounds = f.editor->getLocalBounds().toFloat();
    ASSERT_FALSE(box.isEmpty());
    EXPECT_TRUE(bounds.contains(box)) << "no edge of the box is cut off by the lane's edge";
}

TEST(AutomationLanesStretchTest, TheLanesLimitsSitInsideItsEdgesSoTheLineAndDotsAreDrawnWhole) {
    StretchLane f;
    const auto& range = f.theLane().range;
    const double height = f.editor->getHeight();
    EXPECT_GE(f.editor->valueToY(range.maxValue), 4.0) << "the maximum is below the top edge";
    EXPECT_LE(f.editor->valueToY(range.minValue), height - 4.0) << "the minimum is above the bottom edge";
    const double lo = range.minValue, hi = range.maxValue;
    for (double v : {lo, 0.5 * (lo + hi), hi})
        EXPECT_NEAR(f.editor->yToValue(f.editor->valueToY(v)), v, 1e-6) << "the mapping still round-trips";

    // The curve at the minimum is painted on the lane, not off its bottom edge.
    f.doc.removeBreakpoint(f.lane, 6.0);
    for (double b : {1.0, 2.0, 3.0, 4.0})
        f.doc.addBreakpoint(f.lane, b, range.minValue);
    juce::Image image(juce::Image::ARGB, f.editor->getWidth(), f.editor->getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    f.editor->paint(g);
    const int y = juce::roundToInt(f.editor->valueToY(range.minValue));
    const int x = juce::roundToInt(f.at(2.5, range.minValue).x);
    bool drawn = false;
    for (int dy = -2; dy <= 2; ++dy)
        drawn = drawn || image.getPixelAt(x, y + dy) != image.getPixelAt(x, y - 12);
    EXPECT_TRUE(drawn) << "the line at the minimum is visible";
}
