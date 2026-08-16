// TimelineClipLaneTests.cpp
//
// Clip lanes — drag/trim/split/duplicate + marquee selection, backed by
// synth::ui::ClipSelectionModel.
//
// Six groups:
//   1. synth::ui::ClipSelectionModel — mirrors SelectionModelTests.cpp's coverage of
//      SelectionModel, just keyed on synth::ClipId.
//   2. synth::ui::clipHitTestMarquee — mirrors SelectionModelTests.cpp's hitTestMarquee coverage.
//   3. synth::ui::TimelineClipLaneArea::computeClipRect — pure geometry, no doc/component state.
//   4. synth::ui::TimelineClipLaneArea interactions — click/marquee select, drag-move, trim,
//      split/duplicate/delete via the menu hook, the panel-scoped Delete key, doc-change pruning,
//      and a snapshot smoke test. None of this is #if SYNTH_ENABLE_TIMELINE-gated: the component
//      compiles and runs unconditionally, exactly like TimelinePanelComponent/
//      TimelineTrackHeaderComponent (only MainComponent's use of it is gated).
//   5. Waveform painting from synth::PeaksFile, the peaks cache and its invalidation, the
//      live-recording strip's repaint-on-arrival rule, and the pure bucketRangeForClip() helper.
//   6. The missing-asset placeholder — setAssetExistsResolver, its cache (no repeated
//      filesystem stats), and that its painted result differs from both the waveform case and the
//      no-asset (MIDI) case.

#include "../Source/AppUndoManager.h"
#include "../Source/Modules/RecordTapModule.h"
#include "../Source/Timeline/PeaksFile.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/ClipSelectionModel.h"
#include "../Source/UI/TimelineClipLaneArea.h"
#include "../Source/UI/TimelineViewState.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ClipId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::ClipSelectionModel;
using synth::ui::TimelineClipLaneArea;
using synth::ui::TimelineViewState;

namespace {
ClipId cid(std::int64_t v) { return ClipId{v}; }

synth::MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0) {
    synth::MidiNote note;
    note.startBeat = startBeat;
    note.pitch = pitch;
    note.lengthBeats = lengthBeats;
    return note;
}
} // namespace

// ============================================================================
// 1. ClipSelectionModel
// ============================================================================

TEST(ClipSelectionModel, StartsEmpty) {
    ClipSelectionModel sel;
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_EQ(sel.size(), 0);
    EXPECT_FALSE(sel.contains(cid(1)));
}

TEST(ClipSelectionModel, AddIsIdempotent) {
    ClipSelectionModel sel;
    EXPECT_TRUE(sel.add(cid(7)));
    EXPECT_FALSE(sel.add(cid(7))) << "adding an already-selected id must report no change";
    EXPECT_EQ(sel.size(), 1);
    EXPECT_TRUE(sel.contains(cid(7)));
}

TEST(ClipSelectionModel, RejectsInvalidClipIdZero) {
    // value == 0 is TimelineDoc's "not found" sentinel — selecting it would let a rejected/absent
    // clip reach a batched move or delete.
    ClipSelectionModel sel;
    EXPECT_FALSE(sel.add(cid(0)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(ClipSelectionModel, RemoveReportsWhetherAnythingWasRemoved) {
    ClipSelectionModel sel;
    sel.add(cid(3));
    EXPECT_TRUE(sel.remove(cid(3)));
    EXPECT_FALSE(sel.remove(cid(3)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(ClipSelectionModel, ToggleReturnsStateAfterToggling) {
    ClipSelectionModel sel;
    EXPECT_TRUE(sel.toggle(cid(2))) << "toggling an unselected id selects it";
    EXPECT_TRUE(sel.contains(cid(2)));
    EXPECT_FALSE(sel.toggle(cid(2))) << "toggling a selected id deselects it";
    EXPECT_FALSE(sel.contains(cid(2)));
}

TEST(ClipSelectionModel, SetSelectionReplacesAndDeduplicates) {
    ClipSelectionModel sel;
    sel.add(cid(99));
    sel.setSelection({cid(1), cid(2), cid(2), cid(0)});

    EXPECT_EQ(sel.size(), 2) << "duplicates collapse and the invalid id is dropped";
    EXPECT_TRUE(sel.contains(cid(1)));
    EXPECT_TRUE(sel.contains(cid(2)));
    EXPECT_FALSE(sel.contains(cid(99))) << "setSelection replaces rather than merges";
}

TEST(ClipSelectionModel, GetSelectedIsOrderedByValueRegardlessOfInsertionOrder) {
    ClipSelectionModel a;
    a.setSelection({cid(30), cid(10), cid(20)});
    ClipSelectionModel b;
    b.setSelection({cid(10), cid(20), cid(30)});

    auto expected = std::vector<ClipId>{cid(10), cid(20), cid(30)};
    EXPECT_EQ(a.getSelected(), expected);
    EXPECT_EQ(b.getSelected(), expected);
}

TEST(ClipSelectionModel, ClearEmptiesEverything) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2)});
    sel.clear();
    EXPECT_TRUE(sel.isEmpty());
}

TEST(ClipSelectionModel, RetainOnlyDropsIdsThatNoLongerExist) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2), cid(3)});

    EXPECT_TRUE(sel.retainOnly({cid(1), cid(3)}));
    EXPECT_EQ(sel.size(), 2);
    EXPECT_TRUE(sel.contains(cid(1)));
    EXPECT_FALSE(sel.contains(cid(2)));
    EXPECT_TRUE(sel.contains(cid(3)));
}

TEST(ClipSelectionModel, RetainOnlyReportsNoChangeWhenEverythingSurvives) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2)});
    EXPECT_FALSE(sel.retainOnly({cid(1), cid(2), cid(5)}));
    EXPECT_EQ(sel.size(), 2);
}

TEST(ClipSelectionModel, RetainOnlyWithNothingAliveClearsSelection) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2)});
    EXPECT_TRUE(sel.retainOnly({}));
    EXPECT_TRUE(sel.isEmpty());
}

// ============================================================================
// 2. clipHitTestMarquee
// ============================================================================

namespace {
std::vector<std::pair<ClipId, juce::Rectangle<int>>> threeClipRects() {
    return {
        {cid(1), juce::Rectangle<int>(0, 0, 100, 100)},
        {cid(2), juce::Rectangle<int>(200, 0, 100, 100)},
        {cid(3), juce::Rectangle<int>(400, 400, 100, 100)},
    };
}
} // namespace

TEST(ClipMarqueeHitTest, SelectsFullyEnclosedClips) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(-10, -10, 330, 130), threeClipRects());
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], cid(1));
    EXPECT_EQ(hits[1], cid(2));
}

TEST(ClipMarqueeHitTest, SelectsPartiallyTouchedClips) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(90, 90, 20, 20), threeClipRects());
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], cid(1));
}

TEST(ClipMarqueeHitTest, MissesClipsOutsideTheBand) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(120, 0, 50, 50), threeClipRects());
    EXPECT_TRUE(hits.empty());
}

TEST(ClipMarqueeHitTest, DegenerateMarqueeSelectsNothing) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(50, 50, 0, 0), threeClipRects());
    EXPECT_TRUE(hits.empty());
}

TEST(ClipMarqueeHitTest, IgnoresInvalidClipIds) {
    std::vector<std::pair<ClipId, juce::Rectangle<int>>> rects{{cid(0), juce::Rectangle<int>(0, 0, 100, 100)}};
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(0, 0, 200, 200), rects);
    EXPECT_TRUE(hits.empty());
}

TEST(ClipMarqueeHitTest, EmptyListYieldsNoHits) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(0, 0, 500, 500), {});
    EXPECT_TRUE(hits.empty());
}

// ============================================================================
// 3. TimelineClipLaneArea::computeClipRect — pure geometry
// ============================================================================

TEST(TimelineClipLaneGeometryTest, GeometryMapsBeatsAndRows) {
    TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    constexpr int rowHeight = 56;

    const auto rect = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);

    EXPECT_EQ(rect.getX(), (int)std::llround(state.beatToX(3.0)));
    EXPECT_EQ(rect.getRight(), (int)std::llround(state.beatToX(5.5)));
    EXPECT_EQ(rect.getY(), 2 * rowHeight);
    EXPECT_EQ(rect.getHeight(), rowHeight);

    // Rows align with timelineTrackRowHeight: row N's y is exactly N * rowHeight, so a header row
    // and its clip row always share the same y regardless of scroll/zoom (both x-only concerns).
    const auto rowBelow = TimelineClipLaneArea::computeClipRect(state, 3, 3.0, 2.5, rowHeight);
    EXPECT_EQ(rowBelow.getY() - rect.getY(), rowHeight);

    // Scroll (firstVisibleBeat) shifts x, never y.
    state.firstVisibleBeat = 1.0;
    const auto scrolled = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);
    EXPECT_EQ(scrolled.getY(), rect.getY());
    EXPECT_LT(scrolled.getX(), rect.getX());
}

// ============================================================================
// 4. TimelineClipLaneArea interactions
// ============================================================================

namespace {

struct ClipLaneFixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    TimelineClipLaneArea lane{state, selection};

    ClipLaneFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Beat;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
    }
};

juce::MouseEvent makeClipMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                    bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

juce::MouseEvent leftClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeClipMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                              pos);
}

juce::MouseEvent leftDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                          int extraFlags = 0) {
    return makeClipMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), true,
                              anchor);
}

juce::MouseEvent rightClick(juce::Component& comp, juce::Point<float> pos) {
    return makeClipMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), false, pos);
}

juce::Point<float> centreOf(juce::Rectangle<int> rect) { return {(float)rect.getCentreX(), (float)rect.getCentreY()}; }

} // namespace

// ---- Click select / deferred deselect / marquee-begin / right-click preserves selection ----

TEST(TimelineClipLaneInteractionTest, ClickSelectsDeferredDeselect) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());

    const auto rect = f.lane.getClipRect(clipId);
    const auto clipCentre = centreOf(rect);

    // Click on the clip selects it — a plain mouseDown+mouseUp at the same position with no drag.
    f.lane.mouseDown(leftClick(f.lane, clipCentre));
    f.lane.mouseUp(leftClick(f.lane, clipCentre));
    ASSERT_TRUE(f.selection.contains(clipId));

    // A press on EMPTY space defers: it must not clear on mouseDown alone.
    const juce::Point<float> emptyA(900.0f, 300.0f);
    f.lane.mouseDown(leftClick(f.lane, emptyA));
    EXPECT_TRUE(f.selection.contains(clipId)) << "mouseDown alone must not clear — see the deferred-click contract";

    // ...but a press that turns into a DRAG promotes to a marquee (cancelling the deferred click).
    const juce::Point<float> emptyB(950.0f, 300.0f);
    f.lane.mouseDrag(leftDrag(f.lane, emptyB, emptyA));
    EXPECT_TRUE(f.lane.isMarqueeActiveForTest());
    f.lane.mouseUp(leftDrag(f.lane, emptyB, emptyA));
    EXPECT_FALSE(f.lane.isMarqueeActiveForTest());

    // A plain click-release (no drag) on empty space clears.
    f.selection.setSelection({clipId});
    f.lane.mouseDown(leftClick(f.lane, emptyA));
    f.lane.mouseUp(leftClick(f.lane, emptyA));
    EXPECT_TRUE(f.selection.isEmpty());

    // Right-click preserves the selection (the GraphEditor rule) — whether on a clip or on empty
    // lane space.
    f.selection.setSelection({clipId});
    f.lane.mouseDown(rightClick(f.lane, emptyA));
    f.lane.mouseUp(rightClick(f.lane, emptyA));
    EXPECT_TRUE(f.selection.contains(clipId));
}

TEST(TimelineClipLaneInteractionTest, MarqueeSelectsIntersecting) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto trackId2 = f.doc.addTrack(TrackKind::Midi, "Track 2");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");   // row 0, x in [0, 160)
    const auto clipB = f.doc.addClip(trackId2, 20.0, 4.0, "B"); // row 1, x in [800, 960) — far away
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());

    const auto rectA = f.lane.getClipRect(clipA);

    // Anchor sits just BELOW clipA's row (empty space — mouseDown there must start a marquee, not
    // select/drag the clip), and the drag reaches up and to the right so the resulting rect clips
    // clipA's bottom-right corner only.
    const juce::Point<float> anchor((float)rectA.getRight() - 10.0f, (float)rectA.getBottom() + 4.0f);
    const juce::Point<float> current((float)rectA.getRight() + 40.0f, (float)rectA.getY() - 4.0f);

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, current, anchor));
    ASSERT_TRUE(f.lane.isMarqueeActiveForTest());
    EXPECT_TRUE(f.selection.contains(clipA));
    EXPECT_FALSE(f.selection.contains(clipB)) << "clipB sits far outside the marquee band";
    f.lane.mouseUp(leftDrag(f.lane, current, anchor));

    // Additive (Shift-held) marquee starting from clipB's own selection preserves it.
    f.selection.setSelection({clipB});
    f.lane.mouseDown(leftClick(f.lane, anchor, juce::ModifierKeys::shiftModifier));
    f.lane.mouseDrag(leftDrag(f.lane, current, anchor, juce::ModifierKeys::shiftModifier));
    EXPECT_TRUE(f.selection.contains(clipA));
    EXPECT_TRUE(f.selection.contains(clipB)) << "additive marquee must keep the base selection";
    f.lane.mouseUp(leftDrag(f.lane, current, anchor, juce::ModifierKeys::shiftModifier));
}

// ---- Drag move: snapped, one undo step, multi-selection moves together ----

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

// ---- Split / Duplicate / Delete via the menu hook (showMenuAsync doesn't run headless) ----

TEST(TimelineClipLaneInteractionTest, SplitAtPointerViaMenuHook) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
    ASSERT_TRUE(clipId.isValid());

    // Strictly inside, Beat-snapped: 3.3 -> 3.0.
    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::SplitAtPointer, 3.3);

    const auto* track = f.doc.getTrackForClip(clipId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 1.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u);
}

TEST(TimelineClipLaneInteractionTest, SplitAtPointerRejectsOutsideTheClip) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
    ASSERT_TRUE(clipId.isValid());

    // 2.0 snaps to the clip's own start — not strictly inside.
    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::SplitAtPointer, 2.0);
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneInteractionTest, DuplicateViaMenuHook) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::Duplicate, 0.0);

    const auto* track = f.doc.getTrackForClip(clipId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    const auto duplicateId = track->clips[1].id;
    EXPECT_NE(duplicateId, clipId);
    EXPECT_DOUBLE_EQ(track->clips[1].startBeat, 6.0) << "appended immediately after the original";
    EXPECT_TRUE(f.selection.contains(duplicateId)) << "the new clip becomes the selection";
    EXPECT_FALSE(f.selection.contains(clipId));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u);
}

TEST(TimelineClipLaneInteractionTest, DeleteViaMenuHook) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());

    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::Delete, 0.0);
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 0u);
    ASSERT_TRUE(f.undo.canUndo());
}

// ---- Panel-scoped Delete key ----

TEST(TimelineClipLaneInteractionTest, DeleteKeyDeletesSelectionOneStep) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");
    const auto clipB = f.doc.addClip(trackId, 10.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());
    f.selection.setSelection({clipA, clipB});

    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 0u);
    EXPECT_TRUE(f.selection.isEmpty());

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 2u) << "both clips came back in ONE undo";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneInteractionTest, DeleteKeyWithEmptySelectionReturnsFalse) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(f.selection.isEmpty());
    EXPECT_FALSE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
}

TEST(TimelineClipLaneInteractionTest, EscapeKeyClearsSelection) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "A");
    f.selection.setSelection({clipId});

    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.selection.isEmpty());
    EXPECT_FALSE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))) << "nothing left to clear";
}

// ---- Doc-change pruning ----

TEST(TimelineClipLaneInteractionTest, DocChangeRefreshesAndPrunesSelection) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "A");
    f.selection.setSelection({clipId});
    ASSERT_TRUE(f.selection.contains(clipId));

    // A mutation straight on the doc (not through the lane area at all — e.g. the panel's own
    // TimelineDoc::Listener would call refreshFromDoc() on the next notification; this is that
    // call, exercised directly).
    ASSERT_TRUE(f.doc.removeClip(clipId));
    f.lane.refreshFromDoc();

    EXPECT_TRUE(f.selection.isEmpty());
}

// ---- Snapshot smoke ----

TEST(TimelineClipLaneInteractionTest, SnapshotSmoke) {
    ClipLaneFixture f;
    const auto trackA = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto trackB = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto clip1 = f.doc.addClip(trackA, 0.0, 4.0, "Beat 1");
    f.doc.addClip(trackA, 4.0, 4.0, "Beat 2");
    f.doc.addClip(trackB, 2.0, 6.0, "Bassline");
    f.doc.addNote(clip1, makeNote(0.0, 36));
    f.doc.addNote(clip1, makeNote(1.0, 40, 0.5));
    f.selection.setSelection({clip1});

    f.lane.setSize(1000, 160);
    const juce::Image img = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 1000);
    EXPECT_EQ(img.getHeight(), 160);
}

// ============================================================================
// 5. Waveform peaks, cache invalidation, and the live-recording strip
// ============================================================================

namespace {

bool imagesIdentical(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

struct ScopedPeaksFile {
    explicit ScopedPeaksFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedPeaksFile() { file.deleteFile(); }
    juce::File file;
};

synth::PeaksFile::Data makeWaveformData(int numBuckets, float amplitude) {
    synth::PeaksFile::Data data;
    data.bucketSize = 256;
    data.numChannels = 1;
    for (int i = 0; i < numBuckets; ++i)
        data.buckets.emplace_back(-amplitude, amplitude);
    return data;
}

// Polls RecordTapModule::copyLivePeaks() until it has at least `minPairs` entries, or gives up
// after a generous bound. The writer thread drains asynchronously (TimeSliceThread — see
// RecordTapModule's class comment), so growing its live peaks mid-capture is inherently a real
// background-thread hand-off, not something this test can force synchronously; the bound is wide
// enough that a slow CI box does not make this flaky in practice.
bool waitForLivePeaks(const RecordTapModule& tap, size_t minPairs, std::vector<std::pair<float, float>>& out) {
    constexpr int kMaxWaitMs = 2000;
    constexpr int kStepMs = 5;
    for (int waited = 0; waited <= kMaxWaitMs; waited += kStepMs) {
        tap.copyLivePeaks(out);
        if (out.size() >= minPairs)
            return true;
        juce::Thread::sleep(kStepMs);
    }
    return false;
}

} // namespace

TEST(TimelineClipLaneWaveformTest, AudioClipPaintsWaveform) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Take"); // 20 beats * 40 px/beat = wide
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clipId, "Audio/take-1.wav", 0.0));
    f.lane.setSize(1000, 200);

    // Baseline: no resolver installed at all, so paintWaveform() has nothing to draw from.
    const juce::Image withoutPeaks = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    ScopedPeaksFile peaksFile("agentsynth_clipslane_waveform.agpk");
    ASSERT_TRUE(synth::PeaksFile::write(peaksFile.file, makeWaveformData(400, 0.8f)));

    // Resolves unconditionally to the synthetic file, regardless of the ref text it's handed.
    const juce::File resolved = peaksFile.file;
    f.lane.setPeaksResolver([resolved](const juce::String&) { return resolved; });
    const juce::Image withPeaks = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    ASSERT_FALSE(withoutPeaks.isNull());
    ASSERT_FALSE(withPeaks.isNull());
    EXPECT_FALSE(imagesIdentical(withoutPeaks, withPeaks))
        << "installing a resolver with real peaks data must change the painted pixels";
}

TEST(TimelineClipLaneWaveformTest, CacheInvalidation) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Take");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clipId, "Audio/take-1.wav", 0.0));
    f.lane.setSize(1000, 200);

    ScopedPeaksFile fileA("agentsynth_clipslane_cache_a.agpk");
    ScopedPeaksFile fileB("agentsynth_clipslane_cache_b.agpk");
    ASSERT_TRUE(synth::PeaksFile::write(fileA.file, makeWaveformData(50, 0.1f)));
    ASSERT_TRUE(synth::PeaksFile::write(fileB.file, makeWaveformData(400, 0.9f)));

    juce::File current = fileA.file;
    f.lane.setPeaksResolver([&current](const juce::String&) { return current; });
    const juce::Image first = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    // Swap the file the resolver would now return, WITHOUT invalidating: paint() must keep
    // serving the cached (stale) data.
    current = fileB.file;
    const juce::Image stillCached = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_TRUE(imagesIdentical(first, stillCached)) << "the cache must not silently re-resolve every paint";

    f.lane.invalidatePeaksCache();
    const juce::Image afterInvalidate = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_FALSE(imagesIdentical(first, afterInvalidate)) << "invalidation must force a fresh resolve + read";
}

TEST(TimelineClipLaneLiveRecordingTest, LiveStripGrowsOnNewBucketsOnly) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(f.doc.setTrackArmed(trackId, true));
    f.lane.setSize(2000, 400);

    const auto wavFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_clipslane_livestrip.wav");
    const auto peaksFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_clipslane_livestrip.agpk");
    wavFile.deleteFile();
    peaksFile.deleteFile();

    constexpr double kSampleRate = 48000.0;
    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, 256);
    ASSERT_TRUE(tap.startCapture(wavFile, peaksFile, kSampleRate, 2));

    synth::ui::TimelineClipLaneArea::LiveRecordingInfo info;
    info.active = true;
    info.track = trackId;
    info.punchBeat = 0.0;
    info.currentBeat = 4.0;
    info.tap = &tap;

    juce::AudioBuffer<float> buffer(2, 256); // exactly one peak bucket
    juce::MidiBuffer midi;
    buffer.clear();

    tap.processBlock(buffer, midi);
    std::vector<std::pair<float, float>> peaks;
    ASSERT_TRUE(waitForLivePeaks(tap, 2, peaks)) << "one full bucket (both channels) must have flushed by now";

    f.lane.updateLiveRecording(info); // first frame: always a repaint
    const int afterFirst = f.lane.getLiveStripRepaintCountForTest();
    EXPECT_GT(afterFirst, 0);

    // Same info again, no new samples pushed: bucket count is unchanged -> no additional repaint.
    f.lane.updateLiveRecording(info);
    EXPECT_EQ(f.lane.getLiveStripRepaintCountForTest(), afterFirst)
        << "an unchanged bucket count must not trigger a repaint";

    // Push another full bucket and wait for it to flush -> a repaint.
    tap.processBlock(buffer, midi);
    ASSERT_TRUE(waitForLivePeaks(tap, 4, peaks));
    f.lane.updateLiveRecording(info);
    EXPECT_GT(f.lane.getLiveStripRepaintCountForTest(), afterFirst) << "new buckets must trigger a repaint";

    tap.stopCapture();
    wavFile.deleteFile();
    peaksFile.deleteFile();
}

TEST(TimelineClipLaneWaveformTest, SourceOffsetShiftsWaveform) {
    using synth::ui::TimelineClipLaneArea;

    // sampleRate chosen so 1 second is an EXACT whole number of buckets (25600 / 256 = 100),
    // which keeps the expected shift/length arithmetic exact rather than off-by-one from floor/
    // ceil rounding at a non-bucket-aligned sample offset.
    constexpr double kSampleRate = 25600.0;
    constexpr double kBpm = 120.0;
    constexpr double kLengthBeats = 4.0; // 2 seconds at 120 bpm

    const auto data = makeWaveformData(400, 1.0f);

    const auto atZero = TimelineClipLaneArea::bucketRangeForClip(data, kLengthBeats, 0.0, kBpm, kSampleRate);
    const auto atOneSecond = TimelineClipLaneArea::bucketRangeForClip(data, kLengthBeats, 1.0, kBpm, kSampleRate);

    ASSERT_GT(atZero.bucketCount, 0);
    constexpr int kExpectedShiftBuckets = 100; // 1 second * 25600 Hz / 256 samples-per-bucket
    EXPECT_EQ(atOneSecond.firstBucket, atZero.firstBucket + kExpectedShiftBuckets);
    EXPECT_EQ(atOneSecond.bucketCount, atZero.bucketCount)
        << "same clip length -> same bucket-window size, just shifted";

    // Empty peaks data yields a zero-length range rather than an out-of-bounds one.
    synth::PeaksFile::Data empty;
    empty.bucketSize = 256;
    empty.numChannels = 1;
    const auto emptyRange = TimelineClipLaneArea::bucketRangeForClip(empty, kLengthBeats, 0.0, kBpm, kSampleRate);
    EXPECT_EQ(emptyRange.bucketCount, 0);
}

// ============================================================================
// 6. Missing-asset placeholder
// ============================================================================

TEST(TimelineClipLaneWaveformTest, MissingAssetPaintsPlaceholder) {
    int missingResolverCalls = 0;

    const juce::Image missingImage = [&] {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Missing");
        // EXPECT rather than ASSERT: this lambda returns a value, and ASSERT_* expands to a bare
        // `return;` on failure, which cannot coexist with a non-void return type.
        EXPECT_TRUE(clipId.isValid());
        EXPECT_TRUE(f.doc.setClipAsset(clipId, "Audio/ghost.wav", 0.0));
        f.lane.setAssetExistsResolver([&](const juce::String&) {
            ++missingResolverCalls;
            return false;
        });
        f.lane.setSize(1000, 200);

        // Paint TWICE — the existence answer must be cached per assetRef, not re-queried every
        // paint (the same contract the peaks cache already has).
        const auto first = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
        const auto second = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
        EXPECT_TRUE(imagesIdentical(first, second));
        return second;
    }();
    EXPECT_EQ(missingResolverCalls, 1) << "the existence check must be cached per assetRef";

    const juce::Image presentImage = [] {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Present");
        EXPECT_TRUE(clipId.isValid());
        EXPECT_TRUE(f.doc.setClipAsset(clipId, "Audio/real.wav", 0.0));

        ScopedPeaksFile peaksFile("agentsynth_clipslane_missing_placeholder.agpk");
        EXPECT_TRUE(synth::PeaksFile::write(peaksFile.file, makeWaveformData(400, 0.8f)));
        const juce::File resolved = peaksFile.file;
        f.lane.setPeaksResolver([resolved](const juce::String&) { return resolved; });
        f.lane.setAssetExistsResolver([](const juce::String&) { return true; });
        f.lane.setSize(1000, 200);
        return f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    }();

    const juce::Image midiImage = [] {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Midi 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Notes");
        EXPECT_TRUE(clipId.isValid());
        f.doc.addNote(clipId, makeNote(0.0, 60));
        f.lane.setSize(1000, 200);
        return f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    }();

    ASSERT_FALSE(missingImage.isNull());
    ASSERT_FALSE(presentImage.isNull());
    ASSERT_FALSE(midiImage.isNull());
    EXPECT_FALSE(imagesIdentical(missingImage, presentImage))
        << "the placeholder must not look like a normal waveform clip";
    EXPECT_FALSE(imagesIdentical(missingImage, midiImage))
        << "the placeholder must not look like a plain MIDI (no-asset) clip";
}

TEST(TimelineClipLaneWaveformTest, NoResolverInstalledAssumesAssetExists) {
    // Degrade-gracefully contract: without setAssetExistsResolver ever being called, paint() must
    // NOT draw a placeholder — existing callers (and existing snapshot tests) that never wire this
    // resolver must see byte-identical output.
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Take");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clipId, "Audio/take-1.wav", 0.0));
    f.lane.setSize(1000, 200);

    EXPECT_TRUE(f.lane.getClipRect(clipId).getWidth() > 24) << "sanity: the clip must be wide enough to paint into";
    // No crash, no assertion failure — the absence of a resolver is itself the thing under test.
    const auto snapshot = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_FALSE(snapshot.isNull());
}
