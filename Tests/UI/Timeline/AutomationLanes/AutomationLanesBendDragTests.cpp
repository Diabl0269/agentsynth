// AutomationLanesBendDragTests.cpp -- a press on the line between two lane points bends it so the curve follows the
// pointer, and dragging all the way toward the start value and 12 px further makes the segment a hold: one undo step,
// the preview drawn through the same points the release commits. Real events on the lane editor.

#include "../TimelinePanel/TimelinePanelTestEvents.h"
#include "AppUndoManager.h"
#include "Timeline/AutomationKernel.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/PointEditing/SegmentBendDrag.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include <cmath>
#include <gtest/gtest.h>

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
    // A row `overshoot` pixels past the tension +1 extreme, toward the start value.
    juce::Point<float> pastExtreme(juce::Point<float> from, double overshoot) const {
        return {from.x, (float)(yForFraction(synth::ui::minMidFraction()) + overshoot)};
    }
};

} // namespace

TEST(AutomationLaneBendDrag, RisingSegmentFollowsThePointerUpAndDown) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const double chordY = f.midY();
    f.press(from);
    f.dragTo({from.x, from.y - 20.0f}, from);
    f.release({from.x, from.y - 20.0f}, from);
    EXPECT_NEAR(f.midY(), chordY - 20.0, 1.0) << "dragged up 20 px: the half-way point went up with it";

    const auto again = f.midPoint();
    f.press(again);
    f.dragTo({again.x, again.y + 40.0f}, again);
    f.release({again.x, again.y + 40.0f}, again);
    EXPECT_NEAR(f.midY(), chordY + 20.0, 1.0) << "down 40 px more: 20 px below the straight line";
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
    f.dragTo({again.x, again.y + 40.0f}, again);
    f.release({again.x, again.y + 40.0f}, again);
    EXPECT_NEAR(f.midY(), chordY + 20.0, 1.0);
}

TEST(AutomationLaneBendDrag, DragToTheExtremeAndTwelvePixelsMoreMakesAHoldInOneStep) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto rev = f.doc.getRevision();
    const auto to = f.pastExtreme(from, synth::ui::kHoldOvershootPx + 1.0);

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

TEST(AutomationLaneBendDrag, JustShortOfTheOvershootStaysACurve) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto to = f.pastExtreme(from, synth::ui::kHoldOvershootPx - 2.0);
    f.press(from);
    f.dragTo(to, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release(to, from);
    EXPECT_EQ(f.first().curve, kLinear);
    EXPECT_NEAR(f.first().tension, 1.0f, 1e-5f);
}

TEST(AutomationLaneBendDrag, BackAboveTheThresholdBeforeReleaseIsNotAHold) {
    BendLane f(20.0, 80.0);
    const auto from = f.midPoint();
    const auto deep = f.pastExtreme(from, synth::ui::kHoldOvershootPx + 6.0);
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

TEST(AutomationLaneBendDrag, AHoldDraggedTowardTheEndByThirteenPixelsBecomesACurve) {
    BendLane f(20.0, 80.0, kHold);
    const juce::Point<float> from((float)f.state.beatToX(2.0), (float)f.startY());
    const auto rev = f.doc.getRevision();

    f.press(from);
    f.dragTo({from.x, from.y - 5.0f}, from);
    EXPECT_TRUE(f.editor.isBendPreviewHoldForTest()) << "a short drag is still a hold";
    f.release({from.x, from.y - 5.0f}, from);
    EXPECT_EQ(f.doc.getRevision(), rev) << "a hold left a hold writes nothing";

    f.press(from);
    f.dragTo({from.x, from.y - 13.0f}, from);
    EXPECT_FALSE(f.editor.isBendPreviewHoldForTest());
    f.release({from.x, from.y - 13.0f}, from);
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
        const auto deep = f.pastExtreme(from, synth::ui::kHoldOvershootPx + 6.0);
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
        g.dragTo(g.pastExtreme(from, synth::ui::kHoldOvershootPx + 6.0), from);
        EXPECT_FALSE(g.editor.isBendBlendRunningForTest()) << "Reduce Motion lands the step at once";
        g.release(from, from);
    }
    synth::ui::setReducedMotionForTest(std::nullopt);
}
