// TimelineClipLaneMouseTests.cpp — drag/trim, edge-auto-scroll velocity, beat-anchored drag under
// mid-drag view scroll, and the gated auto-scroll timer.
#include "TimelineClipLaneTestFixture.h"

TEST(TimelineClipLaneInteractionTest, DragMoveSnapsAndIsOneUndoStep) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    const auto rect = f.lane.getClipRect(clipId);
    const auto anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 52.0f, anchor.y); // +1.3 beats at 40 px/beat

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor));
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor));

    const auto* moved = f.doc.getClip(clipId);
    ASSERT_NE(moved, nullptr);
    EXPECT_DOUBLE_EQ(moved->startBeat, 1.0) << "1.3 beats, Beat snap -> exactly 1.0";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 0.0);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was ONE undo step";
}

TEST(TimelineClipLaneInteractionTest, DragMoveMultiSelectionMovesTogetherOneStep) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");
    const auto clipB = f.doc.addClip(trackId, 10.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());
    f.selection.setSelection({clipA, clipB});

    const auto rect = f.lane.getClipRect(clipA); // grab clipA — the "anchor" of the group drag
    const auto anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2.0 beats at 40 px/beat

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor));
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor));

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipA)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipB)->startBeat, 12.0) << "same shared delta, not independently snapped";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipA)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipB)->startBeat, 10.0);
    EXPECT_FALSE(f.undo.canUndo()) << "one drag of a multi-selection is still ONE undo step";
}

// ---- Trim: right edge (length), left edge (start moves, end fixed) ----

TEST(TimelineClipLaneInteractionTest, TrimRightAndLeft) {
    {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
        ASSERT_TRUE(clipId.isValid());

        const auto rect = f.lane.getClipRect(clipId);
        const juce::Point<float> edge((float)rect.getRight() - 3.0f, (float)rect.getCentreY());
        const juce::Point<float> dragged(edge.x + 52.0f, edge.y); // +1.3 beats

        f.lane.mouseDown(leftClick(f.lane, edge));
        f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
        f.lane.mouseUp(leftDrag(f.lane, dragged, edge));

        const auto* resized = f.doc.getClip(clipId);
        ASSERT_NE(resized, nullptr);
        EXPECT_DOUBLE_EQ(resized->startBeat, 2.0) << "right-edge trim never moves the start";
        EXPECT_DOUBLE_EQ(resized->lengthBeats, 5.0) << "end snapped 6+1.3 -> 7.0, length = 7.0 - 2.0";
        ASSERT_TRUE(f.undo.canUndo());
    }

    // Minimum length enforced: dragging the right edge far past the start must clamp, not invert.
    {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
        ASSERT_TRUE(clipId.isValid());

        const auto rect = f.lane.getClipRect(clipId);
        const juce::Point<float> edge((float)rect.getRight() - 3.0f, (float)rect.getCentreY());
        const juce::Point<float> dragged(edge.x - 4000.0f, edge.y); // wildly past the clip's start

        f.lane.mouseDown(leftClick(f.lane, edge));
        f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
        f.lane.mouseUp(leftDrag(f.lane, dragged, edge));

        const auto* resized = f.doc.getClip(clipId);
        ASSERT_NE(resized, nullptr);
        EXPECT_GE(resized->lengthBeats, 0.0625 - 1e-9);
        EXPECT_LT(resized->lengthBeats, 4.0) << "it did shrink, just not past the floor";
    }

    // Left edge: start moves, end stays fixed, notes travel with the clip.
    {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
        ASSERT_TRUE(clipId.isValid());

        const auto rect = f.lane.getClipRect(clipId);
        const juce::Point<float> edge((float)rect.getX() + 3.0f, (float)rect.getCentreY());
        const juce::Point<float> dragged(edge.x + 52.0f, edge.y); // +1.3 beats

        f.lane.mouseDown(leftClick(f.lane, edge));
        f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
        f.lane.mouseUp(leftDrag(f.lane, dragged, edge));

        const auto* resized = f.doc.getClip(clipId);
        ASSERT_NE(resized, nullptr);
        EXPECT_DOUBLE_EQ(resized->startBeat, 3.0) << "start snapped 2+1.3 -> 3.0";
        EXPECT_DOUBLE_EQ(resized->startBeat + resized->lengthBeats, 6.0) << "the end never moves";
        ASSERT_TRUE(f.undo.canUndo());
    }
}

TEST(EdgeAutoScrollVelocityTest, ZeroInsideTheSafeMiddleBand) {
    // [0, 200), zonePx = 24: the dead band is [24, 176].
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(24, 0, 200, 24, 18.0), 0.0) << "exactly at the inner edge";
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(100, 0, 200, 24, 18.0), 0.0) << "dead centre";
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(176, 0, 200, 24, 18.0), 0.0) << "exactly at the other inner edge";
}

TEST(EdgeAutoScrollVelocityTest, CorrectSignAtEachEdge) {
    // Inside the lo-side zone: negative (scroll toward lo/backward).
    EXPECT_LT(synth::ui::edgeScrollVelocity(10, 0, 200, 24, 18.0), 0.0);
    // Inside the hi-side zone: positive (scroll toward hi/forward).
    EXPECT_GT(synth::ui::edgeScrollVelocity(190, 0, 200, 24, 18.0), 0.0);
}

TEST(EdgeAutoScrollVelocityTest, LinearRampWithPenetrationDepth) {
    constexpr int lo = 0, hi = 200, zone = 24;
    constexpr double maxPerTick = 18.0;

    // hi-side: penetration = (pos - (hi - zone)) / zone. At pos = hi - zone (176), penetration = 0
    // -> 0 velocity (already covered above); at pos = hi - zone/2 (188), penetration = 0.5.
    const double halfway = synth::ui::edgeScrollVelocity(188, lo, hi, zone, maxPerTick);
    EXPECT_NEAR(halfway, maxPerTick * 0.5, 1e-9);

    // Right at the edge (pos == hi): penetration = 1.0 -> full maxPerTick.
    EXPECT_NEAR(synth::ui::edgeScrollVelocity(hi, lo, hi, zone, maxPerTick), maxPerTick, 1e-9);

    // Mirror on the lo side.
    const double loHalfway = synth::ui::edgeScrollVelocity(12, lo, hi, zone, maxPerTick);
    EXPECT_NEAR(loHalfway, -maxPerTick * 0.5, 1e-9);
    EXPECT_NEAR(synth::ui::edgeScrollVelocity(lo, lo, hi, zone, maxPerTick), -maxPerTick, 1e-9);
}

TEST(EdgeAutoScrollVelocityTest, ClampsAtMaxPerTickBeyondTheComponentEntirely) {
    constexpr int lo = 0, hi = 200, zone = 24;
    constexpr double maxPerTick = 18.0;

    // A pointer dragged far off the left/right edge (JUCE still reports these via mouseDrag) must
    // not grow the speed without bound.
    EXPECT_NEAR(synth::ui::edgeScrollVelocity(-5000, lo, hi, zone, maxPerTick), -maxPerTick, 1e-9);
    EXPECT_NEAR(synth::ui::edgeScrollVelocity(5000, lo, hi, zone, maxPerTick), maxPerTick, 1e-9);
}

TEST(EdgeAutoScrollVelocityTest, DegenerateRangesReturnZero) {
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(10, 0, 200, 0, 18.0), 0.0) << "zonePx <= 0";
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(10, 0, 200, -5, 18.0), 0.0) << "negative zonePx";
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(10, 100, 100, 24, 18.0), 0.0) << "hi == lo";
    EXPECT_DOUBLE_EQ(synth::ui::edgeScrollVelocity(10, 200, 0, 24, 18.0), 0.0) << "hi < lo";
}

// ---- Beat-anchored drag: correct under a mid-drag view scroll, unchanged when there is none ----

// Baseline: with no scroll at all, the beat-anchored mapping commits exactly where the plain pixel
// math (delta = (currentX - pressX) / pixelsPerBeat) would have — DragMoveSnapsAndIsOneUndoStep
// above already covers this at the interaction level; this pins the exact arithmetic.
TEST(TimelineClipLaneBeatAnchoredDragTest, NoScrollMatchesThePreRefactorPixelMath) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Off; // no rounding — the raw mapping, unobscured
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    const auto rect = f.lane.getClipRect(clipId);
    const auto anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2.0 beats at 40 px/beat, no scroll

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor));
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor));

    const auto* moved = f.doc.getClip(clipId);
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->startBeat, 4.0, 1e-9) << "2.0 (original) + 80px/40ppb == the plain pixel-delta result too";
}

// The regression this backfill exists to guard: a view scroll that happens BETWEEN mouseDown and a
// later mouseDrag (an edge-scroll tick, or in principle any other scroll source) must not be
// ignored. The pointer's SCREEN position is unchanged across the scroll, but the beat under it has
// moved — the clip must follow that, which the pre-refactor pixel-anchored math
// ((currentX - pressX) / pixelsPerBeat) could not do (it would see zero pixel travel and commit no
// move at all).
TEST(TimelineClipLaneBeatAnchoredDragTest, MidDragViewScrollKeepsTheClipUnderThePointer) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Off; // raw mapping, so the expected number is exact
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    const auto rect = f.lane.getClipRect(clipId);
    const auto anchor = centreOf(rect);
    // First move: +1.0 beat's worth of pixels (40 px at 40 px/beat), no scroll yet.
    const juce::Point<float> firstMove(anchor.x + 40.0f, anchor.y);

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, firstMove, anchor));
    {
        const auto beforeScroll = f.lane.getEffectiveGeometryForTest(clipId);
        ASSERT_TRUE(beforeScroll.has_value());
        EXPECT_NEAR(beforeScroll->first, 1.0, 1e-9) << "sanity: +1 beat previewed before any scroll";
    }

    // The view scrolls forward by 2.0 beats mid-drag (what an edge-scroll tick does to
    // firstVisibleBeat) — the pointer's screen position is NOT re-synthesized here on purpose: this
    // is exactly the case autoScrollTick() itself handles (no new MouseEvent fires on its own).
    f.state.scrollBeats(2.0);
    // Re-derive the preview against the scrolled view from the SAME last-known pointer position —
    // the same thing a continued drag (another mouseDrag at the same screen position, e.g. the
    // pointer held still while the timer scrolls under it) or an auto-scroll tick would trigger.
    f.lane.mouseDrag(leftDrag(f.lane, firstMove, anchor));

    // Expected under the beat-anchored fix: delta = xToBeat(firstMove.x, NEW firstVisibleBeat) -
    // mouseDownBeat_(captured against the OLD firstVisibleBeat) = (2.0 + 1.0) - 0.0 = 3.0 beats.
    const auto previewed = f.lane.getEffectiveGeometryForTest(clipId);
    ASSERT_TRUE(previewed.has_value());
    EXPECT_NEAR(previewed->first, 3.0, 1e-9)
        << "the clip tracks the scroll; the old pixel-anchored math would have stayed at 1.0";

    f.lane.mouseUp(leftDrag(f.lane, firstMove, anchor));
    const auto* moved = f.doc.getClip(clipId);
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->startBeat, 3.0, 1e-9);
    EXPECT_NE(moved->startBeat, 1.0) << "the pre-refactor pixel-anchored result, which this fixes";
}

// ---- Auto-scroll timer: gated on drag + edge zone, only via tickAutoScrollForTest() ----

TEST(TimelineClipLaneAutoScrollTimerTest, GatedOnDragAndEdgeZoneOnly) {
    ClipLaneFixture f;              // 1200x400, pixelsPerBeat = 40
    f.state.firstVisibleBeat = 5.0; // room to scroll backward too
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 5.0, 4.0, "Clip"); // lands near the fixture's centre
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    int scrollCallbackCount = 0;
    f.lane.onViewScrolledByDrag = [&] { ++scrollCallbackCount; };

    const auto rect = f.lane.getClipRect(clipId);
    const auto anchor = centreOf(rect);

    EXPECT_FALSE(f.lane.isAutoScrollTimerRunningForTest()) << "nothing pressed yet";

    f.lane.mouseDown(leftClick(f.lane, anchor));
    EXPECT_FALSE(f.lane.isAutoScrollTimerRunningForTest()) << "a press with no drag arms nothing";

    // Drag into the LEFT edge zone (kEdgeZonePx == 24 px from x = 0).
    const juce::Point<float> nearLeftEdge(10.0f, anchor.y);
    f.lane.mouseDrag(leftDrag(f.lane, nearLeftEdge, anchor));
    ASSERT_TRUE(f.lane.isDragInProgress());
    EXPECT_TRUE(f.lane.isAutoScrollTimerRunningForTest()) << "dragging with the pointer inside the edge zone arms it";

    const double beatBeforeTick = f.state.firstVisibleBeat;
    f.lane.tickAutoScrollForTest();
    EXPECT_LT(f.state.firstVisibleBeat, beatBeforeTick) << "left-edge tick scrolls the view BACKWARD";
    EXPECT_EQ(scrollCallbackCount, 1) << "each tick that actually scrolls fires onViewScrolledByDrag once";

    // Back to the dead middle band: the timer disarms on the very next drag update.
    const juce::Point<float> middle(anchor.x, anchor.y);
    f.lane.mouseDrag(leftDrag(f.lane, middle, anchor));
    EXPECT_FALSE(f.lane.isAutoScrollTimerRunningForTest()) << "pointer back in the dead band disarms it";

    // Drag into the RIGHT edge zone.
    const juce::Point<float> nearRightEdge((float)f.lane.getWidth() - 10.0f, anchor.y);
    f.lane.mouseDrag(leftDrag(f.lane, nearRightEdge, anchor));
    EXPECT_TRUE(f.lane.isAutoScrollTimerRunningForTest());

    const double beatBeforeRightTick = f.state.firstVisibleBeat;
    f.lane.tickAutoScrollForTest();
    EXPECT_GT(f.state.firstVisibleBeat, beatBeforeRightTick) << "right-edge tick scrolls the view FORWARD";
    EXPECT_EQ(scrollCallbackCount, 2);

    // mouseUp always disarms, regardless of where the pointer last was.
    f.lane.mouseUp(leftDrag(f.lane, nearRightEdge, anchor));
    EXPECT_FALSE(f.lane.isAutoScrollTimerRunningForTest());
}

TEST(TimelineClipLaneAutoScrollTimerTest, TickIsANoOpOnceTheDragHasEnded) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    const auto rect = f.lane.getClipRect(clipId);
    const auto anchor = centreOf(rect);
    const juce::Point<float> nearRightEdge((float)f.lane.getWidth() - 10.0f, anchor.y);

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, nearRightEdge, anchor));
    ASSERT_TRUE(f.lane.isAutoScrollTimerRunningForTest());
    f.lane.mouseUp(leftDrag(f.lane, nearRightEdge, anchor));
    ASSERT_FALSE(f.lane.isAutoScrollTimerRunningForTest());

    // A stray tick after the drag ended (the real juce::Timer could in principle fire once more
    // before stopTimer() takes effect) must be inert — autoScrollTick() re-checks dragging itself.
    const double beatBefore = f.state.firstVisibleBeat;
    f.lane.tickAutoScrollForTest();
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, beatBefore);
}
