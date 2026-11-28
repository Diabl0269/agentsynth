// TimelineClipLaneMouseTests.cpp — drag/trim, edge-auto-scroll velocity, beat-anchored drag under
// mid-drag view scroll, and the gated auto-scroll timer.
#include "../../Layout/ViewportDragFollow.h"
#include "TimelineClipLaneTestFixture.h"
#include "UI/Timeline/EdgeAutoScroll.h"

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

    // Left edge: start moves, end stays fixed.
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

// ---- Left-edge trim anchors notes to absolute time ----

namespace {
// Drags the left edge of `clipId` by `beats` (40 px/beat) through the real mouse path.
void dragLeftEdge(ClipLaneFixture& f, ClipId clipId, double beats) {
    const auto rect = f.lane.getClipRect(clipId);
    const juce::Point<float> edge((float)rect.getX() + 3.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + (float)(beats * 40.0), edge.y);
    f.lane.mouseDown(leftClick(f.lane, edge));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
    f.lane.mouseUp(leftDrag(f.lane, dragged, edge));
}

double absoluteStart(const synth::Clip& clip, const synth::MidiNote& note) { return clip.startBeat + note.startBeat; }
} // namespace

TEST(TimelineClipLaneInteractionTest, LeftTrimShrinkKeepsSurvivingNotesAtTheirAbsoluteBeat) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)).isValid()); // abs [2, 3): wholly before the edge
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(0.5, 62, 2.0)).isValid()); // abs [2.5, 4.5): straddles the edge
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(3.0, 64, 1.0)).isValid()); // abs [5, 6)

    dragLeftEdge(f, clipId, 1.3); // start snaps 2 -> 3

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_NE(clip, nullptr);
    EXPECT_DOUBLE_EQ(clip->startBeat, 3.0);
    EXPECT_DOUBLE_EQ(clip->startBeat + clip->lengthBeats, 6.0);
    ASSERT_EQ(clip->notes.size(), 2u) << "the note wholly before the new edge is dropped";
    EXPECT_EQ(clip->notes[0].pitch, 62);
    EXPECT_DOUBLE_EQ(absoluteStart(*clip, clip->notes[0]), 3.0) << "straddler keeps only its right half";
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 1.5);
    EXPECT_EQ(clip->notes[1].pitch, 64);
    EXPECT_DOUBLE_EQ(absoluteStart(*clip, clip->notes[1]), 5.0) << "untouched note stays at the same absolute beat";
    EXPECT_DOUBLE_EQ(clip->notes[1].lengthBeats, 1.0);
}

TEST(TimelineClipLaneInteractionTest, LeftTrimGrowKeepsEveryNoteAtItsAbsoluteBeat) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 4.0, "Clip"); // [4, 8)
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)).isValid()); // abs 4
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)).isValid()); // abs 6

    dragLeftEdge(f, clipId, -1.3); // start snaps 4 -> 3

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_NE(clip, nullptr);
    EXPECT_DOUBLE_EQ(clip->startBeat, 3.0);
    EXPECT_DOUBLE_EQ(clip->startBeat + clip->lengthBeats, 8.0);
    ASSERT_EQ(clip->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(absoluteStart(*clip, clip->notes[0]), 4.0);
    EXPECT_DOUBLE_EQ(absoluteStart(*clip, clip->notes[1]), 6.0);
}

TEST(TimelineClipLaneInteractionTest, LeftTrimUndoRestoresGeometryAndNotesInOneStep) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)).isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(3.0, 64, 1.0)).isValid());

    dragLeftEdge(f, clipId, 1.3);
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    const auto* clip = f.doc.getClip(clipId);
    ASSERT_NE(clip, nullptr);
    EXPECT_DOUBLE_EQ(clip->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(clip->lengthBeats, 4.0);
    ASSERT_EQ(clip->notes.size(), 2u) << "the dropped note comes back";
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(clip->notes[1].startBeat, 3.0);
    EXPECT_FALSE(f.undo.canUndo()) << "geometry and notes reverted in a single step";
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

    // The drag first moves a little inside the dead band, so the edge zones that follow are reached from outside.
    f.lane.mouseDrag(leftDrag(f.lane, {anchor.x + 20.0f, anchor.y}, anchor));
    EXPECT_FALSE(f.lane.isAutoScrollTimerRunningForTest());

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
    f.lane.tickAutoScrollForTest(); // the speed eases out (it was one ramp step), then the follow stops itself
    f.lane.tickAutoScrollForTest();
    EXPECT_FALSE(f.lane.isAutoScrollTimerRunningForTest()) << "pointer back in the dead band disarms it";
    EXPECT_EQ(scrollCallbackCount, 1) << "and the ease-out scrolled nothing more";

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

    // A clip pressed with the pointer already inside the right edge zone stays put until the pointer moves toward it.
    ClipLaneFixture still;
    const auto stillTrack = still.doc.addTrack(TrackKind::Midi, "Track 1");
    // A one-beat clip whose middle sits 12 px from the right end, so the press lands on it inside the zone.
    const double clipBeat = still.state.xToBeat((double)still.lane.getWidth() - 12.0) - 0.5;
    const auto stillClip = still.doc.addClip(stillTrack, clipBeat, 1.0, "Clip");
    still.selection.setSelection({stillClip});
    const auto from = centreOf(still.lane.getClipRect(stillClip));
    ASSERT_GE(from.x, (float)still.lane.getWidth() - 24.0f) << "the press is inside the edge zone";
    const juce::Point<float> inZone = from;
    const juce::Point<float> towardEdge(inZone.x + 6.0f, from.y);
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             still.lane.mouseDown(leftClick(still.lane, from));
             still.lane.mouseDrag(leftDrag(still.lane, inZone, from));
         },
         [&] { still.lane.tickAutoScrollForTest(); },
         [&] { still.lane.mouseDrag(leftDrag(still.lane, towardEdge, from)); },
         [&] { return still.state.firstVisibleBeat; },
         [&] { still.lane.mouseUp(leftDrag(still.lane, towardEdge, from)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the view scrolling, the clip in view.
    struct Past {
        ClipLaneFixture f;
        synth::ClipId clip;
        juce::Point<float> anchor;
        Past() {
            f.state.firstVisibleBeat = 5.0;
            const auto track = f.doc.addTrack(TrackKind::Midi, "Track 1");
            clip = f.doc.addClip(track, 7.0, 4.0, "Clip");
            f.selection.setSelection({clip});
            anchor = centreOf(f.lane.getClipRect(clip));
        }
        juce::Point<float> at(float px) const { return {(float)f.lane.getWidth() + px, anchor.y}; }
    };
    std::shared_ptr<Past> past;
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             past = std::make_shared<Past>();
             past->f.lane.mouseDown(leftClick(past->f.lane, past->anchor));
         },
         [&](float px) { past->f.lane.mouseDrag(leftDrag(past->f.lane, past->at(px), past->anchor)); },
         [&] { past->f.lane.tickAutoScrollForTest(); }, [&] { return past->f.state.firstVisibleBeat; },
         [&] {
             const auto geometry = past->f.lane.getEffectiveGeometryForTest(past->clip);
             return geometry.has_value() && past->f.state.beatToX(geometry->first) < past->f.lane.getWidth();
         },
         [&] { past->f.lane.mouseUp(leftDrag(past->f.lane, past->at(40.0f), past->anchor)); }});
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

// The Select tool's cursor stays in charge until a move drag has actually moved the clips; then the
// grab hand shows (the copy cursor while Alt-copying) and the tool cursor returns on release.
TEST(TimelineClipLaneInteractionTest, MoveDragShowsTheGrabCursorAndReleaseRestoresTheToolCursor) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    const auto anchor = centreOf(f.lane.getClipRect(clipId));
    const juce::Point<float> dragged(anchor.x + 52.0f, anchor.y);

    f.lane.mouseDown(leftClick(f.lane, anchor));
    EXPECT_TRUE(f.lane.getMouseCursor() == juce::MouseCursor::NormalCursor) << "a press alone changes nothing";
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor));
    EXPECT_TRUE(f.lane.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor));
    EXPECT_TRUE(f.lane.getMouseCursor() == juce::MouseCursor::NormalCursor);
}

TEST(TimelineClipLaneInteractionTest, AltCopyDragShowsTheCopyCursorAndReleaseRestoresTheToolCursor) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    const auto anchor = centreOf(f.lane.getClipRect(clipId));
    const juce::Point<float> dragged(anchor.x + 52.0f, anchor.y);
    constexpr int kAlt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(leftClick(f.lane, anchor, kAlt));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor, kAlt));
    EXPECT_TRUE(f.lane.getMouseCursor() == juce::MouseCursor::CopyingCursor);
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor)); // Alt released mid-drag: the move continues
    EXPECT_TRUE(f.lane.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor));
    EXPECT_TRUE(f.lane.getMouseCursor() == juce::MouseCursor::NormalCursor);
}

// ---- A right-edge resize stops at the end of the section placement the clip starts in ----

namespace {
// Presses a clip's right edge and drags it `beats` to the right (40 px per beat), releasing there.
void dragRightEdge(ClipLaneFixture& f, synth::ClipId clipId, double beats) {
    const auto rect = f.lane.getClipRect(clipId);
    const juce::Point<float> edge((float)rect.getRight() - 3.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + (float)(beats * 40.0), edge.y);
    f.lane.mouseDown(leftClick(f.lane, edge));
    f.lane.mouseDrag(leftDrag(f.lane, {edge.x + 20.0f, edge.y}, edge));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
    f.lane.mouseUp(leftDrag(f.lane, dragged, edge));
}
} // namespace

TEST(TimelineClipLaneSectionCapTest, ResizingPastTheSectionEndStopsThereAndTellsOnce) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6) inside a section [0, 8)
    ASSERT_TRUE(f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5).isValid());
    std::vector<juce::String> toasts;
    f.lane.onClipCappedBySection = [&toasts](const juce::String& m) { toasts.push_back(m); };

    dragRightEdge(f, clipId, 5.0); // wants 11, the section ends at 8

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 6.0) << "stopped at the section's end";
    ASSERT_EQ(toasts.size(), 1u) << "once per gesture, however many drag events held the cap";
    EXPECT_EQ(toasts[0], "To make this clip longer, make the section longer first.");
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0);
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
}

TEST(TimelineClipLaneSectionCapTest, ResizingInsideTheSectionIsNotCappedAndSaysNothing) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5).isValid());
    int toasts = 0;
    f.lane.onClipCappedBySection = [&toasts](const juce::String&) { ++toasts; };

    dragRightEdge(f, clipId, 1.0); // end 7, inside

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 5.0);
    EXPECT_EQ(toasts, 0);
}

TEST(TimelineClipLaneSectionCapTest, WithoutSectionsTheResizeIsUncappedAndSaysNothing) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    int toasts = 0;
    f.lane.onClipCappedBySection = [&toasts](const juce::String&) { ++toasts; };

    dragRightEdge(f, clipId, 5.0);

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 9.0);
    EXPECT_EQ(toasts, 0);
}

namespace {
// Presses a clip's body and drags it `beats` to the right (40 px per beat), releasing there; `extraFlags` is held
// throughout (Alt copies).
void dragBody(ClipLaneFixture& f, synth::ClipId clipId, double beats, int extraFlags = 0) {
    const auto anchor = centreOf(f.lane.getClipRect(clipId));
    const juce::Point<float> dragged(anchor.x + (float)(beats * 40.0), anchor.y);
    f.lane.mouseDown(leftClick(f.lane, anchor, extraFlags));
    f.lane.mouseDrag(leftDrag(f.lane, {anchor.x + 20.0f, anchor.y}, anchor, extraFlags));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor, extraFlags));
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor, extraFlags));
}

constexpr const char* kTrimmedToast =
    "This clip now ends at its section's end. Hold Option to make the section longer instead.";
} // namespace

TEST(TimelineClipLaneSectionCapTest, MovingAClipPastTheSectionEndTrimsItAndTellsOnce) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6) inside a section [0, 8)
    ASSERT_TRUE(f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5).isValid());
    std::vector<juce::String> toasts;
    f.lane.onClipCappedBySection = [&toasts](const juce::String& m) { toasts.push_back(m); };

    dragBody(f, clipId, 3.0); // start 5, would end at 9

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 5.0) << "the start stays where it was dropped";
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 3.0) << "the end is trimmed to the section's";
    ASSERT_EQ(toasts.size(), 1u);
    EXPECT_EQ(toasts[0], kTrimmedToast);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0);
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
}

TEST(TimelineClipLaneSectionCapTest, TheMovePreviewAlreadyShowsTheTrimmedLength) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5).isValid());

    const auto anchor = centreOf(f.lane.getClipRect(clipId));
    const juce::Point<float> dragged(anchor.x + 120.0f, anchor.y);
    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, {anchor.x + 20.0f, anchor.y}, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor));

    const auto geometry = f.lane.getEffectiveGeometryForTest(clipId);
    ASSERT_TRUE(geometry.has_value());
    EXPECT_DOUBLE_EQ(geometry->first, 5.0);
    EXPECT_DOUBLE_EQ(geometry->second, 3.0) << "drawn as it will land";
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor));
}

// Option (the copy-drag key) makes the section longer so the copy fits whole; plain drag keeps the trim.
TEST(TimelineClipLaneSectionCapTest, OptionCopyDragPastTheSectionEndGrowsTheSectionAndKeepsTheWholeCopy) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    const auto section = f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5);
    ASSERT_TRUE(section.isValid());
    std::vector<juce::String> toasts;
    f.lane.onClipCappedBySection = [&toasts](const juce::String& m) { toasts.push_back(m); };

    dragBody(f, clipId, 3.0, juce::ModifierKeys::altModifier); // the copy wants [5, 9), the section ends at 8

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0) << "the original is untouched";
    const auto* track = f.doc.getTrackForClip(clipId);
    ASSERT_EQ(track->clips.size(), 2u);
    const synth::Clip* copy = track->clips[0].id == clipId ? &track->clips[1] : &track->clips[0];
    EXPECT_DOUBLE_EQ(copy->startBeat, 5.0);
    EXPECT_DOUBLE_EQ(copy->lengthBeats, 4.0) << "the whole clip, not trimmed";
    EXPECT_DOUBLE_EQ(f.doc.getSection(section)->lengthBeats, 12.0) << "one bar longer, so it ends at a bar";
    ASSERT_EQ(toasts.size(), 1u);
    EXPECT_EQ(toasts[0], "Section made longer to fit the clip.");

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u) << "undo takes the copy away";
    EXPECT_DOUBLE_EQ(f.doc.getSection(section)->lengthBeats, 8.0) << "and the section goes back, in the same step";
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step";
}

TEST(TimelineClipLaneSectionCapTest, OptionCopyDragGrowsEveryRepeatAndShiftsTheMusicAfterThem) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    const auto section = f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5);
    ASSERT_TRUE(section.isValid());
    ASSERT_TRUE(f.doc.placeSection(section, 8.0, synth::SectionDropMode::MakeRoom, 0.5).isValid());
    const auto afterId = f.doc.addClip(trackId, 20.0, 2.0, "After");

    dragBody(f, clipId, 3.0, juce::ModifierKeys::altModifier);

    EXPECT_DOUBLE_EQ(f.doc.getSection(section)->lengthBeats, 12.0);
    ASSERT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 12.0) << "the repeat stays touching the first";
    EXPECT_DOUBLE_EQ(f.doc.getClip(afterId)->startBeat, 28.0) << "later music follows both repeats' growth";
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getSection(section)->lengthBeats, 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(afterId)->startBeat, 20.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneSectionCapTest, TheOptionCopyPreviewDrawsTheWholeClipPastTheSectionEnd) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5).isValid());

    const auto anchor = centreOf(f.lane.getClipRect(clipId));
    const juce::Point<float> dragged(anchor.x + 120.0f, anchor.y);
    f.lane.mouseDown(leftClick(f.lane, anchor, juce::ModifierKeys::altModifier));
    f.lane.mouseDrag(leftDrag(f.lane, {anchor.x + 20.0f, anchor.y}, anchor, juce::ModifierKeys::altModifier));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, anchor, juce::ModifierKeys::altModifier));

    const auto ghosts = f.lane.getDragGhostRectsForTest();
    ASSERT_EQ(ghosts.size(), 1u);
    EXPECT_EQ(ghosts[0].getWidth(), f.lane.getClipRect(clipId).getWidth()) << "same length as the clip: not trimmed";
    f.lane.mouseUp(leftDrag(f.lane, dragged, anchor, juce::ModifierKeys::altModifier));
}

TEST(TimelineClipLaneSectionCapTest, MovingInsideTheSectionIsUnchangedAndSilent) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5).isValid());
    int toasts = 0;
    f.lane.onClipCappedBySection = [&toasts](const juce::String&) { ++toasts; };

    dragBody(f, clipId, 2.0); // [4, 8) fits exactly

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 4.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0);
    EXPECT_EQ(toasts, 0);
}

TEST(TimelineClipLaneSectionCapTest, WithoutSectionsMovingIsUncappedAndSilent) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    int toasts = 0;
    f.lane.onClipCappedBySection = [&toasts](const juce::String&) { ++toasts; };

    dragBody(f, clipId, 3.0);

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 5.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 4.0);
    EXPECT_EQ(toasts, 0);
}

TEST(TimelineClipLaneSectionCapTest, TheLeftEdgeStopsAtTheSectionStartAndTellsOnce) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto section = f.doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5);
    ASSERT_TRUE(section.isValid());
    ASSERT_TRUE(f.doc.placeSection(section, 8.0, synth::SectionDropMode::MakeRoom, 0.5).isValid());
    const auto clipId = f.doc.addClip(trackId, 10.0, 4.0, "Clip"); // [10, 14) inside the repeat [8, 16)
    std::vector<juce::String> toasts;
    f.lane.onClipCappedBySection = [&toasts](const juce::String& m) { toasts.push_back(m); };

    dragLeftEdge(f, clipId, -5.0); // wants 5, the section starts at 8

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 6.0) << "the end stays put";
    ASSERT_EQ(toasts.size(), 1u);
    EXPECT_EQ(toasts[0], "To make this clip longer, make the section longer first.");
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 10.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneSectionCapTest, WithoutSectionsTheLeftEdgeIsUncappedAndSilent) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 10.0, 4.0, "Clip");
    int toasts = 0;
    f.lane.onClipCappedBySection = [&toasts](const juce::String&) { ++toasts; };

    dragLeftEdge(f, clipId, -5.0);

    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 5.0);
    EXPECT_EQ(toasts, 0);
}
