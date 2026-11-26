// AutomationLanesBendDragTests.cpp -- a press on the line between two lane points bends it so the curve passes under
// the pointer on every row, and dragging to the start row makes the segment a hold: one undo step, the preview drawn
// through the same points the release commits, wherever the pointer lets go. Real events on the lane editor.

#include "../TimelinePanel/TimelinePanelTestEvents.h"
#include "AppUndoManager.h"
#include "Timeline/AutomationKernel.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include <cmath>
#include <gtest/gtest.h>
#include <string>

using synth::ui::AutomationLaneEditor;
using synth::ui::LaneBreakpoint;

namespace {

constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);
constexpr int kJump = static_cast<int>(synth::BreakpointCurve::Jump);

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
    // The row `pixels` from the start row toward the end row (negative: past the start row).
    float rowAlong(double pixels) const { return (float)(startY() + pixels * (endY() >= startY() ? 1.0 : -1.0)); }
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
    // Moves the pointer a pixel at a time from `from` to row `row` (x unchanged); returns the last position.
    juce::Point<float> walk(juce::Point<float> from, float row, juce::Point<float> press) {
        juce::Point<float> at = from;
        const float dir = row > from.y ? 1.0f : -1.0f;
        for (float y = from.y + dir; dir > 0 ? y <= row : y >= row; y += dir) {
            at = {from.x, y};
            dragTo(at, press);
        }
        return at;
    }
    // Moves the pointer a pixel at a time from `press` toward the start row (or the end row) until the drag shows that
    // end's step; returns the row it shows on, which is within a pixel of the end row.
    juce::Point<float> walkToStep(juce::Point<float> press, bool toStart) {
        const double row = toStart ? startY() : endY();
        const float dir = row > press.y ? 1.0f : -1.0f;
        auto at = press;
        for (int i = 0; i < 400 && !(toStart ? editor.isBendPreviewHoldForTest() : editor.isBendPreviewJumpForTest());
             ++i) {
            at = {at.x, at.y + dir};
            dragTo(at, press);
        }
        EXPECT_LE(std::abs(at.y - row), 1.5) << "the step shows on the end row";
        return at;
    }
};

} // namespace

// The curve passes under the pointer: after a drag of dy rows the half-way point sits on the row the pointer let go on,
// either way, however far (the old drag moved it by an eased fraction of the travel).
TEST(AutomationLaneBendDrag, RisingSegmentCurvePassesUnderThePointerUpAndDown) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    f.press(from);
    f.dragTo({from.x, from.y - 6.0f}, from);
    f.release({from.x, from.y - 6.0f}, from);
    EXPECT_NEAR(f.midY(), from.y - 6.0, 1.5) << "dragged up 6 px: the half-way point went up with it";

    const auto again = f.midPoint();
    f.press(again);
    f.dragTo({again.x, again.y + 40.0f}, again);
    f.release({again.x, again.y + 40.0f}, again);
    EXPECT_NEAR(f.midY(), (double)again.y + 40.0, 1.5) << "down 40 px toward the start value: 1:1";
}

TEST(AutomationLaneBendDrag, FallingSegmentCurvePassesUnderThePointer) {
    BendLane f(80.0, 20.0);
    const auto from = f.midPoint();
    f.press(from);
    f.dragTo({from.x, from.y - 20.0f}, from);
    f.release({from.x, from.y - 20.0f}, from);
    EXPECT_NEAR(f.midY(), from.y - 20.0, 1.5);
    EXPECT_GT(f.first().tension, 0.0f) << "a falling segment that bulges up has a positive tension";

    const auto again = f.midPoint();
    f.press(again);
    f.dragTo({again.x, again.y + 6.0f}, again);
    f.release({again.x, again.y + 6.0f}, again);
    EXPECT_NEAR(f.midY(), (double)again.y + 6.0, 1.5);
}

// Regression test for FRO2153: the curve could not be dragged to the bottom of the lane. Every row down to the lane's
// last row bends a falling segment further (and up to the first row a rising one), until the limit at the end row.
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
            if (last > -synth::kMaxLaneTension + 1e-4f)
                EXPECT_LT(t, last) << (falling ? "falling" : "rising") << " row " << y << ": the curve stood still";
            last = t;
        }
        EXPECT_NEAR(last, -synth::kMaxLaneTension, 1e-4f) << "the limit is reached at the lane's edge";
        f.release(at, from);
        EXPECT_NEAR(f.first().tension, last, 1e-6f);
    }
}

// preview-vs-commit: for every start value x end value x direction (lane edges included), a press anywhere along the
// line, a drag in single-pixel steps to every row of the lane (the first and last included) and a release there, the
// committed points are exactly the points the last drag event drew: the curve at its tension, or the hold, never a
// nearer shape and never the shape it had. The release is also tried away from the last drag position, past the lane's
// top and bottom edges and after the pointer sat still for several timer ticks, and each bend is one undo step.
TEST(AutomationLaneBendDrag, TheLastDrawnFrameIsWhatEveryReleaseCommits) {
    int bent = 0;
    int total = 0;
    int variant = 0;
    for (const double startValue : {0.0, 30.0, 70.0, 100.0})
        for (const double endValue : {0.0, 30.0, 70.0, 100.0}) {
            if (startValue == endValue)
                continue;
            for (const double along : {0.2, 0.5, 0.8})
                for (int row = 0; row < 200; row += (row < 4 || row > 195) ? 1 : 3) {
                    BendLane f(startValue, endValue);
                    const auto from = f.pointOnLine(along);
                    const auto original = f.points();
                    const auto rev = f.doc.getRevision();
                    f.press(from);
                    const auto at = f.walk(from, (float)row, from);
                    const int how = variant++ % 5;
                    if (how == 4)
                        for (int tick = 0; tick < 6; ++tick)
                            f.editor.tickEdgeScrollForTest(); // the pointer sits still while timers run
                    const auto drawn = f.editor.getBendPreviewPointsForTest();
                    const juce::Point<float> where2[] = {
                        at, {at.x, at.y + 7.0f}, {at.x, -20.0f}, {at.x, (float)f.editor.getHeight() + 20.0f}, at};
                    f.release(where2[how], from);
                    const std::string where = std::to_string(startValue) + "->" + std::to_string(endValue) + " at " +
                                              std::to_string(along) + " row " + std::to_string(row) + " release " +
                                              std::to_string(how);
                    EXPECT_TRUE(samePoints(f.points(), drawn)) << where << ": the release changed what was drawn";
                    const bool changed = !samePoints(f.points(), original);
                    EXPECT_EQ(f.doc.getRevision(), rev + (changed ? 1u : 0u)) << where << ": one undo step";
                    if (changed) {
                        f.undo.undo();
                        EXPECT_TRUE(samePoints(f.points(), original)) << where << ": undo restores the curve";
                    }
                    ++total;
                    bent += changed ? 1 : 0;
                }
        }
    EXPECT_GT(bent, total / 2) << "the presses land on the line and the drags bend it";
}

// The release on the very start row (the step's row) and one row either side commits what was drawn there.
TEST(AutomationLaneBendDrag, LettingGoOnTheStartRowCommitsTheStepAsDrawn) {
    for (const bool falling : {true, false})
        for (const double pastPx : {-1.0, 0.0, 1.0, 3.0, 9.0}) {
            BendLane f(falling ? 80.0 : 20.0, falling ? 20.0 : 80.0);
            const auto from = f.midPoint();
            f.press(from);
            const auto at = f.walk(from, f.rowAlong(pastPx), from);
            const auto drawn = f.editor.getBendPreviewPointsForTest();
            f.release({at.x, at.y - 15.0f}, from);
            EXPECT_TRUE(samePoints(f.points(), drawn)) << (falling ? "falling " : "rising ") << pastPx;
        }
}

// Dragging to the start row (and one pixel past) is the step, in one undo step: the curve at its steepest, then a hold.
TEST(AutomationLaneBendDrag, DragToTheStartRowMakesAHoldInOneStep) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto rev = f.doc.getRevision();
    f.press(from);
    const auto short_ = f.walk(from, f.rowAlong(5.0), from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    EXPECT_GT(f.editor.getBendPreviewTensionForTest(), 1.0f) << "bending into the step, no wall";
    const auto to = f.walk(short_, f.rowAlong(-1.0), from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest());
    const auto preview = f.editor.getBendPreviewPointsForTest();
    EXPECT_EQ(preview[0].curve, kHold) << "the preview is drawn from the same points the release commits";
    EXPECT_EQ(f.doc.getRevision(), rev) << "no mutation during the drag";
    f.release(to, from);

    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one undo step writes the curve and the tension";
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_EQ(f.first().tension, synth::kMaxLaneTension);
    EXPECT_TRUE(samePoints(f.points(), preview)) << "the committed points are the previewed ones";
    EXPECT_NEAR(f.midValue(), 20.0, 1e-9) << "a hold plays the start value";

    f.undo.undo();
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_EQ(f.first().tension, 0.0f);
}

// Short of the start row the drag is still a curve, and that curve, as drawn, is what a release keeps.
TEST(AutomationLaneBendDrag, LetGoShortOfTheStartRowKeepsTheCurveAsDrawn) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    f.press(from);
    const auto to = f.walk(from, f.rowAlong(3.0), from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    const auto drawn = f.editor.getBendPreviewPointsForTest();
    f.release(to, from);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_TRUE(samePoints(f.points(), drawn));
}

// Hysteresis: back 1 px from inside the hold is still the hold; past 2 px it is a curve again.
TEST(AutomationLaneBendDrag, BackPastTheExitMarginBeforeReleaseIsNotAHold) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    f.press(from);
    const auto deep = f.walk(from, f.rowAlong(-6.0), from);
    ASSERT_TRUE(f.editor.isBendPreviewHoldForTest());
    auto at = f.walk(deep, f.rowAlong(1.0), from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << "a jitter does not leave the hold";
    at = f.walk(at, f.rowAlong(4.0), from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(at, from);
    EXPECT_EQ(f.first().curve, kLinear);
}

TEST(AutomationLaneBendDrag, DraggingPastTheEndValueMakesAJumpNeverAHold) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const juce::Point<float> to(from.x, from.y - 500.0f);
    f.press(from);
    f.dragTo(to, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    EXPECT_TRUE(f.editor.isBendPreviewJumpForTest());
    f.release(to, from);
    EXPECT_EQ(f.first().curve, kJump);
    EXPECT_NEAR(f.midValue(), 80.0, 1e-9) << "a jump plays the end value";
}

TEST(AutomationLaneBendDrag, AHoldDraggedTowardTheEndOutOfItsZoneBecomesACurve) {
    BendLane f(20.0, 80.0, kHold);
    const juce::Point<float> from((float)f.state.beatToX(2.0), (float)f.startY());
    const auto rev = f.doc.getRevision();

    f.press(from);
    f.dragTo({from.x, from.y - 1.0f}, from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << "a short drag is still a hold";
    f.release({from.x, from.y - 1.0f}, from);
    EXPECT_EQ(f.doc.getRevision(), rev) << "a hold left a hold writes nothing";

    f.press(from);
    const auto at = f.walk(from, from.y - 13.0f, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(at, from);
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_NEAR(f.midY(), at.y, 1.5) << "the curve it becomes passes under the pointer";

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
    EXPECT_NEAR(f.midY(), chordY - 3.0, 1.5) << "Shift counts a tenth of the pointer's travel";
}

// Regression test for FRO2292: crossing into the hold started a 240 ms time blend, so a pointer standing still on the
// start row drew a line that kept moving, and a jitter restarted it. Now the drawn shape is the pointer row's alone:
// standing still while timers run changes nothing, animations on or off.
TEST(AutomationLaneBendDrag, APointerStandingStillOnAnEndRowDrawsAStillLine) {
    synth::ui::setReducedMotionForTest(false);
    for (const bool toStart : {true, false}) {
        BendLane f(20.0, 80.0);
        const auto from = f.midPoint();
        f.press(from);
        const auto at = f.walkToStep(from, toStart);
        const auto drawn = f.editor.getBendPreviewPointsForTest();
        const auto bubble = f.editor.getPointBubbleForTest().getText();
        EXPECT_EQ(bubble, toStart ? "Hold" : "Jump");
        for (int tick = 0; tick < 20; ++tick)
            f.editor.tickEdgeScrollForTest();
        EXPECT_TRUE(samePoints(f.editor.getBendPreviewPointsForTest(), drawn)) << (toStart ? "start" : "end");
        EXPECT_TRUE(f.editor.getPointBubbleForTest().isShown());
        f.release(at, from);
    }
    synth::ui::setReducedMotionForTest(std::nullopt);
}

// asymmetric-gesture-zone: the step is at the start row whichever way the segment runs, so a falling segment dragged UP
// to its high start holds exactly as a rising one dragged down does, and neither holds toward its end.
TEST(AutomationLaneBendDrag, AFallingSegmentHoldsAtItsHighStartAndANearMissStaysACurve) {
    BendLane f(80.0, 20.0);
    const auto from = f.midPoint();
    ASSERT_LT(f.startY(), f.endY()) << "a falling segment: the start is the upper dot";
    f.press(from);
    const auto near = f.walk(from, f.rowAlong(3.0), from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    const auto deep = f.walk(near, f.rowAlong(-1.0), from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest());
    f.release(deep, from);
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_NEAR(f.midValue(), 80.0, 1e-9) << "a hold plays the start value";

    BendLane g(80.0, 20.0);
    const auto start = g.midPoint();
    const juce::Point<float> low(start.x, start.y + 500.0f);
    g.press(start);
    g.dragTo(low, start);
    EXPECT_FALSE(g.editor.isBendPreviewHoldForTest()) << "toward the end value it is a jump, never a hold";
    g.release(low, start);
    EXPECT_EQ(g.first().curve, kJump);
}

// Real events: a falling segment from the lane's top value bends into a hold with the pointer inside the lane.
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

// Regression test for FRO2292: dragged to the end row the segment became a curve at tension -3 (x^(1/64)), never the
// step. Now, on both ends of rising and falling segments, the last drawn frame on the exact end row is the step (Hold
// on the start row, Jump on the end row), the audio kernel plays the committed points exactly as the preview drew them,
// and Cmd+Z restores the line.
TEST(AutomationLaneBendDrag, OnEitherEndRowTheDrawnStepIsWhatPlaysAndUndoRestoresTheLine) {
    for (const bool falling : {true, false})
        for (const bool toStart : {true, false}) {
            BendLane f(falling ? 80.0 : 20.0, falling ? 20.0 : 80.0);
            const auto from = f.pointOnLine(0.3);
            const auto original = f.points();
            const std::string where = std::string(falling ? "falling " : "rising ") + (toStart ? "start" : "end");
            f.press(from);
            const auto at = f.walkToStep(from, toStart);
            const auto drawn = f.editor.getBendPreviewPointsForTest();
            EXPECT_EQ(drawn[0].curve, toStart ? kHold : kJump) << where;
            EXPECT_EQ(f.editor.getPointBubbleForTest().getText(), toStart ? "Hold" : "Jump") << where;
            f.release(at, from);
            EXPECT_TRUE(samePoints(f.points(), drawn)) << where;
            for (const double beat : {0.01, 1.0, 2.0, 3.99})
                EXPECT_NEAR(f.valueAtBeat(beat), toStart ? f.first().value : f.points()[1].value, 1e-9)
                    << where << " beat " << beat;
            f.undo.undo();
            EXPECT_TRUE(samePoints(f.points(), original)) << where << ": Cmd+Z restores the line";
        }
}

// A hand resting on an end row trembles. 30 real drag events of +-2 px around each end row keep the step and its
// bubble the whole time (no flip of either), and +-3 px never shows the other end's step; every frame's preview is
// what a release there commits.
TEST(AutomationLaneBendDrag, JitterOnEitherEndRowNeverFlipsTheStepOrItsBubble) {
    for (const bool toStart : {true, false})
        for (const float amp : {2.0f, 3.0f}) {
            BendLane f(80.0, 20.0);
            const auto from = f.pointOnLine(0.5);
            const std::string where = std::string(toStart ? "start" : "end") + " amp " + std::to_string(amp);
            f.press(from);
            auto at = f.walkToStep(from, toStart); // the hand rests on the row where the step shows
            const float row = at.y;
            int flips = 0;
            bool wasStep = true;
            for (int i = 0; i < 30; ++i) {
                at = {at.x, row + (i % 2 == 0 ? amp : -amp)};
                f.dragTo(at, from);
                const bool hold = f.editor.isBendPreviewHoldForTest();
                const bool jump = f.editor.isBendPreviewJumpForTest();
                EXPECT_FALSE(toStart ? jump : hold) << where << " event " << i << ": the other end's step";
                const bool step = toStart ? hold : jump;
                EXPECT_EQ(f.editor.getPointBubbleForTest().isShown(), step) << where << " event " << i;
                flips += step != wasStep ? 1 : 0;
                wasStep = step;
            }
            if (amp <= (float)synth::ui::kHoldExitPx)
                EXPECT_EQ(flips, 0) << where;
            const auto drawn = f.editor.getBendPreviewPointsForTest();
            f.release(at, from);
            EXPECT_TRUE(samePoints(f.points(), drawn)) << where;
        }
}

// No tooltip while a bend drag runs: a pointer resting mid-drag is part of the gesture.
TEST(AutomationLaneBendDrag, NoTooltipWhileDragging) {
    BendLane f(20.0, 80.0);
    EXPECT_TRUE(f.editor.getTooltip().isNotEmpty());
    const auto from = f.midPoint();
    f.press(from);
    f.dragTo({from.x, from.y - 6.0f}, from);
    EXPECT_TRUE(f.editor.getTooltip().isEmpty());
    f.release({from.x, from.y - 6.0f}, from);
    EXPECT_TRUE(f.editor.getTooltip().isNotEmpty());
}
