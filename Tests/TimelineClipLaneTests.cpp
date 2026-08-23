// TimelineClipLaneTests.cpp
//
// Clip lanes — drag/trim/split/duplicate + marquee selection, backed by
// synth::ui::ClipSelectionModel.
//
// Seven groups:
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
//   7. Authoring gestures — double-click empty lane space (a one-bar MIDI clip that opens the note
//      editor; an audio row's injected file chooser), OS file drag/drop onto an audio row, and the
//      empty-row hint line. The IMPORT half of the audio gestures is MainComponent's, and lives in
//      Tests/AssetManagerTests.cpp beside the relink flow it mirrors.

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

    // Horizontal scroll (firstVisibleBeat) shifts x, never y.
    state.firstVisibleBeat = 1.0;
    const auto scrolled = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);
    EXPECT_EQ(scrolled.getY(), rect.getY());
    EXPECT_LT(scrolled.getX(), rect.getX());

    // Vertical track scroll (trackScrollY) shifts y, never x.
    state.trackScrollY = 30.0;
    const auto vScrolled = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);
    EXPECT_EQ(vScrolled.getX(), scrolled.getX());
    EXPECT_EQ(vScrolled.getY(), 2 * rowHeight - 30);
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
        state.snap = TimelineViewState::Snap::Quarter;
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

// ---- Panel-scoped "P" = loop the selection ----

TEST(TimelineClipLaneInteractionTest, LoopSelectionKeyReportsTheSelectionSpan) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 4.0, "A");
    ASSERT_TRUE(clipId.isValid());

    double reportedStart = -1.0, reportedEnd = -1.0;
    int calls = 0;
    f.lane.onLoopRangeRequested = [&](double start, double end) {
        reportedStart = start;
        reportedEnd = end;
        ++calls;
    };

    f.selection.setSelection({clipId});
    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress('p')));
    EXPECT_EQ(calls, 1);
    EXPECT_DOUBLE_EQ(reportedStart, 4.0);
    EXPECT_DOUBLE_EQ(reportedEnd, 8.0) << "the span is [startBeat, startBeat + lengthBeats]";

    // The doc is untouched: this reports a range outwards, it never edits anything.
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneInteractionTest, LoopSelectionKeySpansEverySelectedClip) {
    ClipLaneFixture f;
    const auto trackA = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto trackB = f.doc.addTrack(TrackKind::Midi, "Track 2");
    const auto early = f.doc.addClip(trackB, 2.0, 1.0, "early"); // earliest start, on the LATER row
    const auto late = f.doc.addClip(trackA, 16.0, 4.0, "late");  // latest end
    const auto middle = f.doc.addClip(trackA, 8.0, 2.0, "middle");
    const auto unselected = f.doc.addClip(trackB, 40.0, 4.0, "unselected");
    ASSERT_TRUE(unselected.isValid());

    double reportedStart = -1.0, reportedEnd = -1.0;
    f.lane.onLoopRangeRequested = [&](double start, double end) {
        reportedStart = start;
        reportedEnd = end;
    };

    f.selection.setSelection({late, early, middle});
    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress('p')));
    EXPECT_DOUBLE_EQ(reportedStart, 2.0) << "min startBeat across the whole selection, any row";
    EXPECT_DOUBLE_EQ(reportedEnd, 20.0) << "max endBeat — a clip outside the selection is ignored";
}

TEST(TimelineClipLaneInteractionTest, LoopSelectionKeyWithEmptySelectionReturnsFalse) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.doc.addClip(trackId, 0.0, 4.0, "A");

    int calls = 0;
    f.lane.onLoopRangeRequested = [&](double, double) { ++calls; };

    ASSERT_TRUE(f.selection.isEmpty());
    EXPECT_FALSE(f.lane.keyPressed(juce::KeyPress('p'))) << "nothing selected: the key must bubble";
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(f.lane.getSelectedClipSpan().has_value());
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
// 4b. Edge auto-scroll: the pure synth::ui::edgeScrollVelocity helper (EdgeAutoScroll.h), the
//     beat-anchored drag mapping it exists to keep correct across a mid-drag view scroll, and the
//     gated timer that drives it. Backfilled for the already-landed implementation — see
//     Source/UI/EdgeAutoScroll.h and TimelineClipLaneArea's mouseDownBeat_/lastDragPointer_/
//     autoScrollTick() comments.
// ============================================================================

// ---- synth::ui::edgeScrollVelocity — pure, no component/doc state ----

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

// ============================================================================
// 7. Authoring gestures — double-click empty space, OS file drop, empty-row hint
// ============================================================================

namespace {

// The one y that lands on row `index` regardless of the themed/headless row height.
float rowCentreY(const TimelineClipLaneArea& lane, int index) {
    const int rowHeight = lane.getRowHeight();
    return (float)(index * rowHeight + rowHeight / 2);
}

} // namespace

TEST(TimelineClipLaneAuthoringTest, DoubleClickEmptyMidiRowCreatesOneBarClipAndOpensIt) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar; // 4 beats (no transport) = 160 px at 40 px/beat
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");

    std::vector<ClipId> opened;
    f.lane.onClipDoubleClicked = [&opened](ClipId id) { opened.push_back(id); };

    // x = 200 px -> beat 5.0, which FLOORS to bar 2 (beat 4.0) rather than snapping forward to 8.
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 0)}));

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 1u);
    const auto& clip = track->clips[0];
    EXPECT_DOUBLE_EQ(clip.startBeat, 4.0) << "the snap grid line at or BEFORE the click, never after it";
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0) << "one bar at the default 4/4";
    EXPECT_EQ(clip.name, juce::String("Clip 1"));
    EXPECT_TRUE(clip.assetRef.isEmpty()) << "a MIDI clip carries notes, not an asset";
    EXPECT_TRUE(f.selection.contains(clip.id)) << "the new clip is selected";
    ASSERT_EQ(opened.size(), 1u) << "the same open-piano-roll path a clip double-click uses must fire";
    EXPECT_EQ(opened[0], clip.id);

    // ONE undo step, and undoing removes the clip entirely.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getTrack(trackId)->clips.empty());
    EXPECT_FALSE(f.undo.canUndo()) << "creating a clip was ONE undo step";
}

TEST(TimelineClipLaneAuthoringTest, DoubleClickNamesClipsInSequenceAndSnapOffKeepsTheRawBeat) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Off;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");

    f.lane.mouseDoubleClick(leftClick(f.lane, {80.0f, rowCentreY(f.lane, 0)}));  // beat 2.0
    f.lane.mouseDoubleClick(leftClick(f.lane, {400.0f, rowCentreY(f.lane, 0)})); // beat 10.0

    const auto* track = f.doc.getTrack(trackId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    EXPECT_DOUBLE_EQ(track->clips[0].startBeat, 2.0) << "Snap::Off passes the raw beat through";
    EXPECT_DOUBLE_EQ(track->clips[1].startBeat, 10.0);
    EXPECT_EQ(track->clips[1].name, juce::String("Clip 2")) << "the auto-name counts the row's clips";
}

TEST(TimelineClipLaneAuthoringTest, DoubleClickIgnoresAutomationRowsAndEmptyPanelSpace) {
    ClipLaneFixture f;
    const auto automationTrack = f.doc.addTrack(TrackKind::Automation, "Automation");
    ASSERT_TRUE(automationTrack.isValid());

    int opens = 0;
    f.lane.onClipDoubleClicked = [&opens](ClipId) { ++opens; };

    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 0)}));
    EXPECT_TRUE(f.doc.getTrack(automationTrack)->clips.empty()) << "an automation row authors nothing";

    // Below the last row: panel space, not a row.
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 4)}));
    EXPECT_EQ(f.doc.getTrack(automationTrack)->clips.size(), 0u);

    EXPECT_EQ(opens, 0);
    EXPECT_FALSE(f.undo.canUndo()) << "neither gesture may create an undo step";
}

TEST(TimelineClipLaneAuthoringTest, DoubleClickEmptyAudioRowAsksForAFileAndReportsTheChoice) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(midiTrack.isValid());

    const juce::File fixture =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_clipslane_chooser.wav");

    // The injected chooser stands in for juce::FileChooser, which never runs in a test process —
    // same idiom as applyClipContextChoice standing in for showMenuAsync.
    int chooserCalls = 0;
    f.lane.setAudioFileChooser([&](std::function<void(const juce::File&)> onChosen) {
        ++chooserCalls;
        onChosen(fixture);
    });

    struct Report {
        synth::TrackId track;
        double beat = 0.0;
        juce::File file;
    };
    std::vector<Report> reports;
    f.lane.onAudioFileDropped = [&reports](synth::TrackId track, double beat, juce::File file) {
        reports.push_back({track, beat, file});
    };

    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 1)}));

    EXPECT_EQ(chooserCalls, 1);
    ASSERT_EQ(reports.size(), 1u) << "the lane area reports the choice; the OWNER imports it";
    EXPECT_EQ(reports[0].track, audioTrack);
    EXPECT_DOUBLE_EQ(reports[0].beat, 4.0) << "the same floor-snapped beat a MIDI clip would start on";
    EXPECT_EQ(reports[0].file, fixture);
    EXPECT_TRUE(f.doc.getTrack(audioTrack)->clips.empty()) << "the lane area never creates the audio clip itself";
    EXPECT_FALSE(f.undo.canUndo());

    // A cancelled dialog reports nothing.
    f.lane.setAudioFileChooser([](std::function<void(const juce::File&)> onChosen) { onChosen(juce::File()); });
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 1)}));
    EXPECT_EQ(reports.size(), 1u);

    // A double-click on the MIDI row still creates a clip rather than asking for a file.
    f.lane.mouseDoubleClick(leftClick(f.lane, {200.0f, rowCentreY(f.lane, 0)}));
    EXPECT_EQ(chooserCalls, 1);
    EXPECT_EQ(f.doc.getTrack(midiTrack)->clips.size(), 1u);
}

TEST(TimelineClipLaneAuthoringTest, FileDragInterestIsExtensionBased) {
    ClipLaneFixture f;
    EXPECT_TRUE(f.lane.isInterestedInFileDrag({"/tmp/loop.wav"}));
    EXPECT_TRUE(f.lane.isInterestedInFileDrag({"/tmp/notes.txt", "/tmp/loop.aiff"}))
        << "at least one readable audio file is enough";
    EXPECT_FALSE(f.lane.isInterestedInFileDrag({"/tmp/notes.txt"}));
    EXPECT_FALSE(f.lane.isInterestedInFileDrag({}));
}

TEST(TimelineClipLaneAuthoringTest, FileDragHighlightsAudioRowsOnly) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(audioTrack.isValid());
    const juce::StringArray dragged{"/tmp/loop.wav"};

    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1);

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), 1) << "the audio row under the cursor highlights";

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 0));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "a MIDI row never highlights";

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    f.lane.fileDragMove({"/tmp/notes.txt"}, 200, (int)rowCentreY(f.lane, 1));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "nothing droppable, nothing highlighted";

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 5)); // below the last row
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1);

    f.lane.fileDragMove(dragged, 200, (int)rowCentreY(f.lane, 1));
    f.lane.fileDragExit(dragged);
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "leaving the component clears the highlight";
}

TEST(TimelineClipLaneAuthoringTest, FilesDroppedReportsTrackAndSnappedBeatForAudioRowsOnly) {
    ClipLaneFixture f;
    f.state.snap = TimelineViewState::Snap::Bar;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(midiTrack.isValid());

    struct Report {
        synth::TrackId track;
        double beat = 0.0;
        juce::File file;
    };
    std::vector<Report> reports;
    f.lane.onAudioFileDropped = [&reports](synth::TrackId track, double beat, juce::File file) {
        reports.push_back({track, beat, file});
    };

    // Two files dropped together: the FIRST readable audio one wins, and the .txt is ignored.
    f.lane.fileDragMove({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 1));
    f.lane.filesDropped({"/tmp/notes.txt", "/tmp/loop.wav", "/tmp/second.wav"}, 200, (int)rowCentreY(f.lane, 1));

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].track, audioTrack);
    EXPECT_DOUBLE_EQ(reports[0].beat, 4.0) << "x = 200 px -> beat 5.0, floored onto the bar grid";
    EXPECT_EQ(reports[0].file, juce::File("/tmp/loop.wav"));
    EXPECT_EQ(f.lane.getFileDropRowForTest(), -1) << "the drop clears the highlight";

    // A MIDI row, an automation row and empty panel space all refuse the drop.
    f.lane.filesDropped({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 0));
    f.lane.filesDropped({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 7));
    EXPECT_EQ(reports.size(), 1u);
    EXPECT_TRUE(f.doc.getTrack(midiTrack)->clips.empty());
}

TEST(TimelineClipLaneAuthoringTest, EmptyRowHintTextIsPerKindAndOnlyForEmptyRows) {
    ClipLaneFixture f;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    f.doc.addTrack(TrackKind::Automation, "Automation");
    ASSERT_TRUE(audioTrack.isValid());

    EXPECT_EQ(f.lane.getEmptyRowHintForTest(0),
              juce::String::fromUTF8("Double-click to add a clip \xE2\x80\x94 or arm (R) and record"));
    EXPECT_EQ(f.lane.getEmptyRowHintForTest(1),
              juce::String::fromUTF8("Drop an audio file \xE2\x80\x94 or arm (R) and record"));
    EXPECT_TRUE(f.lane.getEmptyRowHintForTest(2).isEmpty()) << "an automation row has nothing to author";
    EXPECT_TRUE(f.lane.getEmptyRowHintForTest(9).isEmpty()) << "no such row";

    // A row with ANY clip on it stops hinting.
    ASSERT_TRUE(f.doc.addClip(midiTrack, 0.0, 4.0, "Clip 1").isValid());
    EXPECT_TRUE(f.lane.getEmptyRowHintForTest(0).isEmpty());
    EXPECT_FALSE(f.lane.getEmptyRowHintForTest(1).isEmpty()) << "the audio row is still empty";
}

TEST(TimelineClipLaneAuthoringTest, EmptyRowHintIsPaintedAndDroppedWhenTooNarrow) {
    // Both fixtures paint ONE empty row at the same size, differing only in track kind — so any
    // pixel difference is the hint line itself (an automation row never hints).
    const auto imageFor = [](TrackKind kind, int width) {
        ClipLaneFixture f;
        f.doc.addTrack(kind, "Row");
        f.lane.setSize(width, f.lane.getRowHeight());
        return f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    };

    const auto wideMidi = imageFor(TrackKind::Midi, 1000);
    const auto wideAutomation = imageFor(TrackKind::Automation, 1000);
    ASSERT_FALSE(wideMidi.isNull());
    EXPECT_FALSE(imagesIdentical(wideMidi, wideAutomation)) << "an empty MIDI row must paint its hint line";

    // Too narrow for the line plus its padding: dropped entirely rather than truncated, so the row
    // paints exactly like the (never-hinting) automation one.
    const auto narrowMidi = imageFor(TrackKind::Midi, 90);
    const auto narrowAutomation = imageFor(TrackKind::Automation, 90);
    ASSERT_FALSE(narrowMidi.isNull());
    EXPECT_TRUE(imagesIdentical(narrowMidi, narrowAutomation)) << "a row too narrow to read must not paint the hint";
}

TEST(TimelineClipLaneAuthoringTest, FileDropHighlightPaints) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Audio, "Audio 1");
    f.lane.setSize(1000, f.lane.getRowHeight());

    const auto before = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    f.lane.fileDragMove({"/tmp/loop.wav"}, 200, (int)rowCentreY(f.lane, 0));
    ASSERT_EQ(f.lane.getFileDropRowForTest(), 0);
    const auto during = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    f.lane.fileDragExit({"/tmp/loop.wav"});
    const auto after = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    EXPECT_FALSE(imagesIdentical(before, during)) << "the hovered audio row must be visibly marked";
    EXPECT_TRUE(imagesIdentical(before, after)) << "and the mark must be gone once the drag leaves";
}
