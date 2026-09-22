// PianoRoll mouse-gesture tests: MARQUEE multi-select from empty grid (plain drag replaces the
// selection, additive modifiers, the deferred plain click that must still just deselect),
// BEAT-ANCHORED drag math / EDGE AUTO-SCROLL / FOLLOW PLAYHEAD, MULTI-NOTE RESIZE (incl. the Cmd
// unquantized resize and the clip-overrun prompt), and CMD+DRAG unsnapped MOVE + the velocity
// scrub's move to Option. Shared PianoRollFixture and mouse-gesture helpers live in
// PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "Timeline/TimelineSnapshot.h"
#include "UI/Timeline/EdgeAutoScroll.h"

// ============================================================================
// 7. Marquee multi-select from empty grid
// ============================================================================
//
// The roll's marquee arms on a PLAIN drag: unlike GraphEditor, where plain drag has to stay free
// for panning (hence Shift there), nothing else in the roll wants that gesture. The modifier
// variants survive with a different job — they no longer ARM the marquee, they make it additive.
//
// Every test here builds its notes BEFORE f.open(), so openClip's pitch centring has already
// settled by the time pointAt() is asked where a row is.

namespace {

// The three-note bed sections 7's marquee tests sweep: two notes close together near the start
// (the marquee's targets) and one far to the right that a sweep must never touch — which is what
// makes "replace" and "additive" tell each other apart.
struct MarqueeBed {
    ClipId clipId;
    NoteId near1, near2, far1;
};

MarqueeBed makeMarqueeBed(PianoRollFixture& f) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    MarqueeBed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.near1 = f.doc.addNote(bed.clipId, makeNote(1.0, 60, 1.0));
    bed.near2 = f.doc.addNote(bed.clipId, makeNote(3.0, 62, 1.0));
    bed.far1 = f.doc.addNote(bed.clipId, makeNote(10.0, 60, 1.0));
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(bed.clipId);
    return bed;
}

// Presses on empty grid at beat 0.2 / four semitones above middle, sweeps right and down past both
// near notes, and releases — the whole gesture, so a caller only states the modifier it used.
void sweepOverNearNotes(PianoRollFixture& f, int extraFlags = 0) {
    const auto anchor = pointAt(f.roll, 0.2, 64);
    const auto to = pointAt(f.roll, 5.0, 58);
    f.roll.mouseDown(leftClick(f.roll, anchor, extraFlags));
    f.roll.mouseDrag(leftDrag(f.roll, to, anchor, extraFlags));
    EXPECT_TRUE(f.roll.isMarqueeActiveForTest()) << "the drag armed a marquee";
    f.roll.mouseUp(leftDrag(f.roll, to, anchor, extraFlags));
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "and the release ended it";
}

} // namespace

TEST(PianoRollMarqueeTest, PlainDragFromEmptyGridSelectsWhatItSweepsAndReplacesTheSelection) {
    PianoRollFixture f;
    const auto bed = makeMarqueeBed(f);
    ASSERT_TRUE(bed.near1.isValid());
    ASSERT_TRUE(bed.near2.isValid());
    ASSERT_TRUE(bed.far1.isValid());

    // A pre-existing selection OUTSIDE the swept band is what proves "replace": if the plain drag
    // were additive, far1 would still be selected afterwards.
    f.roll.getSelectionForTest().setSelection({bed.far1});

    sweepOverNearNotes(f);

    const auto& sel = f.roll.getSelectionForTest();
    EXPECT_EQ(sel.size(), 2);
    EXPECT_TRUE(sel.contains(bed.near1));
    EXPECT_TRUE(sel.contains(bed.near2));
    EXPECT_FALSE(sel.contains(bed.far1)) << "a plain marquee REPLACES the selection";
    EXPECT_FALSE(f.undo.canUndo()) << "selecting is never a document edit";
}

TEST(PianoRollMarqueeTest, ShiftAndCommandDragsStayAdditive) {
    for (const int modifier : {(int)juce::ModifierKeys::shiftModifier, (int)juce::ModifierKeys::commandModifier,
                               (int)(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier)}) {
        PianoRollFixture f;
        const auto bed = makeMarqueeBed(f);
        f.roll.getSelectionForTest().setSelection({bed.far1});

        sweepOverNearNotes(f, modifier);

        const auto& sel = f.roll.getSelectionForTest();
        EXPECT_EQ(sel.size(), 3) << "modifier " << modifier;
        EXPECT_TRUE(sel.contains(bed.near1));
        EXPECT_TRUE(sel.contains(bed.near2));
        EXPECT_TRUE(sel.contains(bed.far1)) << "a modifier marquee ADDS to the existing selection";
        EXPECT_FALSE(f.undo.canUndo());
    }
}

// The deferred-click half of the promotion: a press that never moves must resolve to the plain
// deselect it always was, NOT to a zero-size marquee that clears the selection by sweeping nothing.
// The two outcomes look identical here on purpose — the assertion that separates them is that no
// marquee was ever armed, at mouse-down or at mouse-up.
TEST(PianoRollMarqueeTest, PlainClickOnEmptyGridStillJustDeselects) {
    PianoRollFixture f;
    const auto bed = makeMarqueeBed(f);
    f.roll.getSelectionForTest().setSelection({bed.near1, bed.far1});

    const auto empty = pointAt(f.roll, 6.0, 58);
    f.roll.mouseDown(leftClick(f.roll, empty));
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "mouse-down alone commits to nothing";
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 2) << "and deselects nothing yet either";

    f.roll.mouseUp(leftClick(f.roll, empty));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty()) << "the release resolves it to a deselect";
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest());
    EXPECT_FALSE(f.undo.canUndo()) << "and writes no undo step";
}

// Regression: the marquee only ever arms from EMPTY grid. A plain drag that starts ON a note is
// still a move, which is the gesture the plain-drag marquee could most easily have swallowed.
TEST(PianoRollMarqueeTest, PlainDragStartingOnANoteStillMovesIt) {
    PianoRollFixture f;
    const auto bed = makeMarqueeBed(f);

    const auto anchor = centreOf(f.roll.getNoteRect(bed.near1));
    const juce::Point<float> to(anchor.x + 40.0f, anchor.y); // +1 beat at 40 px/beat
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, to, anchor));
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "a drag from a note is a move, not a sweep";
    f.roll.mouseUp(leftDrag(f.roll, to, anchor));

    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.near1)->startBeat, 2.0);
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(bed.near1));
    EXPECT_TRUE(f.undo.canUndo()) << "the move IS a document edit";
}

// Regression: only the Select tool owns the empty-grid drag. Draw still draws with it.
TEST(PianoRollMarqueeTest, DrawToolsEmptyGridDragStillDrawsANote) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    f.roll.setActiveTool(EditTool::Draw);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 3;
    const auto anchor = pointAt(f.roll, 2.1, pitch);
    const auto to = pointAt(f.roll, 4.0, pitch);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, to, anchor));
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "the pencil never marquees";
    f.roll.mouseUp(leftDrag(f.roll, to, anchor));

    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    const auto& drawn = f.doc.getClip(clipId)->notes[0];
    EXPECT_DOUBLE_EQ(drawn.startBeat, 2.0) << "the pencil FLOORS to the grid cell it points at";
    EXPECT_DOUBLE_EQ(drawn.lengthBeats, 2.0);
    EXPECT_EQ(drawn.pitch, pitch);
}

// ============================================================================
// 16. Beat-anchored drag math, edge auto-scroll, and follow-playhead
// ============================================================================

// ---- Beat-anchored Move drag: xToBeat(currentX) - mouseDownBeat_, not - xToBeat(mouseDownX) ----

TEST(PianoRollDragAnchorTest, MoveDragWithNoScrollLandsExactlyAsBefore) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 32.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2 beats at 40 px/beat

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 4.0)
        << "unchanged from the pixel-anchored form when the view never scrolls mid-drag";
}

TEST(PianoRollDragAnchorTest, MoveDragAbsorbsAMidDragViewScrollIntoTheBeatDelta) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 32.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2 beats' worth of pixels

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));

    // Mid-drag the view scrolls forward by 1 beat (an edge-auto-scroll tick, or in principle
    // anything else) with the pointer never moving on screen at all.
    f.roll.setHorizontalView(f.roll.getPixelsPerBeat(), f.roll.getFirstVisibleBeat() + 1.0);
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    // The pixel offset (+2 beats) AND the scroll (+1 beat) both count: xToBeat(currentX) -
    // mouseDownBeat_ folds the scroll into the delta. The old xToBeat(currentX) -
    // xToBeat(mouseDownX) form would have re-derived xToBeat(mouseDownX) under the NEW scroll too
    // and the +1 would have cancelled out, landing at 4.0 instead of 5.0.
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 5.0);
}

// ---- Edge auto-scroll: arm/disarm gating and one tick's effect on each axis ----

TEST(PianoRollAutoScrollTest, TimerArmsInsideTheEdgeZoneDuringAMoveDragAndStopsOnMouseUp) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const auto grid = f.roll.getNoteGridBounds();
    const juce::Point<float> nearRightEdge((float)grid.getRight() - 2.0f, anchor.y);

    f.roll.mouseDown(leftClick(f.roll, anchor));
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest())
        << "arming only happens from mouseDrag's own check, never from mouseDown alone";

    f.roll.mouseDrag(leftDrag(f.roll, nearRightEdge, anchor));
    EXPECT_TRUE(f.roll.isAutoScrollTimerRunningForTest()) << "the pointer sits inside the right edge zone";

    // Back to the dead middle band disarms it again without a release.
    f.roll.mouseDrag(leftDrag(f.roll, anchor, anchor));
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest());

    // Re-arm, then release: the timer never outlives the drag it belonged to.
    f.roll.mouseDrag(leftDrag(f.roll, nearRightEdge, anchor));
    ASSERT_TRUE(f.roll.isAutoScrollTimerRunningForTest());
    f.roll.mouseUp(leftDrag(f.roll, nearRightEdge, anchor));
    EXPECT_FALSE(f.roll.isAutoScrollTimerRunningForTest());
}

TEST(PianoRollAutoScrollTest, EachTickAdvancesTheViewAndReDerivesThePreviewFromTheLastPointer) {
    // Two independent fixtures, each dragged into the right edge zone and released after a
    // DIFFERENT number of ticks: if the later mouseUp lands further along, the preview at
    // commit time really did keep following the LAST tick's scroll rather than a stale delta
    // captured once when the drag first armed.
    auto runWithTicks = [](int tickCount) {
        PianoRollFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
        f.open(clipId);
        const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));

        int viewChanged = 0;
        f.roll.onHorizontalViewChanged = [&] { ++viewChanged; };

        const auto rect = f.roll.getNoteRect(id);
        const juce::Point<float> anchor = centreOf(rect);
        const auto grid = f.roll.getNoteGridBounds();
        const juce::Point<float> nearRightEdge((float)grid.getRight() - 2.0f, anchor.y);

        f.roll.mouseDown(leftClick(f.roll, anchor));
        f.roll.mouseDrag(leftDrag(f.roll, nearRightEdge, anchor));

        const double beatBefore = f.roll.getFirstVisibleBeat();
        for (int i = 0; i < tickCount; ++i)
            f.roll.tickAutoScrollForTest();
        EXPECT_GT(f.roll.getFirstVisibleBeat(), beatBefore) << "near the RIGHT edge the view scrolls FORWARD";
        EXPECT_GE(viewChanged, tickCount) << "onHorizontalViewChanged fires on every scrolling tick";

        f.roll.mouseUp(leftDrag(f.roll, nearRightEdge, anchor));
        return f.doc.getNote(id)->startBeat;
    };

    const double afterOneTick = runWithTicks(1);
    const double afterThreeTicks = runWithTicks(3);
    EXPECT_GT(afterThreeTicks, afterOneTick);
}

TEST(PianoRollAutoScrollTest, VerticalTickWalksOnlyVisibleRowsUnderAnActiveScaleContext) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // C: in C major
    f.open(clipId);
    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ true);
    ASSERT_TRUE(id.isValid());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const auto grid = f.roll.getNoteGridBounds();
    const juce::Point<float> nearTopEdge(anchor.x, (float)grid.getY() + 2.0f);

    const int startPitch = f.roll.getFirstVisiblePitchForTest();
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, nearTopEdge, anchor));
    ASSERT_TRUE(f.roll.isAutoScrollTimerRunningForTest());

    // This position (2px inside the edge, deep zone penetration) used to move a WHOLE row on the
    // very first tick, because the old per-tick std::llround rounded its ~0.92-row velocity up to
    // 1. Now topRowPosition_ accumulates that velocity exactly, uncoarsened, so firstVisiblePitch_
    // (still an int — see the class comment) may take a couple of ticks to cross a row boundary;
    // what must hold on EVERY tick is that the continuous anchor itself makes real, monotonic
    // forward progress (see the dedicated shallow-penetration test below for the "no tick may
    // contribute zero" half of this).
    double previousPosition = f.roll.getTopRowPositionForTest();
    for (int i = 0; i < 4; ++i) {
        f.roll.tickAutoScrollForTest();
        const double position = f.roll.getTopRowPositionForTest();
        EXPECT_GT(position, previousPosition) << "tick " << i;
        previousPosition = position;
    }
    const int afterFourTicks = f.roll.getFirstVisiblePitchForTest();
    EXPECT_GT(afterFourTicks, startPitch) << "near the TOP edge the view walks upward (higher pitches)";

    const auto& visible = f.roll.getVisiblePitchesForTest();
    EXPECT_TRUE(std::binary_search(visible.begin(), visible.end(), afterFourTicks))
        << "the walked-to row must itself be a VISIBLE pitch, never one the scale filter collapsed";
}

// THE dead-outer-half-of-zone fix: 2px INSIDE the OUTER boundary of the 24px edge zone is shallow
// penetration (~0.08 of the zone), which the old per-tick std::llround always rounded down to a
// ZERO row step — the pointer could sit there forever and the view would never move. Every tick
// must now make SOME progress, however small.
TEST(PianoRollAutoScrollTest, VerticalAutoScrollAdvancesFractionallyEvenAtShallowZonePenetration) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const auto grid = f.roll.getNoteGridBounds();
    const juce::Point<float> shallowTopEdge(anchor.x, (float)grid.getY() + (float)(synth::ui::kEdgeZonePx - 2));

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, shallowTopEdge, anchor));
    ASSERT_TRUE(f.roll.isAutoScrollTimerRunningForTest()) << "still inside the zone, however shallow";

    double previous = f.roll.getTopRowPositionForTest();
    for (int i = 0; i < 5; ++i) {
        f.roll.tickAutoScrollForTest();
        const double now = f.roll.getTopRowPositionForTest();
        EXPECT_GT(now, previous) << "tick " << i << ": no tick may contribute zero progress";
        previous = now;
    }
}

// ---- Follow playhead ----

TEST(PianoRollFollowPlayheadTest, PageFlipsTheViewWhenTheBeatWouldLeaveIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    f.open(clipId, 40.0);

    EXPECT_FALSE(f.roll.isFollowPlayhead()) << "off by default";
    f.roll.setFollowPlayhead(true);
    ASSERT_TRUE(f.roll.isFollowPlayhead());

    f.roll.setPlayheadBeat(40.0); // well beyond the current view's right edge
    EXPECT_GT(f.roll.getFirstVisibleBeat(), 0.0) << "the view paged forward to keep the playhead visible";

    const auto grid = f.roll.getNoteGridBounds();
    const int x = f.roll.getPlayheadLineX();
    EXPECT_GE(x, grid.getX());
    EXPECT_LE(x, grid.getRight());
}

TEST(PianoRollFollowPlayheadTest, AddsNoExtraRepaintsWhileTheBeatStaysInsideTheView) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.setFollowPlayhead(true);

    f.roll.setPlayheadBeat(2.0);
    EXPECT_EQ(f.roll.requests, 1) << "the first position after an open still costs exactly one strip";

    for (int i = 0; i < 5; ++i)
        f.roll.setPlayheadBeat(2.0);
    EXPECT_EQ(f.roll.requests, 1)
        << "follow adds ZERO extra repaints while the beat is unmoved and already inside the view";
}

TEST(PianoRollFollowPlayheadTest, NeverFlipsWhileADragIsInFlight) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    f.open(clipId, 40.0);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.setFollowPlayhead(true);

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));

    const double beatBefore = f.roll.getFirstVisibleBeat();
    f.roll.setPlayheadBeat(40.0); // way beyond the current view
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), beatBefore)
        << "a Move drag in flight must not be yanked out from under the user by a follow flip";

    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));
}

// FRO247: opening a clip must show ITS pattern, never jump toward wherever a follow flag left over
// from a PREVIOUS clip (or from before the clip was even opened) happens to think the playhead is.
TEST(PianoRollFollowPlayheadTest, OpeningAClipSuppressesAStaleFollowPageUntilReArmed) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "Clip A");
    const auto clipB = f.doc.addClip(trackId, 100.0, 8.0, "Clip B");

    f.open(clipA, 40.0);
    f.roll.setFollowPlayhead(true);
    f.roll.setPlayheadBeat(2.0); // parks the (stale) playhead inside clip A's own view

    // Switching to clip B re-frames the view on clip B's own bounds -- exactly openClip's job.
    f.open(clipB, 40.0);
    const double framedBeat = f.roll.getFirstVisibleBeat();

    // The panel's periodic transport poll delivers the SAME stale beat (still parked inside clip
    // A, way outside clip B's freshly framed view) right after the switch -- follow must not act
    // on it, or the roll would open on an empty grid instead of clip B's notes.
    f.roll.setPlayheadBeat(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), framedBeat)
        << "opening a new clip must show ITS pattern, not jump toward a stale follow position";

    // An explicit re-enable is the one gesture that resumes following.
    f.roll.setFollowPlayhead(true);
    f.roll.setPlayheadBeat(2.0);
    EXPECT_NE(f.roll.getFirstVisibleBeat(), framedBeat) << "re-arming follow still works once the user asks again";
}

// FRO247: a manual scroll away from the playhead must stick -- not get undone by the very next
// follow tick, which is what a mouse-wheel scroll (or a trackpad pan) previously read as.
TEST(PianoRollFollowPlayheadTest, ManualScrollSuspendsFollowUntilReArmed) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    f.open(clipId, 40.0);
    f.roll.setFollowPlayhead(true);

    // Follow pages toward the playhead once, same setup as PageFlipsTheViewWhenTheBeatWouldLeaveIt
    // -- landing it just inside the LEFT edge of the view (setPlayheadBeat's own 0.1*visibleBeats
    // margin), so scrolling the view further RIGHT (forward in time) is what leaves it behind.
    f.roll.setPlayheadBeat(40.0);
    const double followedBeat = f.roll.getFirstVisibleBeat();
    ASSERT_GT(followedBeat, 0.0);

    // The user scrolls away on purpose -- same Shift+wheel gesture ShiftWheelScrollsTimeLocally
    // exercises (scrolls RIGHT, past the playhead at beat 40, leaving it off the left edge).
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -0.5f;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheel);
    const double scrolledBeat = f.roll.getFirstVisibleBeat();
    ASSERT_GT(scrolledBeat, followedBeat) << "the scroll itself must have moved the view right, past the playhead";

    // The SAME playhead position again -- as if the panel's low-rate transport poll ticked again
    // without the beat itself moving -- must not snap the view back to where follow last put it.
    f.roll.setPlayheadBeat(40.0);
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), scrolledBeat)
        << "a manual scroll away from the playhead must stick until follow is explicitly re-armed";

    // Re-enabling follow resumes the paging behaviour, exactly like turning it on fresh.
    f.roll.setFollowPlayhead(true);
    f.roll.setPlayheadBeat(40.0);
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), followedBeat)
        << "an explicit re-enable pages back toward the playhead";
}

// ============================================================================
// 21. MULTI-NOTE RESIZE (11.1), the Cmd unquantized resize (11.2), and the clip-overrun prompt.
// ============================================================================

TEST(PianoRollResizeTest, RightEdgeDragAppliesOneSharedDeltaToTheWholeSelectionInOneUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(5.0, 64, 2.0));
    const auto idC = f.doc.addNote(clipId, makeNote(9.0, 67, 1.0)); // NOT selected
    ASSERT_TRUE(idA.isValid() && idB.isValid() && idC.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    const auto rectA = f.roll.getNoteRect(idA);
    const juce::Point<float> edge((float)rectA.getRight() - 2.0f, (float)rectA.getCentreY());
    const juce::Point<float> dragged(edge.x + 40.0f, edge.y); // +1 beat at 40 px/beat

    f.roll.mouseDown(leftClick(f.roll, edge));
    EXPECT_EQ(f.roll.getResizeNoteCountForTest(), 2) << "the gesture snapshotted the whole selection";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    EXPECT_DOUBLE_EQ(f.roll.getResizeDeltaForTest(), 1.0);
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->lengthBeats, 3.0) << "its OWN length plus the shared delta, "
                                                              "not everyone snapping to the same end";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idC)->lengthBeats, 1.0) << "an unselected note is untouched";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0) << "a resize never moves a start";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->lengthBeats, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "the multi-note resize was ONE undo step";
}

// Grabbing a note that is NOT in the selection replaces the selection with it (mouseDown's rule),
// so it resizes alone — the pre-existing single-note behaviour, unchanged.
TEST(PianoRollResizeTest, GrabbingANoteOutsideTheSelectionResizesOnlyThatNote) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(5.0, 64, 2.0));
    f.roll.getSelectionForTest().setSelection({idB}); // grab A while B is selected

    const auto rectA = f.roll.getNoteRect(idA);
    const juce::Point<float> edge((float)rectA.getRight() - 2.0f, (float)rectA.getCentreY());
    const juce::Point<float> dragged(edge.x + 40.0f, edge.y);

    f.roll.mouseDown(leftClick(f.roll, edge));
    EXPECT_EQ(f.roll.getResizeNoteCountForTest(), 1);
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->lengthBeats, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->lengthBeats, 2.0) << "B was dropped from the selection by the grab";
}

// A shortening drag floors every note at kMinNoteLengthBeats INDIVIDUALLY, so a note already
// shorter than the shared delta cannot be inverted (or, worse, lengthened by a floor of one grid
// division).
TEST(PianoRollResizeTest, ShorteningDragFloorsEachNoteIndividually) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto idLong = f.doc.addNote(clipId, makeNote(1.0, 60, 2.0));
    const auto idShort = f.doc.addNote(clipId, makeNote(5.0, 64, 0.25));
    f.roll.getSelectionForTest().setSelection({idLong, idShort});

    const auto rect = f.roll.getNoteRect(idLong);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x - 40.0f, edge.y); // -1 beat

    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idLong)->lengthBeats, 1.0) << "the grabbed note snaps to the grid";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idShort)->lengthBeats, PianoRollComponent::kMinNoteLengthBeats)
        << "0.25 - 1.0 would invert it, so it floors at the editor's minimum";
}

// 11.2: Cmd on a right EDGE bypasses the grid entirely; Cmd anywhere else on the note still scrubs
// velocity (the pre-existing gesture), so the two Cmd meanings never collide.
TEST(PianoRollResizeTest, CmdOnTheRightEdgeResizesWithoutSnapping) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0) << "the fixture pins a 1-beat grid";

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> nudged(edge.x + 10.0f, edge.y); // a QUARTER of a grid division

    // Without Cmd the sub-division drag snaps back to where it started: no change at all.
    f.roll.mouseDown(leftClick(f.roll, edge));
    EXPECT_FALSE(f.roll.isResizeUnquantizedForTest());
    f.roll.mouseDrag(leftDrag(f.roll, nudged, edge));
    f.roll.mouseUp(leftDrag(f.roll, nudged, edge));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 1.0) << "snapped: a quarter-division drag rounds away";
    EXPECT_FALSE(f.undo.canUndo());

    // With Cmd the note's end lands exactly on the POINTER (a resize sets an absolute end, it does
    // not add a delta — so the 2 px grab offset inside the edge is part of the answer: the pointer
    // sits at beat 2.2, giving a length of 1.2 rather than a snapped 1.0).
    f.roll.mouseDown(leftClick(f.roll, edge, juce::ModifierKeys::commandModifier));
    EXPECT_TRUE(f.roll.isResizeUnquantizedForTest());
    EXPECT_EQ(f.roll.getResizeNoteCountForTest(), 1);
    f.roll.mouseDrag(leftDrag(f.roll, nudged, edge, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, nudged, edge, juce::ModifierKeys::commandModifier));
    EXPECT_NEAR(f.doc.getNote(id)->lengthBeats, 1.2, 1.0e-9) << "Cmd bypassed the grid: a sub-division length";
    EXPECT_TRUE(f.undo.canUndo());
    // Latched per gesture: the bypass must not leak into the next, plain, resize.
    EXPECT_FALSE(f.roll.isResizeUnquantizedForTest());
}

TEST(PianoRollResizeTest, CmdOnTheNoteBodyStillScrubsVelocityRatherThanResizing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    auto n = makeNote(1.0, 60, 2.0);
    n.velocity = 80;
    const auto id = f.doc.addNote(clipId, n);
    f.roll.getSelectionForTest().setSelection({id});

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    const juce::Point<float> up(anchor.x, anchor.y - 10.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::altModifier));
    EXPECT_EQ(f.roll.getResizeNoteCountForTest(), 0) << "this is a velocity scrub, not a resize";
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseUp(leftDrag(f.roll, up, anchor, juce::ModifierKeys::altModifier));

    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 2.0) << "the length is untouched";
    EXPECT_EQ(f.doc.getNote(id)->velocity, 90);
}

// The UI clip-length clamp is GONE (11.1): a note may be dragged out past the clip's end, and the
// mouse-up asks about it instead of silently trimming.
TEST(PianoRollResizeTest, ResizePastTheClipEndIsAllowedAndRaisesTheExtendPrompt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(3.0, 60, 1.0)); // ends exactly at the clip's end
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + 80.0f, edge.y); // +2 beats, well past the clip's end

    ASSERT_EQ(f.roll.extendPrompts, 0);
    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));

    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 3.0) << "no clip-length clamp — the note is as long as dragged";
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0) << "the clip is NOT grown behind the user's back";
    ASSERT_EQ(f.roll.extendPrompts, 1);
    EXPECT_DOUBLE_EQ(f.roll.lastExtendPromptRequest, 6.0) << "the prompt asks for the max note END";
    EXPECT_EQ(f.roll.lastExtendPromptClipId, clipId) << "and names the clip whose notes overran";
}

TEST(PianoRollResizeTest, AResizeThatStaysInsideTheClipRaisesNoPrompt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + 40.0f, edge.y);
    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));

    EXPECT_EQ(f.roll.extendPrompts, 0);
}

// "Extend" — its OWN undo step, deliberately not merged with the resize that provoked it.
TEST(PianoRollResizeTest, ExtendClipToGrowsTheClipInItsOwnUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(3.0, 60, 3.0)); // already overruns
    ASSERT_TRUE(id.isValid());

    EXPECT_TRUE(f.roll.extendClipTo(clipId, 6.0));
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 6.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 3.0) << "undoing the extend leaves the note alone";

    // Already long enough: no mutation, no undo step.
    EXPECT_FALSE(f.roll.extendClipTo(clipId, 2.0));
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0);
}

// THE capture regression. A modal alert blocks user INPUT, not the message thread: an AI action, an
// undo/redo or a timer can openClip() a DIFFERENT clip while the overrun prompt is up. The answer
// must therefore act on the clip that was captured when the prompt was raised — reading the live
// clipId_ at answer time silently grew whichever clip happened to be open, which is precisely the
// "the clip is NOT grown behind the user's back" guarantee this feature rests on.
TEST(PianoRollResizeTest, ExtendAnswerActsOnTheCapturedClipNotWhicheverIsOpenNow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");
    const auto clipB = f.doc.addClip(trackId, 16.0, 8.0, "B");

    f.open(clipA);
    const auto id = f.doc.addNote(clipA, makeNote(3.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + 80.0f, edge.y); // out past A's end
    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));

    ASSERT_EQ(f.roll.extendPrompts, 1);
    const auto promptedClip = f.roll.lastExtendPromptClipId;
    const double promptedLength = f.roll.lastExtendPromptRequest;
    ASSERT_EQ(promptedClip, clipA);

    // The roll gets repointed at B before the user answers.
    f.open(clipB);
    ASSERT_EQ(f.roll.getClipId(), clipB);

    // Answer "Extend" exactly the way the real alert callback does.
    f.roll.applyExtendPromptAnswer(promptedClip, promptedLength, /*extend=*/true);

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipA)->lengthBeats, 6.0) << "the clip that actually overran grew";
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipB)->lengthBeats, 8.0) << "the clip that happens to be OPEN is untouched";
}

// The captured clip being deleted while the alert is up is a silent no-op, not a crash and not a
// resurrection: extendClipTo looks the id up in the doc at answer time.
TEST(PianoRollResizeTest, ExtendAnswerForADeletedClipIsANoOp) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");
    f.open(clipA);
    const auto id = f.doc.addNote(clipA, makeNote(3.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + 80.0f, edge.y);
    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge));
    ASSERT_EQ(f.roll.extendPrompts, 1);
    const auto promptedClip = f.roll.lastExtendPromptClipId;
    const double promptedLength = f.roll.lastExtendPromptRequest;

    ASSERT_TRUE(f.doc.removeClip(clipA));
    const int undoDepthBefore = f.undo.canUndo() ? 1 : 0;

    f.roll.applyExtendPromptAnswer(promptedClip, promptedLength, /*extend=*/true);
    EXPECT_EQ(f.doc.getClip(clipA), nullptr) << "answering must not resurrect the clip";
    EXPECT_EQ(f.undo.canUndo() ? 1 : 0, undoDepthBefore) << "and must push no undo step";
}

// The "Keep" arm is a real no-op rather than a differently-shaped write — the notes are already the
// length the user dragged, and nothing further happens.
TEST(PianoRollResizeTest, KeepAnswerWritesNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(3.0, 60, 3.0)).isValid());
    ASSERT_FALSE(f.undo.canUndo());

    f.roll.applyExtendPromptAnswer(clipId, 6.0, /*extend=*/false);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0) << "Keep leaves the clip exactly as it was";
    EXPECT_FALSE(f.undo.canUndo()) << "and writes no undo step";
}

// "Keep" — the notes stay overrunning, and that is SAFE because playback truncates at the clip
// boundary: TimelineSnapshot clamps every event's end to the clip's end and drops any note starting
// at or past it. This is the assertion that makes the "Keep" arm inaudible rather than wrong.
TEST(PianoRollResizeTest, OverrunNotesAreTruncatedByTheSnapshotSoKeepingThemIsInaudible) {
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 2.0, 4.0, "Clip"); // absolute beats 2..6

    // Overruns the clip's end by 2 beats (clip-relative 3.0 + 3.0 == 6.0 vs a length of 4.0).
    ASSERT_TRUE(doc.addNote(clipId, makeNote(3.0, 60, 3.0)).isValid());
    // Starts PAST the clip's end entirely — dropped rather than clamped.
    ASSERT_TRUE(doc.addNote(clipId, makeNote(5.0, 67, 1.0)).isValid());

    const auto snapshot = synth::TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->notes.size(), 1u) << "the note starting past the clip end contributes nothing";
    EXPECT_DOUBLE_EQ(snapshot->notes[0].startBeat, 5.0) << "clip start 2.0 + note start 3.0";
    EXPECT_DOUBLE_EQ(snapshot->notes[0].endBeat, 6.0) << "clamped to the clip's own end, not 8.0";
    EXPECT_EQ(snapshot->notes[0].pitch, 60);
}

// ============================================================================
// 26. CMD+DRAG = unsnapped MOVE, and the velocity scrub's move to Option (3.4).
// ============================================================================

TEST(PianoRollCmdMoveTest, CmdDragOnANoteBodyMovesItWithoutSnapping) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0) << "the fixture pins a 1-beat grid";

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    const juce::Point<float> nudged(anchor.x + 10.0f, anchor.y); // +0.25 beats: a QUARTER division

    // Plain drag snaps back to where it started — a sub-division move rounds away entirely.
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, nudged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, nudged, anchor));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo());

    // Cmd+drag lands exactly where the pointer went.
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseDrag(leftDrag(f.roll, nudged, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, nudged, anchor, juce::ModifierKeys::commandModifier));
    EXPECT_NEAR(f.doc.getNote(id)->startBeat, 2.25, 1.0e-9) << "Cmd bypassed the grid";
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "a horizontal drag changed no pitch";
    EXPECT_TRUE(f.undo.canUndo());
}

// Cmd+CLICK (a press that never crossed the drag threshold) is still an additive-select TOGGLE, and
// it must not move anything or write an undo step.
TEST(PianoRollCmdMoveTest, CmdClickTogglesSelectionWithoutMovingOrRecording) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto a = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto b = f.doc.addNote(clipId, makeNote(5.0, 64, 1.0));
    f.roll.getSelectionForTest().setSelection({a});

    // Cmd+click an UNSELECTED note: adds it.
    const auto bCentre = centreOf(f.roll.getNoteRect(b));
    f.roll.mouseDown(leftClick(f.roll, bCentre, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftClick(f.roll, bCentre, juce::ModifierKeys::commandModifier));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(a));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(b));
    EXPECT_DOUBLE_EQ(f.doc.getNote(b)->startBeat, 5.0) << "a click moves nothing";
    EXPECT_FALSE(f.undo.canUndo()) << "selection is not document state";

    // Cmd+click it AGAIN: removes it. That is the toggle half the drag has to share the modifier with.
    f.roll.mouseDown(leftClick(f.roll, bCentre, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftClick(f.roll, bCentre, juce::ModifierKeys::commandModifier));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(a));
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(b)) << "Cmd+click toggled it back out";
    EXPECT_FALSE(f.undo.canUndo());
}

// A Cmd+DRAG on an already-selected note must NOT toggle it out from under the gesture — the toggle
// only fires when nothing moved.
TEST(PianoRollCmdMoveTest, CmdDragOnASelectedNoteKeepsItSelected) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    const juce::Point<float> moved(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseDrag(leftDrag(f.roll, moved, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, moved, anchor, juce::ModifierKeys::commandModifier));

    EXPECT_TRUE(f.roll.getSelectionForTest().contains(id)) << "the drag must not deselect what it moved";
    EXPECT_NEAR(f.doc.getNote(id)->startBeat, 3.0, 1.0e-9);
}

TEST(PianoRollCmdMoveTest, OptionDragScrubsVelocityAndCmdDragNoLongerDoes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    auto n = makeNote(2.0, 60, 1.0);
    n.velocity = 80;
    const auto id = f.doc.addNote(clipId, n);
    f.roll.getSelectionForTest().setSelection({id});

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    const juce::Point<float> up(anchor.x, anchor.y - 10.0f);

    // Option = velocity, and it changes no geometry.
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseUp(leftDrag(f.roll, up, anchor, juce::ModifierKeys::altModifier));
    EXPECT_EQ(f.doc.getNote(id)->velocity, 90);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);

    // Cmd = move, and it changes no velocity. A vertical Cmd drag moves the PITCH now, which is the
    // clearest proof the two gestures really did swap.
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, up, anchor, juce::ModifierKeys::commandModifier));
    EXPECT_EQ(f.doc.getNote(id)->velocity, 90) << "unchanged: Cmd is not the velocity gesture any more";
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61) << "one row up";
}

// Cmd on the right EDGE still resizes (the more specific of the two Cmd gestures), so the body case
// above cannot have swallowed it.
TEST(PianoRollCmdMoveTest, CmdOnTheRightEdgeStillResizesRatherThanMoving) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> edge((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + 10.0f, edge.y);

    f.roll.mouseDown(leftClick(f.roll, edge, juce::ModifierKeys::commandModifier));
    EXPECT_TRUE(f.roll.isResizeUnquantizedForTest()) << "still the resize gesture, not the move";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, edge, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, dragged, edge, juce::ModifierKeys::commandModifier));

    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0) << "a resize never moves the start";
    EXPECT_GT(f.doc.getNote(id)->lengthBeats, 1.0);
}
