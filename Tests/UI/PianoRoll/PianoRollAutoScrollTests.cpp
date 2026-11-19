// PianoRoll edge auto-scroll tests: a drag held in (or past) the edge zone arms the roll's follow, each tick advances
// the view and re-derives the preview from the last pointer, the vertical axis walks only visible rows, and a shallow
// zone still advances. Shared PianoRollFixture and mouse-gesture helpers live in PianoRollTestHelpers.h.

#include "../Layout/ViewportDragFollow.h"
#include "PianoRollTestHelpers.h"

#include "Timeline/TimelineSnapshot.h"
#include "UI/Timeline/EdgeAutoScroll.h"

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
    for (int i = 0; i < 4; ++i) // the speed eases out over a few frames, then the follow stops itself
        f.roll.tickAutoScrollForTest();
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

    // A note pressed with the pointer already inside the right edge zone stays put until the pointer moves toward it.
    PianoRollFixture still;
    const auto trackId = still.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = still.doc.addClip(trackId, 0.0, 64.0, "Clip");
    still.open(clipId);
    const float right = (float)still.roll.getNoteGridBounds().getRight();
    const double noteBeat = still.roll.xToBeat(right - 12.0) - 0.5; // a one-beat note centred 12 px inside the zone
    const auto id = still.doc.addNote(clipId, makeNote(noteBeat, 60, 1.0));
    const juce::Point<float> anchor = centreOf(still.roll.getNoteRect(id));
    ASSERT_GE(anchor.x, right - 24.0f) << "the press is inside the edge zone";
    const juce::Point<float> inZone = anchor, towardEdge(right - 6.0f, anchor.y);
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             still.roll.mouseDown(leftClick(still.roll, anchor));
             still.roll.mouseDrag(leftDrag(still.roll, inZone, anchor));
         },
         [&] { still.roll.tickAutoScrollForTest(); },
         [&] { still.roll.mouseDrag(leftDrag(still.roll, towardEdge, anchor)); },
         [&] { return still.roll.getFirstVisibleBeat(); },
         [&] { still.roll.mouseUp(leftDrag(still.roll, towardEdge, anchor)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the roll scrolling, the note in view.
    std::shared_ptr<PianoRollFixture> past;
    synth::NoteId pastNote;
    juce::Point<float> pastAnchor;
    auto pastAt = [&](float px) {
        return juce::Point<float>((float)past->roll.getNoteGridBounds().getRight() + px, pastAnchor.y);
    };
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             past = std::make_shared<PianoRollFixture>();
             const auto track = past->doc.addTrack(TrackKind::Midi, "Track 1");
             const auto clip = past->doc.addClip(track, 0.0, 64.0, "Clip");
             past->open(clip);
             pastNote = past->doc.addNote(clip, makeNote(2.0, 60, 1.0));
             pastAnchor = centreOf(past->roll.getNoteRect(pastNote));
             past->roll.mouseDown(leftClick(past->roll, pastAnchor));
         },
         [&](float px) { past->roll.mouseDrag(leftDrag(past->roll, pastAt(px), pastAnchor)); },
         [&] { past->roll.tickAutoScrollForTest(); }, [&] { return past->roll.getFirstVisibleBeat(); },
         [&] {
             return past->roll.beatToX(past->roll.getShownGeometryForTest(pastNote).startBeat) <
                    (double)past->roll.getNoteGridBounds().getRight();
         },
         [&] { past->roll.mouseUp(leftDrag(past->roll, pastAt(40.0f), pastAnchor)); }});
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
