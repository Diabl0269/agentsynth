// AutomationLanesEdgeScrollTests.cpp -- a lane drag follows the view: a dot, a selection of dots, the stretch box or a
// selection box held in the edge zone scrolls the shared timeline view and stays under the still pointer, the view
// stops at beat 0, a held Alt+arrow keeps the moving dot in view, follow-playhead turns off as for a manual scroll, the
// scroll stops on release, Escape and a tool change, and the whole move is still one undo step. Real events on the real
// panel's editor.

#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "../../Layout/ViewportDragFollow.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LanePointStretch.h"
#include <algorithm>
#include <cmath>

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::StretchHandle;

namespace {

// An open lane (snap a quarter note, one beat; 40 px per beat) with a Pointer editor whose held keys are the test's.
struct EdgeLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;
    key_glide_test::FakeKeys keys;

    EdgeLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        editor->getKeyHoldForTest().host = keys.host();
    }

    synth::ui::TimelineViewState& view() { return panel.getViewState(); }
    double first() { return view().firstVisibleBeat; }
    double visibleBeats() { return (double)editor->getWidth() / view().pixelsPerBeat; }
    juce::Point<float> at(double beat, double value) {
        return {(float)view().beatToX(beat), (float)editor->valueToY(value)};
    }
    // A pointer two pixels in from the right edge, or the left: deep in the edge zone.
    juce::Point<float> rightEdge(double value) {
        return {(float)editor->getWidth() - 2.0f, (float)editor->valueToY(value)};
    }
    juce::Point<float> leftEdge(double value) { return {2.0f, (float)editor->valueToY(value)}; }

    using Snap = synth::ui::TimelineViewState::Snap;
    // The pointer `dx` pixels in from the right edge (negative: past it); `leftDx` likewise for the left edge.
    juce::Point<float> fromRight(float dx, double value) {
        return {(float)editor->getWidth() - dx, (float)editor->valueToY(value)};
    }
    // The lane's x of a beat sits inside [0, width].
    bool inLane(double beat) {
        const double x = view().beatToX(beat);
        return x >= -1e-6 && x <= (double)editor->getWidth() + 1e-6;
    }

    void press(juce::Point<float> p) { editor->mouseDown(makeClickEvent(*editor, p, leftButton())); }
    void dragTo(juce::Point<float> p, juce::Point<float> from) {
        editor->mouseDrag(makeDragEvent(*editor, p, from, leftButton()));
    }
    void release(juce::Point<float> p, juce::Point<float> from) {
        editor->mouseUp(makeDragEvent(*editor, p, from, leftButton()));
    }
    // Ticks only while the timer runs, as the real timer does.
    void ticks(int n) {
        for (int i = 0; i < n && editor->isEdgeScrollRunningForTest(); ++i)
            editor->tickEdgeScrollForTest();
    }
    const std::vector<synth::ui::LaneBreakpoint>& points() const { return doc.getLane(lane)->points; }
};

} // namespace

// Regression test for FRO1333: a dot dragged to the edge of the lane stopped there; the view never scrolled.
TEST(AutomationLanesEdgeScroll, ADotHeldAtTheRightEdgeScrollsTheViewAndStaysUnderThePointer) {
    EdgeLane f;
    f.doc.addBreakpoint(f.lane, 2.0, 50.0);
    f.undo.beginNewTransaction();
    const auto rev = f.doc.getRevision();
    const auto from = f.at(2.0, 50.0);
    f.press(from);
    f.dragTo(f.rightEdge(50.0), from);
    ASSERT_TRUE(f.editor->isEdgeScrollRunningForTest()) << "the pointer in the edge zone starts the scroll";

    const double firstBefore = f.first();
    const double beatBefore = f.editor->getPreviewBeatForTest();
    f.ticks(30);
    const double scrolled = f.first() - firstBefore;
    EXPECT_GT(scrolled, 5.0) << "a second at the edge scrolls several beats";
    const double followed = f.editor->getPreviewBeatForTest() - beatBefore;
    EXPECT_NEAR(followed, scrolled, 1.0) << "the dot moves with the view, within one grid step";
    const double under = f.view().xToBeat(f.rightEdge(50.0).x);
    // The dot is on a grid line within one step of the pointer: the nearest one, or the last one that keeps its whole
    // handle inside the lane when the nearest lies past the visible end (the view moves in whole pixels, so which of
    // the two holds depends on where the last frame left it).
    const double dotBeat = f.editor->getPreviewBeatForTest();
    // On the grid, or held on the edge (pointer at the dot's limit) while the view pans.
    EXPECT_TRUE(dotBeat == std::round(dotBeat) || f.view().beatToX(dotBeat) > (double)f.editor->getWidth() - 20.0)
        << "on the grid or on the edge: " << dotBeat;
    EXPECT_NEAR(dotBeat, under, 1.0 - 1e-9) << "the dot is still under the pointer";
    EXPECT_LE(dotBeat, std::round(under)) << "and never further out than the nearest grid line";

    // Nearer the edge is faster: a pointer just inside the zone crawls.
    const auto crawlAt = juce::Point<float>((float)f.editor->getWidth() - 20.0f, f.rightEdge(50.0).y);
    f.dragTo(crawlAt, from);
    const double crawlFrom = f.first();
    f.ticks(30);
    EXPECT_LT(f.first() - crawlFrom, scrolled / 3.0) << "the scroll is slower further from the edge";

    const auto middle = juce::Point<float>((float)f.editor->getWidth() / 2.0f, f.rightEdge(50.0).y);
    f.dragTo(middle, from);
    f.ticks(4); // the speed eases out over a few frames...
    EXPECT_FALSE(f.editor->isEdgeScrollRunningForTest()) << "...and back in the middle the scroll stops";
    const double rested = f.first();
    f.editor->tickEdgeScrollForTest();
    EXPECT_EQ(f.first(), rested) << "and the view stays still";

    f.dragTo(f.rightEdge(50.0), from);
    f.ticks(3);
    const double landed = f.editor->getPreviewBeatForTest();
    f.release(f.rightEdge(50.0), from);
    EXPECT_FALSE(f.editor->isEdgeScrollRunningForTest()) << "release stops the scroll";
    ASSERT_EQ(f.points().size(), 1u);
    EXPECT_EQ(f.points()[0].beat, f.view().floorToGrid(landed, 4.0))
        << "the drop is the last grid line inside the view, at or before the beat the pinned dot showed (FRO2084)";
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one edit";
    f.undo.undo();
    EXPECT_EQ(f.points()[0].beat, 2.0) << "one undo puts it back";

    // A dot pressed with the pointer already inside the right edge zone stays put until the pointer moves toward it.
    EdgeLane still;
    // The dot sits inside the zone, so the press itself lands there.
    const double zoneBeat = (double)(still.editor->getWidth() - 12) / still.view().pixelsPerBeat;
    still.doc.addBreakpoint(still.lane, zoneBeat, 50.0);
    const auto start = still.at(zoneBeat, 50.0);
    const auto inZone = start;
    viewport_drag_follow::expectPressInZoneHoldsStill({[&] {
                                                           still.press(start);
                                                           still.dragTo(inZone, start);
                                                       },
                                                       [&] {
                                                           still.ticks(1);
                                                           still.dragTo(inZone, start);
                                                       },
                                                       [&] { still.dragTo(still.fromRight(6.0f, 50.0), start); },
                                                       [&] { return still.first(); },
                                                       [&] { still.release(still.fromRight(6.0f, 50.0), start); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the view scrolling, the dot in view.
    std::unique_ptr<EdgeLane> past;
    juce::Point<float> pastFrom;
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             past = std::make_unique<EdgeLane>();
             past->doc.addBreakpoint(past->lane, 2.0, 50.0);
             pastFrom = past->at(2.0, 50.0);
             past->press(pastFrom);
         },
         [&](float px) { past->dragTo(past->fromRight(-px, 50.0), pastFrom); }, [&] { past->ticks(1); },
         [&] { return past->first(); },
         [&] { return past->view().beatToX(past->editor->getPreviewBeatForTest()) < (double)past->editor->getWidth(); },
         [&] { past->release(past->fromRight(-40.0f, 50.0), pastFrom); }});
}

// Regression test for FRO1739: after a long edge drag of a lane dot the release looked like a jump of the view and
// the dot. A release does not jump on screen: the view stays where the last tick left it and the drawn dot starts
// where the last frame showed it, then glides to the grid line it lands on (the drop is on a line with snap
// on), also when the up event lands a few pixels from the last drag event.
TEST(AutomationLanesEdgeScroll, ReleasingAfterALongEdgeHoldLeavesTheViewAndTheDrawnDotWhereTheLastFrameShowedThem) {
    for (const float upOffset : {0.0f, -3.0f}) {
        EdgeLane f;
        f.doc.addBreakpoint(f.lane, 2.0, 50.0);
        f.undo.beginNewTransaction();
        f.editor->forceGlideAnimateForTest(true);
        const auto from = f.at(2.0, 50.0);
        f.press(from);
        f.dragTo(f.rightEdge(50.0), from);
        f.ticks(45);
        ASSERT_GT(f.first(), 10.0) << "a long hold scrolled well past the first view";
        const double firstBefore = f.first();
        const double landed = f.editor->getPreviewBeatForTest();
        const auto drawn = f.editor->getGrabbedPointDrawnAtForTest();
        ASSERT_TRUE(drawn.has_value());

        auto up = f.rightEdge(50.0);
        up.x += upOffset;
        f.release(up, from);
        EXPECT_DOUBLE_EQ(f.first(), firstBefore) << "the release does not move the view";
        ASSERT_EQ(f.points().size(), 1u);
        const double target = f.view().floorToGrid(landed, 4.0);
        EXPECT_EQ(f.points()[0].beat, target)
            << "the drop is the last grid line at or before the beat the last frame showed";
        auto& tween = f.editor->getPointTweenForTest();
        tween.stepForTest(0.0);
        const auto first = f.editor->getDrawnSelectionBoundsForTest().getCentre();
        EXPECT_NEAR(first.x, drawn->x, 0.5) << "the dot starts the glide where the last frame drew it";
        EXPECT_NEAR(first.y, drawn->y, 0.5);
        float lastX = first.x;
        for (double ms = 16.0; ms <= 400.0; ms += 16.0) {
            tween.stepForTest(ms);
            const float x = f.editor->getDrawnSelectionBoundsForTest().getCentre().x;
            EXPECT_LE(x, lastX + 0.5f) << "it glides toward its line, never away " << ms << " ms after the release";
            lastX = x;
        }
        EXPECT_NEAR(lastX, f.view().beatToX(target), 0.5) << "and rests on the line";
        f.editor->forceGlideAnimateForTest(false);
    }
}

TEST(AutomationLanesEdgeScroll, TheLeftEdgeScrollsBackAndStopsAtBeatZero) {
    EdgeLane f;
    f.view().firstVisibleBeat = 8.0;
    f.doc.addBreakpoint(f.lane, 12.0, 50.0);
    const auto from = f.at(12.0, 50.0);
    f.press(from);
    f.dragTo(f.leftEdge(50.0), from);
    f.ticks(10);
    EXPECT_LT(f.first(), 8.0) << "the view scrolls back";
    f.ticks(200);
    EXPECT_EQ(f.first(), 0.0) << "and stops at beat 0";
    EXPECT_GE(f.editor->getPreviewBeatForTest(), 0.0);
    EXPECT_NEAR(f.editor->getPreviewBeatForTest(), 0.0, 1e-9) << "the dot lands at the start, under the pointer";
    f.release(f.leftEdge(50.0), from);
}

TEST(AutomationLanesEdgeScroll, ThreeSelectedDotsAndTheStretchBoxFollowTheView) {
    {
        SCOPED_TRACE("three dots moved together");
        EdgeLane f;
        for (const double b : {1.0, 2.0, 3.0})
            f.doc.addBreakpoint(f.lane, b, 50.0);
        f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
        const auto from = f.at(3.0, 50.0);
        f.press(from);
        f.dragTo(f.rightEdge(50.0), from);
        const auto before = f.editor->getDragMovedPointsForTest();
        f.ticks(20);
        const auto after = f.editor->getDragMovedPointsForTest();
        ASSERT_EQ(before.size(), 3u);
        ASSERT_EQ(after.size(), 3u);
        EXPECT_GT(after[2].beat - before[2].beat, 3.0) << "the selection moves on with the view";
        for (std::size_t i = 0; i < 3; ++i)
            EXPECT_NEAR(after[i].beat - before[i].beat, after[2].beat - before[2].beat, 1e-9) << "as one block";
        f.release(f.rightEdge(50.0), from);
        EXPECT_EQ(f.points().size(), 3u);
    }
    {
        SCOPED_TRACE("the stretch box's right edge");
        EdgeLane f;
        for (const double b : {1.0, 2.0, 3.0})
            f.doc.addBreakpoint(f.lane, b, 50.0);
        f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
        f.editor->resized();
        const auto from = f.editor->getStretchHandleRectForTest(StretchHandle::Right).getCentre();
        ASSERT_FALSE(f.editor->getStretchHandleRectForTest(StretchHandle::Right).isEmpty());
        f.undo.beginNewTransaction();
        const auto rev = f.doc.getRevision();
        f.press(from);
        f.dragTo(f.rightEdge(50.0), from);
        ASSERT_TRUE(f.editor->isEdgeScrollRunningForTest());
        const double endBefore = f.editor->getStretchForTest().result().moved.back().beat;
        f.ticks(20);
        EXPECT_GT(f.editor->getStretchForTest().result().moved.back().beat, endBefore + 3.0)
            << "the stretched end follows the view";
        f.release(f.rightEdge(50.0), from);
        EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one edit";
        EXPECT_GT(f.points().back().beat, 6.0);
    }
}

TEST(AutomationLanesEdgeScroll, AHeldAltRightNearTheRightEdgeKeepsTheMovingDotInView) {
    EdgeLane f;
    const double start = std::floor(f.visibleBeats()) - 2.0;
    f.doc.addBreakpoint(f.lane, start, 50.0);
    f.editor->getPointSelection().setSelection({start});
    f.undo.beginNewTransaction();
    const auto rev = f.doc.getRevision();

    f.keys.down = {juce::KeyPress::rightKey};
    f.keys.alt = true;
    ASSERT_TRUE(f.editor->keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0)));
    for (double ms = 0.0; ms <= 4000.0; ms += 16.0) {
        f.keys.now = 1000.0 + synth::ui::lane_key::kTapMs + ms;
        f.editor->getKeyHoldForTest().tick();
        const double beat = f.editor->getPreviewBeatForTest();
        ASSERT_GE(beat, f.first()) << "at " << ms << " ms";
        ASSERT_LE(beat, f.first() + f.visibleBeats()) << "the moving dot is in view at " << ms << " ms";
    }
    EXPECT_GT(f.first(), 0.0) << "the view scrolled along";
    EXPECT_FALSE(f.editor->isEdgeScrollRunningForTest()) << "a key hold scrolls by itself, not with the mouse timer";
    f.keys.down.clear();
    f.editor->getKeyHoldForTest().checkKeyUp();
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one edit";
    f.undo.undo();
    EXPECT_EQ(f.points()[0].beat, start) << "one undo puts it back";
}

// A held Alt+Right across the right edge with the dots' glide running: the view never jumps (each frame it moves by
// no more than one grid step) and the DRAWN dot, not the preview, is what stays inside the margin band.
TEST(AutomationLanesEdgeScroll, AHeldAltRightAcrossTheRightEdgeScrollsTheViewSmoothlyAndKeepsTheDrawnDotInTheMargin) {
    EdgeLane f;
    const double start = std::floor(f.visibleBeats()) - 2.0;
    f.doc.addBreakpoint(f.lane, start, 50.0);
    f.editor->getPointSelection().setSelection({start});
    f.editor->forceGlideAnimateForTest(true);
    auto& tween = f.editor->getPointTweenForTest();
    const double stepBeats = 1.0;
    const double width = (double)f.editor->getWidth();

    f.keys.down = {juce::KeyPress::rightKey};
    f.keys.alt = true;
    ASSERT_TRUE(f.editor->keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0)));
    double easeMs = 0.0;
    double lastBeat = f.editor->getPreviewBeatForTest();
    double lastFirst = f.first();
    double lastDrawnX = -1.0;
    for (double ms = 0.0; ms <= 5000.0; ms += 16.0) {
        f.keys.now = 1000.0 + synth::ui::lane_key::kTapMs + ms;
        f.editor->getKeyHoldForTest().tick();
        if (f.editor->getPreviewBeatForTest() != lastBeat) {
            lastBeat = f.editor->getPreviewBeatForTest();
            easeMs = 0.0;
        }
        easeMs += 16.0;
        tween.stepForTest(easeMs);
        EXPECT_LE(std::abs(f.first() - lastFirst), stepBeats) << "the view did not jump at " << ms << " ms";
        lastFirst = f.first();
        const double drawnX =
            f.view().beatToX(lastBeat) + (double)tween.offsetFor(synth::ui::lane_key::tweenKey(start)).x;
        EXPECT_LE(drawnX, 0.9 * width + 0.5) << "the drawn dot is not past the margin line at " << ms << " ms";
        EXPECT_GE(drawnX, 0.0) << "or off the left at " << ms << " ms";
        if (lastDrawnX >= 0.0)
            EXPECT_LE(std::abs(drawnX - lastDrawnX), stepBeats * f.view().pixelsPerBeat)
                << "the dot never lurches on screen at " << ms << " ms";
        lastDrawnX = drawnX;
    }
    EXPECT_GT(f.first(), 0.0) << "the view scrolled along";
    f.keys.down.clear();
    f.editor->getKeyHoldForTest().checkKeyUp();
    f.editor->forceGlideAnimateForTest(false);
}

TEST(AutomationLanesEdgeScroll, ADragThatScrollsTheViewTurnsFollowPlayheadOff) {
    EdgeLane f;
    f.panel.setFollowPlayheadEnabled(true);
    ASSERT_TRUE(f.panel.isFollowPlayheadEnabled());
    f.doc.addBreakpoint(f.lane, 2.0, 50.0);
    const auto from = f.at(2.0, 50.0);
    f.press(from);
    f.dragTo(f.rightEdge(50.0), from);
    f.ticks(1);
    EXPECT_FALSE(f.panel.isFollowPlayheadEnabled())
        << "taking the view by hand stops following, as a wheel scroll does";
    f.release(f.rightEdge(50.0), from);
}

TEST(AutomationLanesEdgeScroll, EscapeAndAToolChangeStopTheScroll) {
    {
        EdgeLane f;
        f.doc.addBreakpoint(f.lane, 2.0, 50.0);
        const auto rev = f.doc.getRevision();
        const auto from = f.at(2.0, 50.0);
        f.press(from);
        f.dragTo(f.rightEdge(50.0), from);
        ASSERT_TRUE(f.editor->isEdgeScrollRunningForTest());
        EXPECT_TRUE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        EXPECT_FALSE(f.editor->isEdgeScrollRunningForTest()) << "Escape stops the scroll";
        f.release(f.rightEdge(50.0), from);
        EXPECT_EQ(f.doc.getRevision(), rev) << "and the cancelled drag writes nothing";
    }
    {
        EdgeLane f;
        f.doc.addBreakpoint(f.lane, 2.0, 50.0);
        const auto from = f.at(2.0, 50.0);
        f.press(from);
        f.dragTo(f.rightEdge(50.0), from);
        ASSERT_TRUE(f.editor->isEdgeScrollRunningForTest());
        f.editor->setTool(AutomationLaneEditor::Tool::Pencil);
        EXPECT_FALSE(f.editor->isEdgeScrollRunningForTest()) << "a tool change stops the scroll";
    }
}

// Regression test for FRO1688: a selection box held at the edge never scrolled the view, and its pressed corner was a
// pixel, so a view scrolled under it would have slid the corner off the dots it was pressed beside.
TEST(AutomationLanesEdgeScroll, ASelectionBoxHeldAtTheEdgeScrollsAndKeepsItsCornerOnItsBeat) {
    EdgeLane f;
    for (int b = 3; b <= 60; b += 3)
        f.doc.addBreakpoint(f.lane, (double)b, 50.0);
    const auto rev = f.doc.getRevision();
    const auto from = f.at(2.5, 80.0);
    f.press(from);
    f.dragTo(f.rightEdge(20.0), from);
    ASSERT_TRUE(f.editor->getPointSelection().isBoxActive());
    ASSERT_TRUE(f.editor->isEdgeScrollRunningForTest()) << "the box in the edge zone starts the scroll";
    const double lastBefore = f.view().xToBeat(f.rightEdge(20.0).x);

    f.ticks(30);
    EXPECT_GT(f.first(), 5.0) << "the view scrolled under the box";
    const double lastAfter = f.view().xToBeat(f.rightEdge(20.0).x);
    const auto picked = f.editor->getPointSelection().getSelected();
    ASSERT_FALSE(picked.empty());
    EXPECT_EQ(picked.front(), 3.0) << "the corner stayed at beat 2.5, so the first dot is still inside";
    EXPECT_GT(picked.back(), lastBefore) << "dots that scrolled in are selected";
    EXPECT_LE(picked.back(), lastAfter);
    EXPECT_EQ(f.editor->getPointSelection().boxAnchor().x, (int)std::lround(f.view().beatToX(2.5)))
        << "the box's corner moved with its beat";

    EXPECT_TRUE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.editor->isEdgeScrollRunningForTest()) << "Escape stops the scroll with the box";
    EXPECT_EQ(f.doc.getRevision(), rev) << "a box selects; it never edits the lane";
}

namespace {
struct HeldPointer {
    const char* name;
    float rightIn; // pixels in from the right edge; negative is past it
};
constexpr HeldPointer kRightPointers[] = {
    {"in the zone", 20.0f}, {"on the edge", 0.0f}, {"200 px past the edge", -200.0f}};
} // namespace

// Regression test for FRO1779: a lane dot dragged to the edge and past it was drawn outside the visible area and was
// dropped there; with a coarse snap the nearest grid line sat up to half a bar past the edge even inside it.
TEST(AutomationLanesEdgeScroll, ADotHeldAtEitherEdgeStaysInsideTheView) {
    using Snap = EdgeLane::Snap;
    for (const Snap snap : {Snap::Bar, Snap::Quarter, Snap::Off})
        for (const auto& held : kRightPointers) {
            SCOPED_TRACE(std::string(held.name) + ", snap " + std::to_string((int)snap));
            EdgeLane f;
            f.view().snap = snap;
            f.doc.addBreakpoint(f.lane, 2.0, 50.0);
            f.undo.beginNewTransaction();
            const auto rev = f.doc.getRevision();
            const auto from = f.at(2.0, 50.0);
            const auto to = f.fromRight(held.rightIn, 50.0);
            f.press(from);
            f.dragTo(to, from);
            EXPECT_TRUE(f.inLane(f.editor->getPreviewBeatForTest())) << "after the drag";
            for (int tick = 0; tick < 12; ++tick) {
                f.ticks(1);
                f.dragTo(to, from);
                ASSERT_TRUE(f.inLane(f.editor->getPreviewBeatForTest()))
                    << "tick " << tick << ": the dot is at x " << f.view().beatToX(f.editor->getPreviewBeatForTest());
            }
            const double firstBefore = f.first();
            f.release(to, from);
            EXPECT_DOUBLE_EQ(f.first(), firstBefore) << "the release does not move the view";
            ASSERT_EQ(f.points().size(), 1u);
            EXPECT_TRUE(f.inLane(f.points()[0].beat)) << "dropped at beat " << f.points()[0].beat;
            EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one undo step";
            f.undo.undo();
            EXPECT_EQ(f.points()[0].beat, 2.0) << "undo puts it back";
        }

    // The left edge: the view stops at beat 0 and the dot with it.
    for (const Snap snap : {Snap::Bar, Snap::Quarter, Snap::Off})
        for (const float past : {20.0f, 0.0f, -200.0f}) {
            SCOPED_TRACE("left, " + std::to_string(past) + " px in, snap " + std::to_string((int)snap));
            EdgeLane f;
            f.view().snap = snap;
            f.view().firstVisibleBeat = 8.0;
            f.doc.addBreakpoint(f.lane, 12.0, 50.0);
            f.undo.beginNewTransaction();
            const auto from = f.at(12.0, 50.0);
            const juce::Point<float> to(past, (float)f.editor->valueToY(50.0));
            f.press(from);
            f.dragTo(to, from);
            for (int tick = 0; tick < 60; ++tick) {
                f.ticks(1);
                f.dragTo(to, from);
                ASSERT_TRUE(f.inLane(f.editor->getPreviewBeatForTest())) << "tick " << tick;
                ASSERT_GE(f.editor->getPreviewBeatForTest(), 0.0);
            }
            f.release(to, from);
            ASSERT_EQ(f.points().size(), 1u);
            EXPECT_TRUE(f.inLane(f.points()[0].beat)) << "dropped at beat " << f.points()[0].beat;
            EXPECT_GE(f.points()[0].beat, 0.0);
            f.undo.undo();
            EXPECT_EQ(f.points()[0].beat, 12.0);
        }
}

// Regression test for FRO1779: a selection dragged past the edge keeps its outermost dot inside the view.
TEST(AutomationLanesEdgeScroll, ADraggedSelectionKeepsItsOutermostDotsInsideTheView) {
    for (const auto snap : {EdgeLane::Snap::Bar, EdgeLane::Snap::Off}) {
        EdgeLane f;
        f.view().snap = snap;
        for (const double b : {1.0, 2.0, 3.0})
            f.doc.addBreakpoint(f.lane, b, 50.0);
        f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
        const auto from = f.at(2.0, 50.0);
        const auto to = f.fromRight(-200.0f, 50.0);
        f.press(from);
        f.dragTo(to, from);
        for (int tick = 0; tick < 12; ++tick) {
            f.ticks(1);
            f.dragTo(to, from);
            const auto moved = f.editor->getDragMovedPointsForTest();
            ASSERT_EQ(moved.size(), 3u);
            ASSERT_TRUE(f.inLane(moved.back().beat)) << "tick " << tick << ": the rightmost dot left the view";
        }
        f.release(to, from);
        ASSERT_EQ(f.points().size(), 3u);
        EXPECT_TRUE(f.inLane(f.points().back().beat));
    }
}

// Regression test for FRO1779: the stretch box's right edge dragged past the edge stays inside the view.
TEST(AutomationLanesEdgeScroll, TheStretchBoxRightEdgeDraggedPastTheEdgeStaysInsideTheView) {
    EdgeLane f;
    f.view().snap = EdgeLane::Snap::Bar;
    for (const double b : {1.0, 2.0, 3.0})
        f.doc.addBreakpoint(f.lane, b, 50.0);
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    f.editor->resized();
    const auto from = f.editor->getStretchHandleRectForTest(StretchHandle::Right).getCentre();
    const auto to = f.fromRight(-200.0f, 50.0);
    f.press(from);
    f.dragTo(to, from);
    for (int tick = 0; tick < 12; ++tick) {
        f.ticks(1);
        f.dragTo(to, from);
        ASSERT_TRUE(f.inLane(f.editor->getStretchForTest().result().moved.back().beat)) << "tick " << tick;
    }
    f.release(to, from);
    EXPECT_TRUE(f.inLane(f.points().back().beat));
}

// Guards the frames, not only the dropped state (drag-leaves-view kind: the edge clamp was once checked only on
// release, not while the view pans): a dot held past an edge sat on the last grid line inside, which slides away with
// the view and jumped back to the edge each time the next line arrived. It must be held on the edge x on every pan
// frame (exactly, no one-pixel blip as a bar line passes), and the drop must be the last drawn beat (with snap on, the
// last grid line inside).
TEST(AutomationLanesEdgeScroll, ADotHeldPastTheEdgeStaysOnTheEdgeOnEveryPanFrame) {
    using Snap = EdgeLane::Snap;
    for (const Snap snap : {Snap::Bar, Snap::Quarter, Snap::Off}) {
        for (const bool right : {true, false}) {
            SCOPED_TRACE(std::string(right ? "right" : "left") + ", snap " + std::to_string((int)snap));
            EdgeLane f;
            f.view().snap = snap;
            if (!right)
                f.view().firstVisibleBeat = 24.0;
            const double start = right ? 2.0 : 26.0;
            f.doc.addBreakpoint(f.lane, start, 50.0);
            f.undo.beginNewTransaction();
            const auto from = f.at(start, 50.0);
            const auto to =
                right ? f.fromRight(-60.0f, 50.0) : juce::Point<float>(-60.0f, (float)f.editor->valueToY(50.0));
            f.press(from);
            f.dragTo(to, from);
            f.ticks(1);
            const double edgeX = f.view().beatToX(f.editor->getPreviewBeatForTest());
            int frames = 0;
            for (int tick = 0; tick < 70 && f.first() > 0.0 && f.editor->isEdgeScrollRunningForTest(); ++tick) {
                f.ticks(1);
                if (f.first() <= 0.0)
                    break; // the view reached beat 0: the dot may sit on it, as it does at the start
                ++frames;
                const double x = f.view().beatToX(f.editor->getPreviewBeatForTest());
                ASSERT_NEAR(x, edgeX, 1e-6) << "pan frame " << tick << ": the dot left the edge";
            }
            ASSERT_GT(frames, 5) << "the view panned";
            const double shown = f.editor->getPreviewBeatForTest();
            f.release(to, from);
            ASSERT_EQ(f.points().size(), 1u);
            // Off: the last drawn beat. On: the last grid line inside the view, never the pinned edge beat.
            const double expected = snap == Snap::Off ? shown
                                    : right           ? f.view().floorToGrid(shown, 4.0)
                                                      : f.view().ceilToGrid(shown, 4.0);
            EXPECT_EQ(f.points()[0].beat, expected) << "the drop is the last drawn beat, on a grid line with snap on";
        }
    }
}
