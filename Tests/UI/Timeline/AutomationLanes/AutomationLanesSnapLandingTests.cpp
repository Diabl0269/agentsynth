// AutomationLanesSnapLandingTests.cpp -- a lane dot dropped with Snap on lands exactly on a grid line, wherever the
// pointer is let go: in the middle of the lane, onto a neighbour, held at either edge of the view, and past it, on
// every division. The sweep is of the RELEASE (the committed beat), not only the live drag, and each drop is one undo
// step. Real events on the real panel's editor (the guard of the snap-misses-line kind for lane dots).

#include "AutomationLanesTestFixture.h"
#include <cmath>
#include <string>
#include <vector>

using namespace automation_lanes_test;
using synth::ui::AutomationLaneEditor;
using Snap = synth::ui::TimelineViewState::Snap;

namespace {

struct DropLane : LanesPanel {
    synth::TrackId track;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    DropLane() {
        track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
    }
    synth::ui::TimelineViewState& view() { return panel.getViewState(); }
    bool onGrid(double beat) { return std::abs(view().snapBeat(beat, 4.0) - beat) <= 1e-9; }
    juce::Point<float> at(double beat, double value) {
        return {(float)view().beatToX(beat), (float)editor->valueToY(value)};
    }
    const std::vector<synth::ui::LaneBreakpoint>& points() const { return doc.getLane(lane)->points; }
    void ticks(int n) {
        for (int i = 0; i < n && editor->isEdgeScrollRunningForTest(); ++i)
            editor->tickEdgeScrollForTest();
    }
};

constexpr Snap kSnaps[] = {Snap::Bar,    Snap::Whole,          Snap::Half,         Snap::Quarter,
                           Snap::Eighth, Snap::QuarterTriplet, Snap::QuarterDotted};

} // namespace

// Regression test for FRO2084: with the grid on a bar, a dot let go between two bars stayed where it was let go.
TEST(AutomationLanesSnapLanding, ADotLetGoAnywhereInTheLaneLandsOnAGridLine) {
    for (const Snap snap : kSnaps)
        for (const bool neighbour : {false, true})
            for (float x = 10.0f; x < 900.0f; x += 29.0f) {
                SCOPED_TRACE("snap " + std::to_string((int)snap) + (neighbour ? ", neighbour" : "") + ", x " +
                             std::to_string(x));
                DropLane f;
                f.view().snap = snap;
                f.doc.addBreakpoint(f.lane, 2.3, 50.0); // off the grid, as a dot drawn with snap off is
                if (neighbour) {
                    f.doc.addBreakpoint(f.lane, f.view().snapBeat(8.0, 4.0), 20.0);
                    f.doc.addBreakpoint(f.lane, f.view().snapBeat(14.0, 4.0), 80.0);
                }
                f.undo.beginNewTransaction();
                const auto rev = f.doc.getRevision();
                const auto size = f.points().size();
                const auto from = f.at(2.3, 50.0);
                const juce::Point<float> to(x, from.y + 3.0f);
                f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
                f.editor->mouseDrag(makeDragEvent(*f.editor, (from + to) * 0.5f, from, leftButton()));
                f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
                f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
                ASSERT_EQ(f.points().size(), size) << "a drag never deletes a dot";
                for (const auto& p : f.points())
                    if (p.value > 45.0 && p.value < 55.0) // the dot that was dragged (the neighbours are at 20 and 80)
                        EXPECT_TRUE(f.onGrid(p.beat)) << "the dropped dot is at beat " << p.beat;
                EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one undo step";
                f.undo.undo();
                EXPECT_EQ(f.points().front().beat, 2.3);
            }
}

// Regression test for FRO2084: held at the edge the dot is pinned ON the edge (no grid line), and the release kept it
// there; it lands on the last line inside the view instead and glides there from where it was drawn.
TEST(AutomationLanesSnapLanding, ADotHeldAtTheEdgeAndLetGoLandsOnTheLastGridLineInsideTheView) {
    for (const Snap snap : kSnaps)
        for (const bool right : {true, false}) {
            SCOPED_TRACE("snap " + std::to_string((int)snap) + (right ? ", right edge" : ", left edge"));
            DropLane f;
            f.view().snap = snap;
            const double start = right ? 2.0 : 12.0;
            if (!right)
                f.view().firstVisibleBeat = 8.0;
            f.doc.addBreakpoint(f.lane, start, 50.0);
            f.undo.beginNewTransaction();
            f.editor->forceGlideAnimateForTest(true);
            const auto from = f.at(start, 50.0);
            const juce::Point<float> to(right ? (float)f.editor->getWidth() - 2.0f : 2.0f, from.y);
            f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
            f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
            for (int tick = 0; tick < 25; ++tick) {
                f.ticks(1);
                f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
            }
            const auto drawn = f.editor->getGrabbedPointDrawnAtForTest();
            ASSERT_TRUE(drawn.has_value());
            f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
            ASSERT_EQ(f.points().size(), 1u);
            const double beat = f.points()[0].beat;
            EXPECT_TRUE(f.onGrid(beat)) << "dropped at beat " << beat;
            const double x = f.view().beatToX(beat);
            EXPECT_GE(x, -1e-6);
            EXPECT_LE(x, (double)f.editor->getWidth() + 1e-6) << "and inside the view";
            // The dot starts the glide where it was drawn and ends on the line.
            auto& tween = f.editor->getPointTweenForTest();
            tween.stepForTest(0.0);
            const auto first = f.editor->getDrawnSelectionBoundsForTest().getCentre();
            EXPECT_NEAR(first.x, drawn->x, 0.5) << "the dot starts from where it was drawn";
            tween.completeForTest();
            EXPECT_NEAR(f.editor->getDrawnSelectionBoundsForTest().getCentre().x, f.view().beatToX(beat), 0.5);
            f.editor->forceGlideAnimateForTest(false);
            f.undo.undo();
            EXPECT_EQ(f.points()[0].beat, start);
        }
}
