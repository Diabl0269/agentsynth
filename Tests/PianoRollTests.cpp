// PianoRollTests.cpp
//
// TL5-8: minimal piano-roll editor inside the timeline panel — draw/move/resize/delete, velocity
// scrub, quantise. Mirrors TimelineClipLaneTests.cpp's structure and harness style:
//   1. synth::ui::NoteSelectionModel — mirrors ClipSelectionModelTests.cpp's coverage, keyed on
//      synth::NoteId.
//   2. synth::ui::noteHitTestMarquee — mirrors clipHitTestMarquee's coverage.
//   3. synth::ui::PianoRollComponent — open/close lifecycle, draw/move/resize/delete/velocity-
//      scrub/quantise, clip-window clamping, pitch-scroll clamping, the shared absolute-beat
//      mapping, and a snapshot smoke test. Driven by hand-built juce::MouseEvents against a bare
//      TimelineDoc + AppUndoManager + PianoRollComponent — no TimelinePanelComponent needed.

#include "../Source/AppUndoManager.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/NoteSelectionModel.h"
#include "../Source/UI/PianoRollComponent.h"
#include "../Source/UI/TimelineViewState.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ClipId;
using synth::NoteId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::NoteSelectionModel;
using synth::ui::PianoRollComponent;
using synth::ui::TimelineViewState;

namespace {
NoteId nid(std::int64_t v) { return NoteId{v}; }

synth::MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0) {
    synth::MidiNote note;
    note.startBeat = startBeat;
    note.pitch = pitch;
    note.lengthBeats = lengthBeats;
    return note;
}
} // namespace

// ============================================================================
// 1. NoteSelectionModel
// ============================================================================

TEST(NoteSelectionModel, StartsEmpty) {
    NoteSelectionModel sel;
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_EQ(sel.size(), 0);
    EXPECT_FALSE(sel.contains(nid(1)));
}

TEST(NoteSelectionModel, AddIsIdempotent) {
    NoteSelectionModel sel;
    EXPECT_TRUE(sel.add(nid(7)));
    EXPECT_FALSE(sel.add(nid(7))) << "adding an already-selected id must report no change";
    EXPECT_EQ(sel.size(), 1);
    EXPECT_TRUE(sel.contains(nid(7)));
}

TEST(NoteSelectionModel, RejectsInvalidNoteIdZero) {
    NoteSelectionModel sel;
    EXPECT_FALSE(sel.add(nid(0)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(NoteSelectionModel, RemoveReportsWhetherAnythingWasRemoved) {
    NoteSelectionModel sel;
    sel.add(nid(3));
    EXPECT_TRUE(sel.remove(nid(3)));
    EXPECT_FALSE(sel.remove(nid(3)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(NoteSelectionModel, ToggleReturnsStateAfterToggling) {
    NoteSelectionModel sel;
    EXPECT_TRUE(sel.toggle(nid(2))) << "toggling an unselected id selects it";
    EXPECT_TRUE(sel.contains(nid(2)));
    EXPECT_FALSE(sel.toggle(nid(2))) << "toggling a selected id deselects it";
    EXPECT_FALSE(sel.contains(nid(2)));
}

TEST(NoteSelectionModel, SetSelectionReplacesAndDeduplicates) {
    NoteSelectionModel sel;
    sel.add(nid(99));
    sel.setSelection({nid(1), nid(2), nid(2), nid(0)});

    EXPECT_EQ(sel.size(), 2) << "duplicates collapse and the invalid id is dropped";
    EXPECT_TRUE(sel.contains(nid(1)));
    EXPECT_TRUE(sel.contains(nid(2)));
    EXPECT_FALSE(sel.contains(nid(99))) << "setSelection replaces rather than merges";
}

TEST(NoteSelectionModel, GetSelectedIsOrderedByValueRegardlessOfInsertionOrder) {
    NoteSelectionModel a;
    a.setSelection({nid(30), nid(10), nid(20)});
    NoteSelectionModel b;
    b.setSelection({nid(10), nid(20), nid(30)});

    auto expected = std::vector<NoteId>{nid(10), nid(20), nid(30)};
    EXPECT_EQ(a.getSelected(), expected);
    EXPECT_EQ(b.getSelected(), expected);
}

TEST(NoteSelectionModel, ClearEmptiesEverything) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2)});
    sel.clear();
    EXPECT_TRUE(sel.isEmpty());
}

TEST(NoteSelectionModel, RetainOnlyDropsIdsThatNoLongerExist) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2), nid(3)});

    EXPECT_TRUE(sel.retainOnly({nid(1), nid(3)}));
    EXPECT_EQ(sel.size(), 2);
    EXPECT_TRUE(sel.contains(nid(1)));
    EXPECT_FALSE(sel.contains(nid(2)));
    EXPECT_TRUE(sel.contains(nid(3)));
}

TEST(NoteSelectionModel, RetainOnlyReportsNoChangeWhenEverythingSurvives) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2)});
    EXPECT_FALSE(sel.retainOnly({nid(1), nid(2), nid(5)}));
    EXPECT_EQ(sel.size(), 2);
}

TEST(NoteSelectionModel, RetainOnlyWithNothingAliveClearsSelection) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2)});
    EXPECT_TRUE(sel.retainOnly({}));
    EXPECT_TRUE(sel.isEmpty());
}

// ============================================================================
// 2. noteHitTestMarquee (named NoteSelectionMarqueeTest so it stays under the "NoteSelection*"
//    gtest filter, alongside the model tests above)
// ============================================================================

namespace {
std::vector<std::pair<NoteId, juce::Rectangle<int>>> threeNoteRects() {
    return {
        {nid(1), juce::Rectangle<int>(0, 0, 40, 10)},
        {nid(2), juce::Rectangle<int>(100, 0, 40, 10)},
        {nid(3), juce::Rectangle<int>(200, 200, 40, 10)},
    };
}
} // namespace

TEST(NoteSelectionMarqueeTest, SelectsFullyEnclosedNotes) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(-10, -10, 160, 30), threeNoteRects());
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], nid(1));
    EXPECT_EQ(hits[1], nid(2));
}

TEST(NoteSelectionMarqueeTest, SelectsPartiallyTouchedNotes) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(30, 5, 20, 20), threeNoteRects());
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], nid(1));
}

TEST(NoteSelectionMarqueeTest, MissesNotesOutsideTheBand) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(50, 50, 20, 20), threeNoteRects());
    EXPECT_TRUE(hits.empty());
}

TEST(NoteSelectionMarqueeTest, DegenerateMarqueeSelectsNothing) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(10, 5, 0, 0), threeNoteRects());
    EXPECT_TRUE(hits.empty());
}

TEST(NoteSelectionMarqueeTest, IgnoresInvalidNoteIds) {
    std::vector<std::pair<NoteId, juce::Rectangle<int>>> rects{{nid(0), juce::Rectangle<int>(0, 0, 100, 100)}};
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(0, 0, 200, 200), rects);
    EXPECT_TRUE(hits.empty());
}

TEST(NoteSelectionMarqueeTest, EmptyListYieldsNoHits) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(0, 0, 500, 500), {});
    EXPECT_TRUE(hits.empty());
}

// ============================================================================
// 3. PianoRollComponent
// ============================================================================

namespace {

struct PianoRollFixture {
    TimelineDoc doc;
    TimelineViewState state;
    AppUndoManager undo;
    PianoRollComponent roll{state};

    PianoRollFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Beat;
        roll.setTimelineDoc(&doc);
        roll.setUndoManager(&undo);
        roll.setSize(900, 160);
    }
};

juce::MouseEvent makeRollMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                    bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

juce::MouseEvent leftClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeRollMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                              pos);
}

juce::MouseEvent leftDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                          int extraFlags = 0) {
    return makeRollMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), true,
                              anchor);
}

juce::Point<float> centreOf(juce::Rectangle<int> rect) { return {(float)rect.getCentreX(), (float)rect.getCentreY()}; }

} // namespace

// ---- Open/close lifecycle ----

TEST(PianoRollLifecycleTest, OpenCloseLifecycle) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());

    bool closeRequested = false;
    f.roll.onCloseRequested = [&] { closeRequested = true; };

    // The double-click hook (TimelineClipLaneArea::onClipDoubleClicked) forwards straight to
    // openClip via the panel's openPianoRoll — exercised directly here.
    f.roll.openClip(clipId);
    EXPECT_TRUE(f.roll.isOpen());
    EXPECT_EQ(f.roll.getClipId(), clipId);

    // Back button closes, and notifies the owner.
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getBackButtonBounds())));
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);

    // Escape closes when nothing is selected.
    closeRequested = false;
    f.roll.openClip(clipId);
    ASSERT_TRUE(f.roll.isOpen());
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);

    // Deleting the edited clip from the doc closes the roll (refreshFromDoc's own contract).
    closeRequested = false;
    f.roll.openClip(clipId);
    ASSERT_TRUE(f.roll.isOpen());
    ASSERT_TRUE(f.doc.removeClip(clipId));
    f.roll.refreshFromDoc();
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);
}

TEST(PianoRollLifecycleTest, EscapeWithSelectionClearsFirstRatherThanClosing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60));
    f.roll.openClip(clipId);
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_TRUE(f.roll.isOpen()) << "first Escape only clears the selection";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isOpen()) << "second Escape (nothing selected) closes the roll";
}

// ---- Draw: pencil-by-default, snapped, one undo step ----

TEST(PianoRollEditingTest, DrawNoteSnappedOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.roll.openClip(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;                    // a row comfortably inside the grid
    const juce::Point<float> anchor(84.0f, (float)f.roll.yForPitch(pitch) + 5.0f); // beat 2.1 -> snaps to 2.0
    const juce::Point<float> dragged(136.0f, anchor.y);                            // beat 3.4 -> snaps to 3.0

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    const auto& note = clip->notes[0];
    EXPECT_DOUBLE_EQ(note.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(note.lengthBeats, 1.0);
    EXPECT_EQ(note.pitch, pitch);
    EXPECT_EQ(note.velocity, 100);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole draw was ONE undo step";
}

// ---- Move (multi-selection, together) and right-edge resize ----

TEST(PianoRollEditingTest, MoveAndResize) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.roll.openClip(clipId);

    const auto id1 = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    const auto id2 = f.doc.addNote(clipId, makeNote(6.0, 64, 1.0));
    ASSERT_TRUE(id1.isValid());
    ASSERT_TRUE(id2.isValid());
    f.roll.getSelectionForTest().setSelection({id1, id2});

    // Grab id1's body, drag +1 beat and up 2 semitones — both notes move together.
    const auto rect1 = f.roll.getNoteRect(id1);
    const juce::Point<float> anchor((float)rect1.getCentreX(), (float)rect1.getCentreY());
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y - 20.0f);

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* note1 = f.doc.getNote(id1);
    const auto* note2 = f.doc.getNote(id2);
    ASSERT_NE(note1, nullptr);
    ASSERT_NE(note2, nullptr);
    EXPECT_DOUBLE_EQ(note1->startBeat, 3.0);
    EXPECT_EQ(note1->pitch, 62);
    EXPECT_DOUBLE_EQ(note2->startBeat, 7.0) << "same shared delta, moved together";
    EXPECT_EQ(note2->pitch, 66);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id1)->startBeat, 2.0);
    EXPECT_EQ(f.doc.getNote(id1)->pitch, 60);
    EXPECT_FALSE(f.undo.canUndo()) << "the multi-note move was ONE undo step";

    // Right-edge resize: grabs only the single note, even inside a wider selection.
    const auto rect1b = f.roll.getNoteRect(id1);
    const juce::Point<float> edge((float)rect1b.getRight() - 2.0f, (float)rect1b.getCentreY());
    const juce::Point<float> draggedEdge(edge.x + 40.0f, edge.y); // +1 beat

    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, draggedEdge, edge));
    f.roll.mouseUp(leftDrag(f.roll, draggedEdge, edge));

    const auto* resized1 = f.doc.getNote(id1);
    const auto* untouched2 = f.doc.getNote(id2);
    ASSERT_NE(resized1, nullptr);
    EXPECT_DOUBLE_EQ(resized1->startBeat, 2.0) << "resize never moves the start";
    EXPECT_DOUBLE_EQ(resized1->lengthBeats, 2.0);
    EXPECT_DOUBLE_EQ(untouched2->lengthBeats, 1.0) << "resize touches only the grabbed note";
    ASSERT_TRUE(f.undo.canUndo());
}

// ---- Velocity scrub: Cmd+drag, ~1/px, clamped, one step ----

TEST(PianoRollEditingTest, VelocityScrub) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.roll.openClip(clipId);

    auto n = makeNote(1.0, 60, 1.0);
    n.velocity = 80;
    const auto id = f.doc.addNote(clipId, n);
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor((float)rect.getCentreX(), (float)rect.getCentreY());
    const juce::Point<float> dragged(anchor.x, anchor.y - 15.0f); // up 15 px -> +15 velocity

    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, juce::ModifierKeys::commandModifier));

    const auto* updated = f.doc.getNote(id);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->velocity, 95);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(id)->velocity, 80);
    EXPECT_FALSE(f.undo.canUndo()) << "the scrub was ONE undo step";

    // Clamped at 127.
    f.roll.getSelectionForTest().setSelection({id});
    const juce::Point<float> draggedFar(anchor.x, anchor.y - 400.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseDrag(leftDrag(f.roll, draggedFar, anchor, juce::ModifierKeys::commandModifier));
    f.roll.mouseUp(leftDrag(f.roll, draggedFar, anchor, juce::ModifierKeys::commandModifier));
    EXPECT_EQ(f.doc.getNote(id)->velocity, 127);
}

// ---- Delete: double-click and the Delete/Backspace key ----

TEST(PianoRollEditingTest, DeleteViaDoubleClickAndKey) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.roll.openClip(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(3.0, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    const auto rectA = f.roll.getNoteRect(idA);
    f.roll.mouseDoubleClick(leftClick(f.roll, centreOf(rectA)));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());

    f.roll.getSelectionForTest().setSelection({idA, idB});
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "both notes came back in ONE undo";
    EXPECT_FALSE(f.undo.canUndo());

    f.roll.getSelectionForTest().clear();
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)))
        << "empty selection: the key falls through";
}

// ---- Quantise: selected subset (per-note moveNote) vs none-selected (doc.quantiseNotes) ----

TEST(PianoRollEditingTest, QuantiseSelectedAndAll) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.roll.openClip(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(2.6, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    // Selected subset: only idA quantises (per-note moveNote path — quantiseNotes has no subset
    // overload).
    f.roll.getSelectionForTest().setSelection({idA});
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6) << "unselected note is untouched";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1);
    EXPECT_FALSE(f.undo.canUndo());

    // Nothing selected: quantises every note via doc.quantiseNotes.
    f.roll.getSelectionForTest().clear();
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0);
    ASSERT_TRUE(f.undo.canUndo());
}

// ---- Edits clamped to the clip window ----

TEST(PianoRollEditingTest, EditsClampedToClipWindow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    f.roll.openClip(clipId);

    // Draw near the clip's end, dragging far past it: the note's length clamps so it ends exactly
    // at the clip boundary rather than escaping it.
    const int pitch = f.roll.getFirstVisiblePitchForTest();
    const juce::Point<float> anchor((float)f.state.beatToX(3.4), (float)f.roll.yForPitch(pitch) + 5.0f);
    const juce::Point<float> dragged(anchor.x + 400.0f, anchor.y);

    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    const auto& drawn = clip->notes[0];
    EXPECT_LE(drawn.startBeat + drawn.lengthBeats, 4.0 + 1e-9) << "clamped to the clip's own end";
    EXPECT_GT(drawn.lengthBeats, 0.0) << "still a valid, positive-length note";

    // Move a note wildly past the clip's end: the shared delta clamps so the group's end never
    // crosses the clip boundary (TimelineClipLaneArea's clamp-the-group-together reasoning,
    // extended with an upper bound because notes — unlike clips — live inside one).
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // [1, 2)
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});
    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> moveAnchor((float)rect.getCentreX(), (float)rect.getCentreY());
    const juce::Point<float> moveDragged(moveAnchor.x + 4000.0f, moveAnchor.y);

    f.roll.mouseDown(leftClick(f.roll, moveAnchor));
    f.roll.mouseDrag(leftDrag(f.roll, moveDragged, moveAnchor));
    f.roll.mouseUp(leftDrag(f.roll, moveDragged, moveAnchor));

    const auto* moved = f.doc.getNote(id);
    ASSERT_NE(moved, nullptr);
    EXPECT_GE(moved->startBeat, 0.0);
    EXPECT_LE(moved->startBeat + moved->lengthBeats, 4.0 + 1e-9);
}

// ---- Pitch-scroll clamps at the extremes ----

TEST(PianoRollInteractionTest, PitchScrollClamps) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.roll.openClip(clipId);

    juce::MouseWheelDetails wheelUp;
    wheelUp.deltaY = 50.0f; // far beyond a real wheel's range — exercises the clamp, not the tuning
    for (int i = 0; i < 50; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {100.0f, 100.0f}), wheelUp);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);

    juce::MouseWheelDetails wheelDown;
    wheelDown.deltaY = -50.0f;
    for (int i = 0; i < 50; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {100.0f, 100.0f}), wheelDown);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);
}

// ---- The shared absolute-beat mapping (why the playhead lines up) ----

TEST(PianoRollInteractionTest, AbsoluteTimeMappingSharedWithLanes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip"); // starts at absolute beat 4
    f.roll.openClip(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)); // clip-relative 0 -> absolute beat 4
    ASSERT_TRUE(id.isValid());

    const auto rect = f.roll.getNoteRect(id);
    const int expectedX = (int)std::llround(f.state.beatToX(4.0));
    EXPECT_EQ(rect.getX(), expectedX) << "a clip-relative-0 note in a clip starting at beat 4 draws at "
                                         "the SAME x the shared TimelineViewState maps beat 4 to — the "
                                         "exact x TimelinePlayheadOverlay draws its line at for that beat.";
}

// ---- Snapshot smoke ----

TEST(PianoRollInteractionTest, SnapshotSmoke) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.roll.openClip(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    f.roll.setSize(900, 160);
    const juce::Image img = f.roll.createComponentSnapshot(f.roll.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 900);
    EXPECT_EQ(img.getHeight(), 160);
}
