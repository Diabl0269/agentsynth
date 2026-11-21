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
    double overshoot() const { return synth::ui::holdOvershootPx(std::abs(endY() - startY())); }
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

// Regression test for FRO2153: let go anywhere between the lane's top and bottom and the segment keeps the shape that
// was drawn (inside the hold's overshoot, the nearer of the curve and the step), never the shape it had.
TEST(AutomationLaneBendDrag, ReleaseAtAnyRowKeepsTheDrawnShape) {
    for (const bool falling : {true, false})
        for (const double startValue : {0.0, 30.0, 70.0, 100.0})
            for (const double endValue : {0.0, 30.0, 70.0, 100.0}) {
                if (startValue == endValue || (startValue > endValue) != falling)
                    continue;
                for (int row = 0; row < 200; row += 3) {
                    BendLane f(startValue, endValue);
                    const auto from = f.midPoint();
                    f.press(from);
                    const float dir = (float)row > from.y ? 1.0f : -1.0f;
                    juce::Point<float> at = from;
                    for (float y = from.y; dir > 0 ? y <= (float)row : y >= (float)row; y += dir) {
                        at = {from.x, y};
                        f.dragTo(at, from);
                    }
                    const auto drawn = f.editor.getBendPreviewPointsForTest()[0];
                    const float toHold = f.editor.bendToHoldForTest();
                    f.release(at, from);
                    const auto& kept = f.first();
                    const std::string where =
                        std::to_string(startValue) + "->" + std::to_string(endValue) + " row " + std::to_string(row);
                    if (toHold <= 0.0f || toHold >= 1.0f) {
                        EXPECT_EQ(kept.curve, drawn.curve) << where;
                        if (drawn.curve != kHold)
                            EXPECT_NEAR(kept.tension, drawn.tension, 1e-6f) << where;
                    } else {
                        EXPECT_EQ(kept.curve, toHold >= 0.5f ? kHold : kLinear) << where << " toHold " << toHold;
                    }
                    EXPECT_FALSE(kept.curve == kLinear && kept.tension == 0.0f && row != (int)from.y &&
                                 std::abs((float)row - from.y) > 2.0f)
                        << where << ": the release put the straight line back";
                }
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

// Two pixels short of the hold the drag is still a curve, drawn almost flattened into the step; let go there and the
// step, the nearer shape, is what stays.
TEST(AutomationLaneBendDrag, JustShortOfTheOvershootIsStillACurveAndLettingGoKeepsTheNearerStep) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto to = f.pastExtreme(from, f.overshoot() - 2.0);
    f.press(from);
    f.dragTo(to, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    EXPECT_GT(f.editor.bendToHoldForTest(), 0.5f);
    f.release(to, from);
    EXPECT_EQ(f.first().curve, kHold);
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
        EXPECT_FALSE(f.editor.isBendBlendRunningForTest()) << "the committed model holds no blend";
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

// Real events: a falling segment from the lane's top value bends into a hold with the pointer inside the lane
// (its hold zone used to start above the lane), and on the way the drawn segment flattens with every move instead of
// standing still and then flipping.
TEST(AutomationLaneBendDrag, AtTheTopOfTheLaneTheDragGlidesIntoAHoldInsideTheLane) {
    BendLane f(100.0, 0.0);
    ASSERT_LT(f.startY(), 10.0) << "the segment starts at the lane's top value";
    const auto from = f.midPoint();
    const double extremeY = f.yForFraction(synth::ui::minMidFraction());
    f.press(from);
    float lastToHold = 0.0f;
    int flattening = 0;
    for (float y = (float)extremeY; y >= 0.0f; y -= 1.0f) {
        f.dragTo({from.x, y}, from);
        const float toHold = f.editor.bendToHoldForTest();
        EXPECT_GE(toHold, lastToHold) << "row " << y;
        EXPECT_LE(toHold - lastToHold, synth::ui::LaneHoldBlend::kFollowStep) << "row " << y;
        flattening += toHold > lastToHold ? 1 : 0;
        lastToHold = toHold;
    }
    EXPECT_GE(flattening, 4) << "the shape moves across the overshoot, not in one step";
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << "the hold is reached inside the lane";
    f.release({from.x, 0.0f}, from);
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_NEAR(f.midValue(), 100.0, 1e-9);
}

// Let go a third of the way across the overshoot: the curve commits and the drawn flattening returns to it; past
// half-way the step commits and the flattening finishes onto it, so a release never undoes most of what was drawn.
TEST(AutomationLaneBendDrag, LetGoShortOfTheHoldCommitsTheCurveAndTheFlatteningReturns) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    f.press(from);
    // In small steps, so the drawn shape follows the pointer.
    juce::Point<float> at = from;
    for (double past = 0.0; past <= f.overshoot() * 0.35; past += 2.0) {
        at = f.pastExtreme(from, past);
        f.dragTo(at, from);
    }
    const float partWay = f.editor.bendToHoldForTest();
    EXPECT_GT(partWay, 0.15f);
    EXPECT_LT(partWay, 0.5f);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(at, from);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_NEAR(f.first().tension, 1.0f, 1e-5f);
    EXPECT_EQ(f.editor.bendToHoldForTest(), 0.0f) << "off screen the flattening lands on the committed curve at once";
}

TEST(AutomationLaneBendDrag, LetGoPastHalfTheOvershootCommitsTheStep) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto rev = f.doc.getRevision();
    f.press(from);
    juce::Point<float> at = from;
    for (double past = 0.0; past <= f.overshoot() * 0.75; past += 2.0) {
        at = f.pastExtreme(from, past);
        f.dragTo(at, from);
    }
    const float partWay = f.editor.bendToHoldForTest();
    EXPECT_GT(partWay, 0.5f);
    EXPECT_LT(partWay, 1.0f);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(at, from);
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one undo step";
    EXPECT_EQ(f.first().curve, kHold);
    EXPECT_EQ(f.editor.bendToHoldForTest(), 1.0f) << "off screen the flattening lands on the committed step at once";
    f.undo.undo();
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_EQ(f.first().tension, 0.0f);
}
