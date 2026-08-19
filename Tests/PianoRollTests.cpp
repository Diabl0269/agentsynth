// PianoRollTests.cpp
//
// The piano-roll editor inside the timeline panel — create/move/resize/delete, velocity scrub,
// quantise, and the roll's OWN beat<->x mapping (zoom, scroll, gridlines, local playhead).
// Mirrors TimelineClipLaneTests.cpp's structure and harness style:
//   1. synth::ui::NoteSelectionModel — mirrors ClipSelectionModelTests.cpp's coverage, keyed on
//      synth::NoteId.
//   2. synth::ui::noteHitTestMarquee — mirrors clipHitTestMarquee's coverage.
//   3. synth::ui::PianoRollComponent — open/close lifecycle, the gesture table (single click
//      deselects, DOUBLE-click creates/deletes, drag moves/resizes, Shift+drag marquees), note
//      length following the snap division, clip-window clamping, the roll's own mapping (first bar
//      reachable, zoom around the cursor, gridline density), the local playhead's strip-confined
//      repaint seam, the Q button, and a snapshot smoke test. Driven by hand-built
//      juce::MouseEvents against a bare TimelineDoc + AppUndoManager + PianoRollComponent — no
//      TimelinePanelComponent needed.

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

// Counts every repaint the roll's LOCAL playhead asks for — the same paint-count pattern
// TimelinePlayheadTests.cpp uses on the overlay, applied to the roll's own seam.
struct CountingRoll : PianoRollComponent {
    using PianoRollComponent::PianoRollComponent;

    int requests = 0;
    juce::Rectangle<int> lastStrip;

    void requestRepaintStrip(juce::Rectangle<int> strip) override {
        ++requests;
        lastStrip = strip;
        PianoRollComponent::requestRepaintStrip(strip);
    }
};

struct PianoRollFixture {
    TimelineDoc doc;
    TimelineViewState state;
    AppUndoManager undo;
    CountingRoll roll{state};

    PianoRollFixture() {
        // The SHARED view state is deliberately left at a different zoom/origin from the roll's own
        // mapping below: every assertion here that goes through the roll must use the roll's
        // beatToX/xToBeat, never this one (which is now consulted for the snap division alone).
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;
        roll.setTimelineDoc(&doc);
        roll.setUndoManager(&undo);
        roll.setSize(900, 160);
    }

    // Opens the roll and PINS its own horizontal mapping, so a pixel offset in a test means an
    // exact number of beats (openClip zooms to fit, which is clip-length dependent by design).
    void open(ClipId id, double pixelsPerBeat = 40.0) {
        roll.openClip(id);
        const auto* clip = doc.getClip(id);
        roll.setHorizontalView(pixelsPerBeat, clip != nullptr ? clip->startBeat : 0.0);
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

// ---- Create: DOUBLE-click on empty grid, snapped, one snap division long, one undo step ----

TEST(PianoRollEditingTest, DoubleClickCreatesSnappedNoteOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2; // a row comfortably inside the grid
    const juce::Point<float> pos((float)f.roll.beatToX(2.1), (float)f.roll.yForPitch(pitch) + 5.0f);

    f.roll.mouseDoubleClick(leftClick(f.roll, pos));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    const auto& note = clip->notes[0];
    EXPECT_DOUBLE_EQ(note.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(note.lengthBeats, 1.0) << "one snap division (Snap::Quarter) long";
    EXPECT_EQ(note.pitch, pitch);
    EXPECT_EQ(note.velocity, 100);
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(note.id)) << "the new note ends up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole create was ONE undo step";
}

// A single click on empty grid is a plain click-through: it DESELECTS and never draws (the old
// pencil-by-default behaviour is gone — creating a note is the double-click above).
TEST(PianoRollEditingTest, SingleClickOnEmptyGridDeselectsAndCreatesNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 4;
    const juce::Point<float> empty((float)f.roll.beatToX(5.0), (float)f.roll.yForPitch(pitch) + 5.0f);

    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseUp(leftClick(f.roll, empty));

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "a single click never creates a note";
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty()) << "it deselects instead";
    EXPECT_FALSE(f.undo.canUndo()) << "and writes no undo step";

    // A drag from empty grid (no Shift) is equally inert — no note, no marquee.
    const juce::Point<float> dragged(empty.x + 120.0f, empty.y);
    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, empty));
    f.roll.mouseUp(leftDrag(f.roll, dragged, empty));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest());
}

// The new note is exactly ONE snap division long: quantise 1 bar -> a 1-bar note, 1/4 -> a quarter.
TEST(PianoRollEditingTest, NewNoteLengthFollowsTheSnapDivision) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const float y = (float)f.roll.yForPitch(f.roll.getFirstVisiblePitchForTest() - 2) + 5.0f;

    // Bar (4 beats with no transport wired — the same 4.0 fallback every timeline sub-component uses).
    f.state.snap = TimelineViewState::Snap::Bar;
    f.roll.mouseDoubleClick(leftClick(f.roll, {(float)f.roll.beatToX(5.0), y}));
    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 4.0) << "1 bar quantise -> a 1-bar note";

    // A sixteenth note (0.25 beat), three rows lower (further from the header, still well inside
    // the grid).
    f.state.snap = TimelineViewState::Snap::Sixteenth;
    f.roll.mouseDoubleClick(leftClick(f.roll, {(float)f.roll.beatToX(10.1), y + 30.0f}));
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    const auto& sixteenth = f.doc.getClip(clipId)->notes[1];
    EXPECT_DOUBLE_EQ(sixteenth.startBeat, 10.0);
    EXPECT_DOUBLE_EQ(sixteenth.lengthBeats, 0.25) << "1/16 quantise -> a sixteenth-long note";
}

// A double-click ON a note deletes it (standard DAW idiom), in one undo step — and creating and
// deleting are the same gesture on opposite targets.
TEST(PianoRollEditingTest, DoubleClickOnNoteDeletesOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const juce::Point<float> pos((float)f.roll.beatToX(2.0) + 4.0f, (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos)); // create
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    const auto id = f.doc.getClip(clipId)->notes[0].id;

    f.roll.mouseDoubleClick(leftClick(f.roll, centreOf(f.roll.getNoteRect(id)))); // delete
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u);
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(id));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "the delete was ONE undo step";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 0u) << "and the create before it was another";
}

// Click-to-select, then drag the body: the existing snapped-move machinery still commits once.
TEST(PianoRollEditingTest, ClickSelectsThenDragMovesOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());

    const auto rect = f.roll.getNoteRect(id);
    const juce::Point<float> anchor = centreOf(rect);
    const juce::Point<float> dragged(anchor.x + 40.0f, anchor.y); // +1 beat at 40 px/beat

    f.roll.mouseDown(leftClick(f.roll, anchor));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(id)) << "a plain click on a note selects it";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 3.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "the move was ONE undo step";
}

// ---- Move (multi-selection, together) and right-edge resize ----

TEST(PianoRollEditingTest, MoveAndResize) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

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
    f.open(clipId);

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
    f.open(clipId);

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
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(2.6, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    // Selected subset: only idA quantises (per-note moveNote path — quantiseNotes has no subset
    // overload).
    f.roll.getSelectionForTest().setSelection({idA});
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds()), juce::ModifierKeys::shiftModifier));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6) << "unselected note is untouched";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1);
    EXPECT_FALSE(f.undo.canUndo());

    // Nothing selected: quantises EVERY note in the clip via doc.quantiseNotes.
    f.roll.getSelectionForTest().clear();
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds()), juce::ModifierKeys::shiftModifier));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0) << "an empty selection means ALL notes";
    ASSERT_TRUE(f.undo.canUndo());
}

// Clicking Q always gives feedback (a momentary highlight), but an ALREADY-quantised clip mutates
// nothing and must not push an undo step — the house no-op rule.
TEST(PianoRollEditingTest, QuantiseNoOpWritesNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    const auto idB = f.doc.addNote(clipId, makeNote(2.6, 64));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    const auto qCentre = centreOf(f.roll.getQuantiseButtonBounds());
    f.roll.mouseDown(leftClick(f.roll, qCentre, juce::ModifierKeys::shiftModifier)); // real change -> one undo step
    ASSERT_TRUE(f.undo.canUndo());
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "the click flashes the button";

    f.roll.mouseDown(
        leftClick(f.roll, qCentre, juce::ModifierKeys::shiftModifier)); // already quantised -> nothing to record
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "a no-op click still gives visual feedback";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1) << "ONE undo returns to the pre-quantise state, "
                                                            "so the second click wrote no step";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6);
    EXPECT_FALSE(f.undo.canUndo());
}

// A PLAIN click on Q (and the plain Q key) toggles grid magnetism — the shared snapEnabled switch
// — and moves no note. Shift is what quantises (covered above).
TEST(PianoRollEditingTest, PlainQTogglesSnapWithoutMovingNotes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(f.state.snapEnabled);

    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));
    EXPECT_FALSE(f.state.snapEnabled) << "plain click flips the switch off";
    EXPECT_EQ(toggles, 1);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1) << "toggling never moves notes";
    EXPECT_FALSE(f.undo.canUndo()) << "a view-state toggle is not a document edit";
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "the press still flashes the button";

    // With the switch off the effective grid is gone (edits go free-hand)…
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);

    // …and the Q key toggles it right back.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('q')));
    EXPECT_TRUE(f.state.snapEnabled);
    EXPECT_EQ(toggles, 2);
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0);
}

// Shift+Q (the key) is the one-shot quantise, and it works from the CHOSEN division even while the
// magnetism switch is off — that is the whole point of a one-shot clean-up.
TEST(PianoRollEditingTest, ShiftQKeyQuantisesEvenWhileSnapToggledOff) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    ASSERT_TRUE(idA.isValid());

    f.state.snapEnabled = false;
    EXPECT_TRUE(f.roll.isQuantiseEnabled()) << "the one-shot reads the RAW division, not the switch";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('Q', juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_FALSE(f.state.snapEnabled) << "Shift+Q never flips the switch";
    ASSERT_TRUE(f.undo.canUndo());
}

// The button paints dimmed when it would do nothing, and carries the tooltip that explains its
// selection-vs-all behaviour.
TEST(PianoRollEditingTest, QuantiseButtonEnabledStateAndTooltip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    EXPECT_FALSE(f.roll.isQuantiseEnabled()) << "an empty clip has nothing to quantise";

    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.1, 60)).isValid());
    EXPECT_TRUE(f.roll.isQuantiseEnabled());

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_FALSE(f.roll.isQuantiseEnabled()) << "Snap::Off leaves no grid to quantise to";

    EXPECT_EQ(f.roll.getTooltipFor(f.roll.getQuantiseButtonBounds().getCentre()),
              juce::String(PianoRollComponent::kQuantiseTooltip));
    EXPECT_TRUE(f.roll.getTooltipFor(f.roll.getBackButtonBounds().getCentre()).isEmpty());
}

// ---- Edits clamped to the clip window ----

TEST(PianoRollEditingTest, EditsClampedToClipWindow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    f.open(clipId);

    // Create past the clip's END: the start snaps back inside the window rather than producing a
    // note that escapes it (or no note at all).
    const int pitch = f.roll.getFirstVisiblePitchForTest();
    const juce::Point<float> pastEnd((float)f.roll.beatToX(3.8), (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, pastEnd));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    const auto& created = clip->notes[0];
    EXPECT_DOUBLE_EQ(created.startBeat, 3.0) << "snapping up to beat 4 would leave no room — stepped back";
    EXPECT_LE(created.startBeat + created.lengthBeats, 4.0 + 1e-9) << "clamped to the clip's own end";
    EXPECT_GT(created.lengthBeats, 0.0) << "still a valid, positive-length note";

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
    f.open(clipId);

    juce::MouseWheelDetails wheelUp{};
    wheelUp.deltaY = 50.0f; // far beyond a real wheel's range — exercises the clamp, not the tuning
    for (int i = 0; i < 50; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {100.0f, 100.0f}), wheelUp);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);

    juce::MouseWheelDetails wheelDown{};
    wheelDown.deltaY = -50.0f;
    for (int i = 0; i < 50; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {100.0f, 100.0f}), wheelDown);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);
}

// ---- The roll's OWN mapping: the keys column is a gutter, so the first bar is reachable ----

TEST(PianoRollInteractionTest, FirstBarIsReachableRightOfTheKeysColumn) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip"); // starts at absolute beat 4
    f.roll.openClip(clipId);                                      // framing comes from openClip itself

    // openClip parks the clip's start at the keys column's right edge and fits the whole clip.
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), 4.0);
    EXPECT_DOUBLE_EQ(f.roll.beatToX(4.0), (double)PianoRollComponent::kKeysColumnWidth);
    EXPECT_GE(f.roll.beatToX(12.0), (double)f.roll.getWidth() - 1.0) << "the whole clip fits the grid";

    // A note can be CREATED at the clip's very first beat — the bug was that those pixels were
    // hidden under an opaque keys strip that ignored clicks.
    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const juce::Point<float> firstBeat((float)PianoRollComponent::kKeysColumnWidth + 2.0f,
                                       (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, firstBeat));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 0.0) << "clip-relative beat 0 — the clip's very first beat";

    const auto rect = f.roll.getNoteRect(clip->notes[0].id);
    EXPECT_GE(rect.getX(), PianoRollComponent::kKeysColumnWidth) << "and it draws RIGHT OF the keys gutter";
    EXPECT_EQ(rect.getX(), (int)std::llround(f.roll.beatToX(4.0)));
}

// Cmd+wheel zooms the roll's own mapping around the cursor: the beat under the pointer does not
// move, and the SHARED view state (the lanes behind the roll) is untouched.
TEST(PianoRollInteractionTest, CmdWheelZoomsAroundTheCursorBeat) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);

    const float anchorX = 300.0f;
    const double beatUnderCursor = f.roll.xToBeat((double)anchorX);

    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = 0.5f;
    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, juce::ModifierKeys::commandModifier), wheel);

    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "positive deltaY zooms in";
    EXPECT_NEAR(f.roll.xToBeat((double)anchorX), beatUnderCursor, 1.0e-9)
        << "the beat under the cursor is the zoom's fixed point";
    EXPECT_DOUBLE_EQ(f.state.pixelsPerBeat, 40.0) << "the panel-wide view state never moves with the roll's zoom";
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0);

    // And back out again: equal-and-opposite gestures cancel (exponential factor).
    wheel.deltaY = -0.5f;
    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, juce::ModifierKeys::commandModifier), wheel);
    EXPECT_NEAR(f.roll.getPixelsPerBeat(), 40.0, 1.0e-9);
}

// Cmd+Shift+wheel is the VERTICAL zoom, clamped to the documented bounds.
TEST(PianoRollInteractionTest, CmdShiftWheelZoomsPitchRowsWithinClamps) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    const int mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = 0.4f;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheel);
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    wheel.deltaY = 5.0f; // far past the clamp
    for (int i = 0; i < 20; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheel);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMaxPixelsPerSemitone);

    wheel.deltaY = -5.0f;
    for (int i = 0; i < 40; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheel);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMinPixelsPerSemitone);
}

// Shift+wheel scrolls time through the roll's own scroll origin (never the shared one).
TEST(PianoRollInteractionTest, ShiftWheelScrollsTimeLocally) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);

    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaY = -0.5f;            // scroll right
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheel);

    EXPECT_GT(f.roll.getFirstVisibleBeat(), 4.0);
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0) << "the lanes behind the roll keep their own scroll";
}

// ---- Gridlines follow the snap division ----

TEST(PianoRollInteractionTest, GridLinesFollowTheSnapDivision) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 32.0, "Clip");
    f.open(clipId); // 40 px/beat, beat 0 at the gutter

    f.state.snap = TimelineViewState::Snap::Quarter;
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0);
    const int beatLines = f.roll.getGridLineCountForTest(1.0);
    EXPECT_GT(beatLines, 0);

    f.state.snap = TimelineViewState::Snap::Sixteenth;
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.25);
    const int sixteenthLines = f.roll.getGridLineCountForTest(f.roll.getGridDivisionForTest());
    EXPECT_GT(sixteenthLines, beatLines * 3) << "a finer division draws proportionally more lines";

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0) << "Snap::Off has no sub-beat level at all";
    EXPECT_EQ(f.roll.getGridLineCountForTest(0.0), 0);

    // Zoomed far out, a sub-beat level is dropped rather than drawn as a wall of lines.
    f.roll.setHorizontalView(TimelineViewState::kMinPixelsPerBeat, 0.0);
    EXPECT_EQ(f.roll.getGridLineCountForTest(0.25), 0);
}

// ---- The local playhead: the roll draws it at ITS OWN x, under the same strip discipline ----

TEST(PianoRollInteractionTest, LocalPlayheadUsesTheRollsOwnMapping) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    EXPECT_FALSE(f.roll.isLocalPlayheadActive()) << "a closed roll never claims the overlay's rows";
    f.open(clipId); // 40 px/beat with beat 4 at the gutter
    EXPECT_TRUE(f.roll.isLocalPlayheadActive());

    // The overlay hands over the BEAT; the roll maps it itself.
    f.roll.setPlayheadBeat(6.0);
    const int expectedX = (int)std::llround(f.roll.beatToX(6.0));
    EXPECT_EQ(f.roll.getPlayheadLineX(), expectedX);
    EXPECT_EQ(expectedX, PianoRollComponent::kKeysColumnWidth + 80);
    EXPECT_NE(expectedX, (int)std::llround(f.state.beatToX(6.0)))
        << "which is exactly why the overlay must not draw here: the shared mapping puts beat 6 elsewhere";
    EXPECT_EQ(f.roll.requests, 1) << "the first position after an open costs exactly one strip";

    // Stopped: the same beat again and again costs nothing.
    for (int i = 0; i < 5; ++i)
        f.roll.setPlayheadBeat(6.0);
    EXPECT_EQ(f.roll.requests, 1);

    // Playing: each moved position repaints a STRIP, never the component.
    f.roll.setPlayheadBeat(6.5);
    EXPECT_EQ(f.roll.requests, 2);
    EXPECT_EQ(f.roll.lastStrip.getY(), PianoRollComponent::kHeaderHeight) << "confined below the header";
    EXPECT_GE(f.roll.lastStrip.getX(), PianoRollComponent::kKeysColumnWidth) << "and right of the keys gutter";
    EXPECT_LE(f.roll.lastStrip.getWidth(), 20 + 2 * PianoRollComponent::kPlayheadStripHalfWidth + 1);
    EXPECT_LT(f.roll.lastStrip.getWidth(), f.roll.getWidth());

    // A zoom moves the line without the transport moving at all.
    f.roll.setHorizontalView(80.0, 4.0);
    EXPECT_EQ(f.roll.getPlayheadLineX(), (int)std::llround(f.roll.beatToX(6.5)));
}

// ---- Snapshot smoke ----

TEST(PianoRollInteractionTest, SnapshotSmoke) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    f.roll.setSize(900, 160);
    const juce::Image img = f.roll.createComponentSnapshot(f.roll.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 900);
    EXPECT_EQ(img.getHeight(), 160);
}
