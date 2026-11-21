// AutomationLanesBendDragTests.cpp -- a press on the line between two lane points bends it so the curve follows the
// pointer, and dragging all the way toward the start value and the overshoot further makes the segment a hold: one undo
// step, the preview drawn through the same points the release commits. Real events on the lane editor.

#include "../TimelinePanel/TimelinePanelTestEvents.h"
#include "AppUndoManager.h"
#include "Timeline/AutomationKernel.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneHoldBlend.h"
#include <cmath>
#include <gtest/gtest.h>
#include <string>

using synth::ui::AutomationLaneEditor;
using synth::ui::LaneBreakpoint;

namespace {

constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);

bool samePoints(const std::vector<LaneBreakpoint>& a, const std::vector<LaneBreakpoint>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].beat != b[i].beat || a[i].value != b[i].value || a[i].tension != b[i].tension ||
            a[i].curve != b[i].curve)
            return false;
    return true;
}

struct BendLane {
    synth::TimelineDoc doc;
    synth::ui::TimelineViewState state;
    AppUndoManager undo;
    AutomationLaneEditor editor{state};
    synth::LaneId laneId;

    BendLane(double startValue, double endValue, int curve = kLinear) {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        editor.setTimelineDoc(&doc);
        editor.setUndoManager(&undo);
        editor.setSize(1000, 200);
        const auto track = doc.addTrack(synth::TrackKind::Automation, "Automation");
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 100.0f;
        range.defaultValue = 50.0f;
        laneId = doc.addLane(track, "node-uuid-1", "cutoff", range);
        editor.setActiveLane(laneId);
        doc.addBreakpoint(laneId, 0.0, startValue, 0.0f, curve);
        doc.addBreakpoint(laneId, 4.0, endValue);
    }

    const std::vector<LaneBreakpoint>& points() const { return doc.getLane(laneId)->points; }
    const LaneBreakpoint& first() const { return points()[0]; }
    double startY() const { return editor.valueToY(first().value); }
    double endY() const { return editor.valueToY(points()[1].value); }
    // The committed curve's value at the segment's half-way beat, and its pixel row.
    double midValue() const {
        std::vector<synth::TimelineSnapshot::Point> shape;
        for (const auto& bp : points())
            shape.push_back({bp.beat, bp.value, bp.tension, bp.curve});
        synth::AutomationCursor cursor{};
        return synth::AutomationKernel::evaluate(shape.data(), (int)shape.size(), 2.0, 50.0, cursor);
    }
    double midY() const { return editor.valueToY(midValue()); }
    juce::Point<float> midPoint() const { return {(float)state.beatToX(2.0), (float)midY()}; }

    static juce::ModifierKeys buttons(int extra = 0) {
        return juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extra);
    }
    void press(juce::Point<float> p, int extra = 0) { editor.mouseDown(makeClickEvent(editor, p, buttons(extra))); }
    void dragTo(juce::Point<float> p, juce::Point<float> from, int extra = 0) {
        editor.mouseDrag(makeDragEvent(editor, p, from, buttons(extra)));
    }
    void release(juce::Point<float> p, juce::Point<float> from, int extra = 0) {
        editor.mouseUp(makeDragEvent(editor, p, from, buttons(extra)));
    }
    // The pixel row where the half-way point sits `fraction` of the way from the start value to the end value.
    double yForFraction(double fraction) const { return startY() + fraction * (endY() - startY()); }
    // How far past the extreme this segment's drag goes before it becomes a hold.
    double overshoot() const { return synth::ui::holdOvershootPx(); }
    // The committed curve's value at `beat`, and the pixel on it a press at `fraction` of the way across the segment
    // lands on.
    double valueAtBeat(double beat) const {
        std::vector<synth::TimelineSnapshot::Point> shape;
        for (const auto& bp : points())
            shape.push_back({bp.beat, bp.value, bp.tension, bp.curve});
        synth::AutomationCursor cursor{};
        return synth::AutomationKernel::evaluate(shape.data(), (int)shape.size(), beat, 50.0, cursor);
    }
    juce::Point<float> pointOnLine(double fraction) const {
        const double beat = 4.0 * fraction;
        return {(float)state.beatToX(beat), (float)editor.valueToY(valueAtBeat(beat))};
    }
    // A row `overshoot` pixels past the tension +1 extreme, toward the start value.
    juce::Point<float> pastExtreme(juce::Point<float> from, double overshoot) const {
        return {from.x, (float)(yForFraction(synth::ui::minMidFraction()) + overshoot)};
    }
};

} // namespace

// Near the press the half-way point moves 1:1 with the pointer either way (toward the end value the travel then eases,
// so the curve reaches its limit at the lane's edge: see EveryRowOfTheLaneBendsTheCurveTowardTheEndValue).
TEST(AutomationLaneBendDrag, RisingSegmentFollowsThePointerUpAndDown) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const double chordY = f.midY();
    f.press(from);
    f.dragTo({from.x, from.y - 6.0f}, from);
    f.release({from.x, from.y - 6.0f}, from);
    EXPECT_NEAR(f.midY(), chordY - 6.0, 1.0) << "dragged up 6 px: the half-way point went up with it";

    const auto again = f.midPoint();
    f.press(again);
    f.dragTo({again.x, again.y + 40.0f}, again);
    f.release({again.x, again.y + 40.0f}, again);
    EXPECT_NEAR(f.midY(), (double)again.y + 40.0, 1.0) << "down 40 px toward the start value: 1:1";
}

TEST(AutomationLaneBendDrag, FallingSegmentFollowsThePointer) {
    BendLane f(80.0, 20.0);
    const auto from = f.midPoint();
    const double chordY = f.midY();
    f.press(from);
    f.dragTo({from.x, from.y - 20.0f}, from);
    f.release({from.x, from.y - 20.0f}, from);
    EXPECT_NEAR(f.midY(), chordY - 20.0, 1.0);
    EXPECT_GT(f.first().tension, 0.0f) << "a falling segment that bulges up has a positive tension";

    const auto again = f.midPoint();
    f.press(again);
    f.dragTo({again.x, again.y + 6.0f}, again);
    f.release({again.x, again.y + 6.0f}, again);
    EXPECT_NEAR(f.midY(), (double)again.y + 6.0, 1.0);
}

// Regression test for FRO2153: dragging a falling segment down did nothing once the curve hit its limit, a third of
// the way down; the rest of the lane was dead. Every few rows down to the lane's bottom now bend it further, and the
// limit arrives at the last row.
TEST(AutomationLaneBendDrag, EveryRowOfTheLaneBendsTheCurveTowardTheEndValue) {
    for (const bool falling : {true, false}) {
        BendLane f(falling ? 100.0 : 0.0, falling ? 0.0 : 100.0);
        const auto from = f.midPoint();
        const float edge = falling ? (float)f.editor.getHeight() - 1.0f : 0.0f;
        const float step = falling ? 1.0f : -1.0f;
        f.press(from);
        float last = f.editor.getBendPreviewPointsForTest()[0].tension;
        juce::Point<float> at = from;
        for (float y = from.y + step; falling ? y <= edge : y >= edge; y += step) {
            at = {from.x, y};
            f.dragTo(at, from);
            const float t = f.editor.getBendPreviewPointsForTest()[0].tension;
            EXPECT_LT(t, last) << (falling ? "falling" : "rising") << " row " << y << ": the curve stood still";
            last = t;
        }
        EXPECT_NEAR(last, -1.0f, 1e-4f) << "the limit is reached at the lane's edge";
        f.release(at, from);
        EXPECT_NEAR(f.first().tension, last, 1e-6f);
    }
}

// preview-vs-commit: for every start value x end value x direction (lane edges included), a press
// anywhere along the line (near either dot too), a drag in single-pixel steps to every row of the lane (the first and
// last rows included) and a release there, the committed points are exactly the points the last drag event drew: the
// curve at its tension, or the hold, never a nearer shape and never the shape it had.
TEST(AutomationLaneBendDrag, TheLastDrawnFrameIsWhatEveryReleaseCommits) {
    int bent = 0;
    int total = 0;
    for (const double startValue : {0.0, 30.0, 70.0, 100.0})
        for (const double endValue : {0.0, 30.0, 70.0, 100.0}) {
            if (startValue == endValue)
                continue;
            for (const double along : {0.2, 0.5, 0.8})
                for (int row = 0; row < 200; row += (row < 4 || row > 195) ? 1 : 3) {
                    BendLane f(startValue, endValue);
                    const auto from = f.pointOnLine(along);
                    const auto original = f.points();
                    f.press(from);
                    const float dir = (float)row > from.y ? 1.0f : -1.0f;
                    juce::Point<float> at = from;
                    for (float y = from.y + dir; dir > 0 ? y <= (float)row : y >= (float)row; y += dir) {
                        at = {from.x, y};
                        f.dragTo(at, from);
                    }
                    const auto drawn = f.editor.getBendPreviewPointsForTest();
                    f.release(at, from);
                    const std::string where = std::to_string(startValue) + "->" + std::to_string(endValue) + " at " +
                                              std::to_string(along) + " row " + std::to_string(row);
                    EXPECT_TRUE(samePoints(f.points(), drawn)) << where << ": the release changed what was drawn";
                    ++total;
                    bent += samePoints(f.points(), original) ? 0 : 1;
                }
        }
    EXPECT_GT(bent, total / 2) << "the presses land on the line and the drags bend it";
}

// The same sweep without a release: every row from the press to either lane edge moves the drawn curve (the steepest
// curve rests across the hold's overshoot), the limit arrives at the edge (tension -1 toward the end value, the hold
// toward the start value), and nothing is a hold on the way to the end value.
TEST(AutomationLaneBendDrag, EveryRowTowardEitherEdgeMovesTheCurveFromAnyPress) {
    for (const bool falling : {true, false})
        for (const double along : {0.2, 0.5, 0.8}) {
            BendLane f(falling ? 100.0 : 0.0, falling ? 0.0 : 100.0);
            const auto from = f.pointOnLine(along);
            const float topRow = 0.0f;
            const float bottomRow = (float)f.editor.getHeight() - 1.0f;
            const std::string where = std::string(falling ? "falling" : "rising") + " at " + std::to_string(along);

            // Toward the end value (down for a falling segment).
            f.press(from);
            float last = f.editor.getBendPreviewPointsForTest()[0].tension;
            const float endEdge = falling ? bottomRow : topRow;
            const float endStep = falling ? 1.0f : -1.0f;
            for (float y = from.y + endStep; falling ? y <= endEdge : y >= endEdge; y += endStep) {
                f.dragTo({from.x, y}, from);
                const float t = f.editor.getBendPreviewPointsForTest()[0].tension;
                EXPECT_FALSE(f.editor.isBendPreviewHoldForTest()) << where << " row " << y;
                if (last > -0.95f)
                    EXPECT_LT(t, last) << where << " row " << y << ": the curve stood still";
                last = t;
            }
            f.dragTo({from.x, endEdge}, from); // the lane's last row itself (the steps above round to whole rows)
            last = f.editor.getBendPreviewPointsForTest()[0].tension;
            EXPECT_NEAR(last, -1.0f, 1e-2f) << where << ": the limit arrives at the edge";
            f.dragTo({from.x, from.y}, from);

            // Toward the start value (up for a falling segment): the curve to its steepest, then the hold, by the edge.
            const float startEdge = falling ? topRow : bottomRow;
            const float startStep = -endStep;
            last = f.editor.getBendPreviewPointsForTest()[0].tension;
            for (float y = from.y + startStep; falling ? y >= startEdge : y <= startEdge; y += startStep) {
                f.dragTo({from.x, y}, from);
                const float t = f.editor.getBendPreviewPointsForTest()[0].tension;
                if (!f.editor.isBendPreviewHoldForTest() && last < 0.95f)
                    EXPECT_GT(t, last) << where << " row " << y << ": the curve stood still";
                last = t;
            }
            f.dragTo({from.x, startEdge}, from);
            EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << where << ": the hold is reached by the lane's edge";
            f.release({from.x, startEdge}, from);
            EXPECT_EQ(f.first().curve, kHold) << where;
        }
}

TEST(AutomationLaneBendDrag, DragToTheExtremeAndTheOvershootMoreMakesAHoldInOneStep) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto rev = f.doc.getRevision();
    const auto to = f.pastExtreme(from, f.overshoot() + 1.0);

    f.press(from);
    f.dragTo(to, from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest());
    const auto preview = f.editor.getBendPreviewPointsForTest();
    EXPECT_EQ(preview[0].curve, kHold) << "the preview is drawn from the same points the release commits";
    EXPECT_EQ(f.doc.getRevision(), rev) << "no mutation during the drag";
    f.release(to, from);

    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one undo step writes the curve and the tension";
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_EQ(f.first().tension, 1.0f);
    EXPECT_TRUE(samePoints(f.points(), preview)) << "the committed points are the previewed ones";
    EXPECT_NEAR(f.midValue(), 20.0, 1e-9) << "a hold plays the start value";

    f.undo.undo();
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_EQ(f.first().tension, 0.0f);
}

// Two pixels short of the hold the drag is still the steepest curve, and that curve, as drawn, is what a release keeps.
TEST(AutomationLaneBendDrag, LetGoInsideTheOvershootKeepsTheSteepestCurveAsDrawn) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto to = f.pastExtreme(from, f.overshoot() - 2.0);
    f.press(from);
    f.dragTo(to, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    const auto drawn = f.editor.getBendPreviewPointsForTest();
    EXPECT_NEAR(drawn[0].tension, 1.0f, 1e-5f);
    EXPECT_EQ(f.editor.bendToHoldForTest(), 0.0f) << "no half-way shape is drawn that a release could not keep";
    f.release(to, from);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_TRUE(samePoints(f.points(), drawn));
}

TEST(AutomationLaneBendDrag, BackAboveTheThresholdBeforeReleaseIsNotAHold) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto deep = f.pastExtreme(from, f.overshoot() + 6.0);
    const auto back = f.pastExtreme(from, 3.0);
    f.press(from);
    f.dragTo(deep, from);
    ASSERT_TRUE(f.editor.isBendPreviewHoldForTest());
    f.dragTo(back, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(back, from);
    EXPECT_EQ(f.first().curve, kLinear);
}

TEST(AutomationLaneBendDrag, DraggingTowardTheEndValueNeverMakesAHold) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const juce::Point<float> to(from.x, from.y - 500.0f);
    f.press(from);
    f.dragTo(to, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(to, from);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_EQ(f.first().tension, -1.0f);
}

TEST(AutomationLaneBendDrag, AHoldDraggedTowardTheEndOutOfItsZoneBecomesACurve) {
    BendLane f(20.0, 80.0, kHold);
    const juce::Point<float> from((float)f.state.beatToX(2.0), (float)f.startY());
    const auto rev = f.doc.getRevision();

    f.press(from);
    f.dragTo({from.x, from.y - 5.0f}, from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << "a short drag is still a hold";
    f.release({from.x, from.y - 5.0f}, from);
    EXPECT_EQ(f.doc.getRevision(), rev) << "a hold left a hold writes nothing";

    // Out of the hold (about 11 px) and most of the way back across its overshoot, so the curve is the nearer shape.
    const float out = (float)(synth::ui::kHoldExitPx + 0.7 * f.overshoot());
    f.press(from);
    f.dragTo({from.x, from.y - 13.0f}, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.dragTo({from.x, from.y - out}, from);
    f.release({from.x, from.y - out}, from);
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_NEAR(f.first().tension, 1.0f, 1e-5f) << "it comes back as the steepest curve, continuous with the step";

    f.undo.undo();
    EXPECT_EQ(f.first().curve, kHold);
}

TEST(AutomationLaneBendDrag, EscapeCancelsAndShiftIsFine) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto rev = f.doc.getRevision();
    f.press(from);
    f.dragTo({from.x, from.y - 30.0f}, from);
    EXPECT_TRUE(f.editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    f.release({from.x, from.y - 30.0f}, from);
    EXPECT_EQ(f.doc.getRevision(), rev);

    const double chordY = f.midY();
    const int shift = juce::ModifierKeys::shiftModifier;
    f.press(from, shift);
    f.dragTo({from.x, from.y - 30.0f}, from, shift);
    f.release({from.x, from.y - 30.0f}, from, shift);
    EXPECT_NEAR(f.midY(), chordY - 3.0, 1.0) << "Shift counts a tenth of the pointer's travel";
}

TEST(AutomationLaneBendDrag, TheSwitchToAHoldBlendsInsteadOfPopping) {
    synth::ui::setReducedMotionForTest(false);
    {
        BendLane f(20.0, 80.0);
        f.editor.forceBendBlendAnimateForTest(true);
        const auto from = f.midPoint();
        const auto deep = f.pastExtreme(from, f.overshoot() + 6.0);
        f.press(from);
        f.dragTo(deep, from);
        EXPECT_TRUE(f.editor.isBendBlendRunningForTest());
        f.release(deep, from);
        EXPECT_TRUE(f.editor.isBendBlendRunningForTest()) << "let go mid-glide, the line finishes onto the step";
        EXPECT_EQ(f.first().curve, kHold);
    }

    synth::ui::setReducedMotionForTest(true);
    {
        BendLane g(20.0, 80.0);
        g.editor.forceBendBlendAnimateForTest(true);
        const auto from = g.midPoint();
        g.press(from);
        g.dragTo(g.pastExtreme(from, g.overshoot() + 6.0), from);
        EXPECT_FALSE(g.editor.isBendBlendRunningForTest()) << "Reduce Motion lands the step at once";
        g.release(from, from);
    }
    synth::ui::setReducedMotionForTest(std::nullopt);
}

// asymmetric-gesture-zone: the hold zone is the start value's side whichever way the segment runs, so a falling segment
// dragged UP (past its high start) holds exactly as a rising one dragged down does, and neither holds toward its end.
TEST(AutomationLaneBendDrag, AFallingSegmentHoldsPastItsHighStartAndANearMissStaysACurve) {
    BendLane f(80.0, 20.0);
    const auto from = f.midPoint();
    ASSERT_LT(f.startY(), f.endY()) << "a falling segment: the start is the upper dot";
    // Past the extreme toward the (upper) start value is up the screen: the row moves by minus the overshoot.
    const auto rowPast = [&](double overshoot) {
        return juce::Point<float>(from.x, (float)(f.yForFraction(synth::ui::minMidFraction()) - overshoot));
    };
    const auto short_ = rowPast(f.overshoot() - 2.0);
    const auto deep = rowPast(f.overshoot() + 1.0);
    ASSERT_LT(deep.y, short_.y) << "past the extreme of a falling segment is up the screen";

    f.press(from);
    f.dragTo(short_, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.dragTo(deep, from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest());
    f.release(deep, from);
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_NEAR(f.midValue(), 80.0, 1e-9) << "a hold plays the start value";

    BendLane g(80.0, 20.0);
    const auto start = g.midPoint();
    const juce::Point<float> low(start.x, start.y + 500.0f);
    g.press(start);
    g.dragTo(low, start);
    EXPECT_FALSE(g.editor.isBendPreviewHoldForTest()) << "toward the end value only clamps";
    g.release(low, start);
    EXPECT_EQ(g.first().curve, kLinear);
}

// Real events: a falling segment from the lane's top value bends into a hold with the pointer inside the lane (its hold
// zone used to start above the lane).
TEST(AutomationLaneBendDrag, AtTheTopOfTheLaneTheDragHoldsInsideTheLane) {
    BendLane f(100.0, 0.0);
    ASSERT_LT(f.startY(), 10.0) << "the segment starts at the lane's top value";
    const auto from = f.midPoint();
    f.press(from);
    for (float y = from.y; y >= 0.0f; y -= 1.0f)
        f.dragTo({from.x, y}, from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << "the hold is reached inside the lane";
    f.release({from.x, 0.0f}, from);
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_NEAR(f.midValue(), 100.0, 1e-9);
}
