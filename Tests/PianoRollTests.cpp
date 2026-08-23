// PianoRollTests.cpp
//
// The piano-roll editor inside the timeline panel — create/move/resize/delete, velocity scrub,
// quantise, and the roll's OWN beat<->x mapping (zoom, scroll, gridlines, local playhead).
// Mirrors TimelineClipLaneTests.cpp's structure and harness style:
//   1. synth::ui::NoteSelectionModel — mirrors ClipSelectionModelTests.cpp's coverage, keyed on
//      synth::NoteId.
//   2. synth::ui::noteHitTestMarquee — mirrors clipHitTestMarquee's coverage.
//   3. synth::ui::PianoRollComponent — open/close lifecycle, the gesture table (single click
//      deselects, DOUBLE-click creates/deletes, drag moves/resizes, drag-from-empty marquees), note
//      length following the snap division, clip-window clamping, the roll's own mapping (first bar
//      reachable, zoom around the cursor, gridline density), the local playhead's strip-confined
//      repaint seam, the Q button, and a snapshot smoke test. Driven by hand-built
//      juce::MouseEvents against a bare TimelineDoc + AppUndoManager + PianoRollComponent — no
//      TimelinePanelComponent needed.
//   4. The edit TOOLS (Split / Glue / Erase / Mute / Draw) — each one's single-click gesture, its
//      no-op cases (which must leave the undo stack alone), and the fact that none of them can
//      start a Select-tool drag.
//   5. The note CLIPBOARD — copy/cut/paste-at-playhead/duplicate/repeat/select-all, including the
//      cross-clip paste the member clipboard exists for.
//   6. ARROW-key editing — grid nudge, semitone/octave transpose, group clamping, and the
//      fall-through contract when nothing is selected.
//   7. MARQUEE multi-select — the plain (unmodified) drag from empty grid that replaces the
//      selection, its additive modifier variants, and the deferred plain CLICK that must still just
//      deselect rather than sweeping a zero-size marquee.
//   8. Alt+Left/Right note NAVIGATION — walking the doc's canonical note order, collapsing a
//      multi-selection, the ends of the run, and the minimal scroll that brings an off-screen note
//      into view.
//   9. REBINDABLE surface keys — the same actions resolved through a ShortcutManager instead of the
//      hardcoded defaults, including the "an unbound action has no key" rule and the two keys
//      (Escape, Delete) that stay fixed on purpose.
//  10. WHEEL/ZOOM policy — the macOS Shift axis swap that used to kill Cmd+Shift+wheel, the
//      natural-vs-inverted scroll sign on both scroll axes, the anchored zoomHorizontal/zoomVertical
//      commands, and the wheel-zoom DIRECTION convention (wheel UP — the physical gesture,
//      independent of isReversed — zooms IN by default; setZoomScrollInverted flips it).
//  12. KEY LABELS — the pure keyLabelFor(pitch, mode, rowHeightPx) helper paintKeysColumn calls per
//      row: AllNotes vs OctavesOnly, and the shared row-height readability floor both modes fall
//      back through.
//  13. ROW MAPPING — yForPitch/pitchForY through visiblePitches_: the unfiltered round-trip
//      (unchanged behaviour), a scale context with pitch-visibility OFF (no row ever hidden), ON
//      (out-of-scale EMPTY rows collapse, a noted out-of-scale pitch never does), the round-trip
//      once filtered, and firstVisiblePitch_'s "always a member of visiblePitches_" invariant.
//  14. NOTE COLOURING — notePaintFor's resolveNoteColour path (NoteColour.h), asserted against the
//      SAME resolver fed the same fallback Colors, rather than against a re-derived formula.
//  15. KEYS-COLUMN geometry — the black-key inset seam, and a snapshot smoke test with a scale
//      context and AllNotes labels active (on top of the unmodified SnapshotSmoke above).
//  16. BEAT-ANCHORED drag math (a mid-drag view scroll folds into the delta rather than being
//      cancelled out by it), EDGE AUTO-SCROLL (the gated timer's arm/disarm contract and what one
//      tick does on each axis, including the vertical walk staying inside visiblePitches_ under
//      an active scale context), and FOLLOW PLAYHEAD (the page-flip, its zero-extra-repaint
//      contract while the beat is already visible and unmoved, and the "never fights a drag"
//      gate).
//
// Every test in 4-6 and 9 configures snap explicitly (never inherits a default), because these are
// grid-sensitive assertions and a machine-local snap preference must never be able to decide
// whether they pass. Section 10 pins the view instead (zoom and scroll origin, via
// PianoRollFixture::open) for the same reason: a wheel assertion measured in beats depends on
// pixels-per-beat, so it is stated rather than inherited.

#include "../Source/AppUndoManager.h"
#include "../Source/ShortcutManager.h"
#include "../Source/Timeline/MusicalScale.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/EditTool.h"
#include "../Source/UI/NoteColour.h"
#include "../Source/UI/NoteSelectionModel.h"
#include "../Source/UI/PianoRollComponent.h"
#include "../Source/UI/ScaleAssistPanel.h"
#include "../Source/UI/Theme/Theme.h"
#include "../Source/UI/TimelineViewState.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

using synth::ClipId;
using synth::NoteId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::EditTool;
using synth::ui::NoteSelectionModel;
using synth::ui::PianoRollComponent;
using synth::ui::ScaleAssistPanel;
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
// TimelinePlayheadTests.cpp uses on the overlay, applied to the roll's own seam. The Split tool's
// hover preview has its OWN counter (its own seam), so a test can prove a hover repaints the
// preview strip and NOTHING else.
struct CountingRoll : PianoRollComponent {
    using PianoRollComponent::PianoRollComponent;

    int requests = 0;
    juce::Rectangle<int> lastStrip;
    int previewRequests = 0;
    juce::Rectangle<int> lastPreviewStrip;

    void requestRepaintStrip(juce::Rectangle<int> strip) override {
        ++requests;
        lastStrip = strip;
        PianoRollComponent::requestRepaintStrip(strip);
    }

    void requestRepaintPreviewStrip(juce::Rectangle<int> strip) override {
        ++previewRequests;
        lastPreviewStrip = strip;
        PianoRollComponent::requestRepaintPreviewStrip(strip);
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

// A throwaway temp file, never the real user settings — the scale-assist persistence tests below
// (panel visibility + user scales) need a REAL juce::PropertiesFile (setPropertiesFile takes a
// pointer to one), not the ApplicationProperties wrapper the Preferences-tab tests use.
std::unique_ptr<juce::PropertiesFile> makeScaleAssistTestProps(const juce::String& name) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

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

    // A drag from empty grid CREATES nothing either — it marquees (section 7 owns that behaviour;
    // this arrangement used to assert the drag was inert, back when the marquee needed Shift).
    const juce::Point<float> dragged(empty.x + 120.0f, empty.y);
    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, empty));
    EXPECT_TRUE(f.roll.isMarqueeActiveForTest()) << "a plain drag from empty grid multi-selects";
    f.roll.mouseUp(leftDrag(f.roll, dragged, empty));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_FALSE(f.roll.isMarqueeActiveForTest());
    EXPECT_FALSE(f.undo.canUndo()) << "selecting is never a document edit";
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

// ---- Fine-snap sanity: a new note is exactly ONE division long (see computeNewNoteAnchor), and
// nothing floors or rejects a division as fine as 1/128 (0.03125 beats). TimelineDoc::addNote only
// rejects a NON-POSITIVE/non-finite length (isFinitePositive: `> 0.0`, no minimum), and
// computeNewNoteAnchor only falls back to kMinNoteLengthBeats when the grid is OFF (`grid > 0.0 ?
// grid : kMinNoteLengthBeats`) — a real division smaller than kMinNoteLengthBeats is used AS IS.
TEST(PianoRollInteractionTest, NoteCreatedAtOneHundredTwentyEighthGridHasExactlyThatLength) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    // Set directly on the shared view state, per the class comment: it is consulted for the snap
    // division ONLY.
    f.state.snap = TimelineViewState::Snap::HundredTwentyEighth;
    f.state.snapEnabled = true;

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const juce::Point<float> pos((float)f.roll.beatToX(2.0), (float)f.roll.yForPitch(pitch) + 5.0f);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 0.03125);
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

// ============================================================================
// 4. Edit tools
// ============================================================================

namespace {

// A point inside the row for `pitch` at the given ABSOLUTE beat, through the roll's own mapping.
// +5 lands in the middle of a 10 px row (kPixelsPerSemitone), so a click can never fall on the
// boundary between two rows.
juce::Point<float> pointAt(PianoRollComponent& roll, double absBeat, int pitch) {
    return {(float)roll.beatToX(absBeat), (float)roll.yForPitch(pitch) + 5.0f};
}

// A no-button move: what mouseMove/mouseEnter get, as opposed to leftClick's pressed state.
juce::MouseEvent hover(juce::Component& comp, juce::Point<float> pos) {
    return makeRollMouseEvent(comp, pos, juce::ModifierKeys(), false, pos);
}

// Pins snap explicitly — never inherited from the fixture or from a machine-local preference.
void setSnap(PianoRollFixture& f, TimelineViewState::Snap snap, bool enabled = true) {
    f.state.snap = snap;
    f.state.snapEnabled = enabled;
}

} // namespace

TEST(PianoRollToolTest, DefaultsToSelectAndTakesTheToolFromItsOwner) {
    PianoRollFixture f;
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Select);
    f.roll.setActiveTool(EditTool::Erase);
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Erase);
    f.roll.setActiveTool(EditTool::Select);
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Select);
}

// The tool DIGITS belong to the panel: the roll must leave them unconsumed or the two would fight
// over which tool is active (see setActiveTool).
TEST(PianoRollToolTest, DigitKeysAreLeftForThePanel) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60)).isValid());

    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('1')));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('3')));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('8')));
    EXPECT_EQ(f.roll.getActiveTool(), EditTool::Select) << "the roll never switches tool by itself";
}

// ---- Split ----

TEST(PianoRollToolTest, SplitCutsANoteInTwoAndTheRightHalfInheritsEveryField) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    auto n = makeNote(1.0, 60, 2.0); // [1, 3)
    n.velocity = 77;
    n.channel = 3;
    const auto id = f.doc.addNote(clipId, n);
    ASSERT_TRUE(id.isValid());
    ASSERT_TRUE(f.doc.setNoteMuted(id, true));
    ASSERT_FALSE(f.undo.canUndo()) << "doc setup happened outside the undo manager";

    f.roll.setActiveTool(EditTool::Split);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 2.0, 60)));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 2u);
    const auto& left = clip->notes[0]; // notes stay sorted by startBeat
    const auto& right = clip->notes[1];

    EXPECT_EQ(left.id, id) << "the left half keeps the original note's identity";
    EXPECT_DOUBLE_EQ(left.startBeat, 1.0);
    EXPECT_DOUBLE_EQ(left.lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(right.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(right.lengthBeats, 1.0);
    EXPECT_EQ(right.pitch, 60);
    EXPECT_EQ(right.velocity, 77);
    EXPECT_EQ(right.channel, 3);
    EXPECT_TRUE(right.muted) << "a split divides a note — the right half inherits mute too";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "resize + add were ONE undo step";
}

TEST(PianoRollToolTest, SplitTooCloseToAnEdgeIsANoOpWithNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // [1, 2): every snapped cut is an edge
    ASSERT_TRUE(id.isValid());
    f.roll.setActiveTool(EditTool::Split);

    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.2, 60))); // snaps back to the start
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.8, 60))); // snaps up to the end

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "neither cut leaves room on both sides";
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->lengthBeats, 1.0);
    EXPECT_FALSE(f.undo.canUndo()) << "a no-op gesture writes no undo step";
}

// The hover preview follows the SNAPPED cut and repaints only when that cut actually moves — and
// it repaints its own strip, never the playhead's.
TEST(PianoRollToolTest, SplitHoverPreviewRepaintsOnlyWhenTheCutMoves) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60, 4.0)).isValid()); // [1, 5)

    f.roll.setActiveTool(EditTool::Split);
    EXPECT_EQ(f.roll.previewRequests, 0);

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 2.0, 60)));
    EXPECT_TRUE(f.roll.hasSplitPreviewForTest());
    EXPECT_DOUBLE_EQ(f.roll.getSplitPreviewBeatForTest(), 2.0);
    EXPECT_EQ(f.roll.previewRequests, 1);

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 2.2, 60)));
    EXPECT_EQ(f.roll.previewRequests, 1) << "same snapped cut — a moving pointer costs nothing";

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 3.0, 60)));
    EXPECT_DOUBLE_EQ(f.roll.getSplitPreviewBeatForTest(), 3.0);
    EXPECT_EQ(f.roll.previewRequests, 2);

    f.roll.mouseMove(hover(f.roll, pointAt(f.roll, 6.0, 60))); // off the note entirely
    EXPECT_FALSE(f.roll.hasSplitPreviewForTest());
    EXPECT_EQ(f.roll.previewRequests, 3);

    EXPECT_EQ(f.roll.requests, 0) << "hovering must never repaint the playhead's strip";
}

// ---- Glue ----

TEST(PianoRollToolTest, GlueAbsorbsTheNextSamePitchNoteAndBridgesTheGap) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto first = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0));  // [0, 1)
    const auto second = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0)); // [2, 3), a 1-beat GAP away
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    f.roll.getSelectionForTest().setSelection({first, second});

    f.roll.setActiveTool(EditTool::Glue);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 0.5, 60)));

    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    ASSERT_NE(f.doc.getNote(first), nullptr);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->lengthBeats, 3.0) << "the gap is bridged, Cubase-style";
    EXPECT_EQ(f.doc.getNote(second), nullptr);
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(second)) << "the absorbed note leaves the selection";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->lengthBeats, 1.0);
    EXPECT_FALSE(f.undo.canUndo()) << "resize + remove were ONE undo step";
}

TEST(PianoRollToolTest, GlueIgnoresANeighbourAtADifferentPitch) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto first = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0));
    const auto other = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)); // a different VOICE
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(other.isValid());

    f.roll.setActiveTool(EditTool::Glue);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 0.5, 60)));

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getNote(first)->lengthBeats, 1.0);
    EXPECT_NE(f.doc.getNote(other), nullptr);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollToolTest, GlueWithNothingToAbsorbWritesNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto only = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(only.isValid());

    f.roll.setActiveTool(EditTool::Glue);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));

    EXPECT_DOUBLE_EQ(f.doc.getNote(only)->lengthBeats, 1.0);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Erase ----

TEST(PianoRollToolTest, EraseClickDeletesTheNoteInOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(3.0, 64, 1.0));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA});

    f.roll.setActiveTool(EditTool::Erase);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));

    EXPECT_EQ(f.doc.getNote(idA), nullptr);
    EXPECT_NE(f.doc.getNote(idB), nullptr) << "only the clicked note goes";
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(idA));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());

    // A click on empty grid erases nothing at all.
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 6.0, 60)));
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Mute ----

TEST(PianoRollToolTest, MuteClickTogglesBothWays) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    ASSERT_FALSE(f.doc.getNote(id)->muted);

    f.roll.setActiveTool(EditTool::Mute);
    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));
    EXPECT_TRUE(f.doc.getNote(id)->muted);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0) << "muting never moves a note";

    f.roll.mouseDown(leftClick(f.roll, pointAt(f.roll, 1.5, 60)));
    EXPECT_FALSE(f.doc.getNote(id)->muted) << "the same click un-mutes";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getNote(id)->muted) << "each toggle is its own undo step";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_FALSE(f.doc.getNote(id)->muted);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- Draw ----

TEST(PianoRollToolTest, DrawDragCreatesANoteOfTheDraggedLength) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const auto anchor = pointAt(f.roll, 1.6, pitch); // inside the [1, 2) grid CELL
    const auto dragged = pointAt(f.roll, 3.0, pitch);

    f.roll.setActiveTool(EditTool::Draw);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    EXPECT_DOUBLE_EQ(f.roll.getDrawPreviewLengthForTest(), 1.0) << "the press arms one division";
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    EXPECT_DOUBLE_EQ(f.roll.getDrawPreviewLengthForTest(), 2.0);
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty()) << "nothing is committed until the release";
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 1.0) << "the pencil FLOORS to the cell it was pressed in";
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 2.0);
    EXPECT_EQ(clip->notes[0].pitch, pitch);
    EXPECT_EQ(clip->notes[0].velocity, 100);
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(clip->notes[0].id));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty()) << "the draw was ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollToolTest, DrawPlainClickCreatesAOneDivisionNote) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const auto pos = pointAt(f.roll, 1.6, pitch);
    f.roll.setActiveTool(EditTool::Draw);
    f.roll.mouseDown(leftClick(f.roll, pos));
    f.roll.mouseUp(leftClick(f.roll, pos));

    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[0].lengthBeats, 1.0);

    // The division still decides the length: a sixteenth grid draws a sixteenth.
    setSnap(f, TimelineViewState::Snap::Sixteenth);
    const auto pos2 = pointAt(f.roll, 8.3, pitch - 3);
    f.roll.mouseDown(leftClick(f.roll, pos2));
    f.roll.mouseUp(leftClick(f.roll, pos2));
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[1].startBeat, 8.25);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[1].lengthBeats, 0.25);
}

TEST(PianoRollToolTest, DrawNeverRedrawsOverAnExistingNote) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());

    f.roll.setActiveTool(EditTool::Draw);
    const auto pos = pointAt(f.roll, 1.5, 60);
    f.roll.mouseDown(leftClick(f.roll, pos));
    f.roll.mouseUp(leftClick(f.roll, pos));

    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

// ---- The tools disable the Select-tool drags entirely ----

TEST(PianoRollToolTest, NonSelectToolsNeverMoveResizeOrMarquee) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    // Mute tool: press on the note, drag a long way, release. The note toggles and stays put.
    f.roll.setActiveTool(EditTool::Mute);
    const auto anchor = pointAt(f.roll, 2.5, 60);
    const juce::Point<float> dragged(anchor.x + 160.0f, anchor.y - 40.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor));

    const auto* note = f.doc.getNote(id);
    ASSERT_NE(note, nullptr);
    EXPECT_DOUBLE_EQ(note->startBeat, 2.0) << "no move";
    EXPECT_EQ(note->pitch, 60) << "no transpose";
    EXPECT_DOUBLE_EQ(note->lengthBeats, 1.0) << "no resize";
    EXPECT_TRUE(note->muted);

    // No drag on empty grid arms a marquee under a non-Select tool — PLAIN included, which is the
    // one that matters now that plain drag is the Select tool's marquee gesture (section 7). Shift
    // is checked alongside it because it used to be the only combination that could arm one at all.
    const auto emptyAnchor = pointAt(f.roll, 9.0, 55);
    const juce::Point<float> emptyTo(emptyAnchor.x + 120.0f, emptyAnchor.y + 20.0f);
    for (const int modifier : {0, (int)juce::ModifierKeys::shiftModifier}) {
        f.roll.mouseDown(leftClick(f.roll, emptyAnchor, modifier));
        EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "modifier " << modifier;
        f.roll.mouseDrag(leftDrag(f.roll, emptyTo, emptyAnchor, modifier));
        EXPECT_FALSE(f.roll.isMarqueeActiveForTest()) << "modifier " << modifier << " after dragging";
        f.roll.mouseUp(leftDrag(f.roll, emptyTo, emptyAnchor, modifier));
    }
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "and the Mute tool drew nothing either";
}

// Switching back to Select restores the whole original gesture table, double-click included.
TEST(PianoRollToolTest, SelectToolKeepsItsDoubleClickCreateAndDelete) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2;
    const auto pos = pointAt(f.roll, 2.1, pitch);

    // Under Erase, a double-click must not create anything.
    f.roll.setActiveTool(EditTool::Erase);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos));
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());

    f.roll.setActiveTool(EditTool::Select);
    f.roll.mouseDoubleClick(leftClick(f.roll, pos));
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 1u);
    const auto id = f.doc.getClip(clipId)->notes[0].id;
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0) << "the double-click still snaps to the NEAREST division";

    f.roll.mouseDoubleClick(leftClick(f.roll, centreOf(f.roll.getNoteRect(id))));
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());

    // And the Select drag still moves.
    const auto moved = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(moved.isValid());
    const auto rect = f.roll.getNoteRect(moved);
    const juce::Point<float> dragAnchor = centreOf(rect);
    const juce::Point<float> dragTo(dragAnchor.x + 40.0f, dragAnchor.y);
    f.roll.mouseDown(leftClick(f.roll, dragAnchor));
    f.roll.mouseDrag(leftDrag(f.roll, dragTo, dragAnchor));
    f.roll.mouseUp(leftDrag(f.roll, dragTo, dragAnchor));
    EXPECT_DOUBLE_EQ(f.doc.getNote(moved)->startBeat, 3.0);
}

// ============================================================================
// 5. Note clipboard
// ============================================================================

TEST(PianoRollClipboardTest, CopyThenPasteAtThePlayheadInsideTheClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());

    EXPECT_FALSE(f.roll.canPasteNotes()) << "nothing copied yet";
    EXPECT_FALSE(f.roll.copySelectedNotes()) << "and nothing selected to copy";

    f.roll.getSelectionForTest().setSelection({idA, idB});
    EXPECT_TRUE(f.roll.hasNoteSelection());
    EXPECT_TRUE(f.roll.copySelectedNotes());
    EXPECT_EQ(f.roll.getClipboardSizeForTest(), 2);
    EXPECT_TRUE(f.roll.canPasteNotes());
    EXPECT_FALSE(f.undo.canUndo()) << "copying is not a document edit";

    f.roll.setPlayheadBeat(4.0);
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 4u);
    EXPECT_DOUBLE_EQ(clip->notes[2].startBeat, 4.0) << "the earliest copied note lands ON the playhead";
    EXPECT_EQ(clip->notes[2].pitch, 60);
    EXPECT_DOUBLE_EQ(clip->notes[3].startBeat, 5.0) << "and the block keeps its internal shape";
    EXPECT_EQ(clip->notes[3].pitch, 64);

    const auto selected = f.roll.getSelectionForTest().getSelected();
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0], clip->notes[2].id);
    EXPECT_EQ(selected[1], clip->notes[3].id) << "the pasted block becomes the selection";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "both pasted notes came back in ONE undo";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollClipboardTest, PasteWithThePlayheadOutsideTheClipAnchorsAtZero) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 4.0, "Clip"); // absolute [4, 8)
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(idA.isValid());
    f.roll.getSelectionForTest().setSelection({idA});
    ASSERT_TRUE(f.roll.copySelectedNotes());

    f.roll.setPlayheadBeat(0.0); // well before the clip starts
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 0.0) << "an out-of-clip playhead anchors the block at the clip start";
}

TEST(PianoRollClipboardTest, CopiedNotesPasteIntoADifferentClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "A");
    const auto clipB = f.doc.addClip(trackId, 8.0, 8.0, "B");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipA);

    const auto id = f.doc.addNote(clipA, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});
    ASSERT_TRUE(f.roll.copySelectedNotes());

    // Switching clips clears the SELECTION but never the clipboard — that is the whole reason the
    // clipboard is a member of the roll.
    f.open(clipB);
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_TRUE(f.roll.canPasteNotes());

    f.roll.setPlayheadBeat(10.0); // absolute -> clip-relative beat 2 inside B
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    ASSERT_EQ(f.doc.getClip(clipB)->notes.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipB)->notes[0].startBeat, 2.0);
    EXPECT_EQ(f.doc.getClip(clipB)->notes[0].pitch, 60);
    EXPECT_EQ(f.doc.getClip(clipA)->notes.size(), 1u) << "the source clip is untouched";
}

TEST(PianoRollClipboardTest, PasteClampsLengthAtTheClipEndAndSkipsNotesPastIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(0.0, 60, 2.0)); // offset 0, two beats long
    const auto idB = f.doc.addNote(clipId, makeNote(3.0, 64, 1.0)); // offset 3
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});
    ASSERT_TRUE(f.roll.copySelectedNotes());

    f.roll.setPlayheadBeat(3.0); // only one beat of room left
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 3u) << "the +3 offset lands past the clip's end and is skipped";
    const auto pasted = f.roll.getSelectionForTest().getSelected();
    ASSERT_EQ(pasted.size(), 1u);
    const auto* note = f.doc.getNote(pasted[0]);
    ASSERT_NE(note, nullptr);
    EXPECT_DOUBLE_EQ(note->startBeat, 3.0);
    EXPECT_DOUBLE_EQ(note->lengthBeats, 1.0) << "clamped to the clip's end rather than overrunning it";
}

TEST(PianoRollClipboardTest, DuplicatePlacesCopiesAfterTheSelectionSpan) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // [1, 2)
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 2.0)); // [2, 4)  -> span 3
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    EXPECT_TRUE(f.roll.duplicateSelectedNotes());

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 4u);
    EXPECT_DOUBLE_EQ(clip->notes[2].startBeat, 4.0) << "immediately after the span's end";
    EXPECT_EQ(clip->notes[2].pitch, 60);
    EXPECT_DOUBLE_EQ(clip->notes[3].startBeat, 5.0);
    EXPECT_EQ(clip->notes[3].pitch, 64);
    EXPECT_DOUBLE_EQ(clip->notes[3].lengthBeats, 2.0);
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 2) << "the copies become the selection";
    EXPECT_EQ(f.roll.getClipboardSizeForTest(), 0) << "duplicating never stomps the clipboard";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollClipboardTest, CutFillsTheClipboardAndDeletesInOneStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    EXPECT_TRUE(f.roll.cutSelectedNotes());
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_EQ(f.roll.getClipboardSizeForTest(), 2);
    EXPECT_TRUE(f.roll.canPasteNotes()) << "a cut is always pasteable";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 2u) << "the whole cut was ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());

    // And what it captured really does paste back.
    f.doc.clearNotes(clipId);
    f.roll.setPlayheadBeat(8.0);
    EXPECT_TRUE(f.roll.pasteNotesAtPlayhead());
    ASSERT_EQ(f.doc.getClip(clipId)->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[0].startBeat, 8.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->notes[1].startBeat, 9.0);
}

TEST(PianoRollClipboardTest, RepeatPlacesBackToBackBlocksAndStopsAtTheClipEnd) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)); // span 1 beat
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_FALSE(f.roll.repeatSelectedNotes(0)) << "a zero repeat does nothing";
    EXPECT_TRUE(f.roll.repeatSelectedNotes(2));

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 3u);
    EXPECT_DOUBLE_EQ(clip->notes[1].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(clip->notes[2].startBeat, 2.0) << "back to back, one span apart";
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 2) << "both copies end up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clipId)->notes.size(), 1u) << "the whole repeat was ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());

    // Asking for more copies than fit stops at the boundary instead of piling them on the last beat.
    f.roll.getSelectionForTest().setSelection({id});
    EXPECT_TRUE(f.roll.repeatSelectedNotes(20));
    const auto* filled = f.doc.getClip(clipId);
    EXPECT_EQ(filled->notes.size(), 8u) << "beats 0..7 of an 8-beat clip, and no more";
    EXPECT_DOUBLE_EQ(filled->notes.back().startBeat, 7.0);
}

TEST(PianoRollClipboardTest, SelectAllSelectsEveryNoteInTheOpenClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    EXPECT_FALSE(f.roll.selectAllNotes()) << "an empty clip has nothing to select";
    EXPECT_FALSE(f.roll.hasNoteSelection());

    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)).isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 62, 1.0)).isValid());
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)).isValid());

    EXPECT_TRUE(f.roll.selectAllNotes());
    EXPECT_EQ(f.roll.getSelectionForTest().size(), 3);
    EXPECT_FALSE(f.undo.canUndo()) << "selecting is not a document edit";
}

// ============================================================================
// 6. Arrow-key editing
// ============================================================================

TEST(PianoRollArrowKeyTest, LeftAndRightNudgeByTheGridDivision) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 3.0);
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "a horizontal nudge never transposes";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 3.0) << "each press is its own undo step";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo());

    // A finer grid nudges by that grid.
    setSnap(f, TimelineViewState::Snap::Sixteenth);
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 2.25);
}

TEST(PianoRollArrowKeyTest, NudgeFallsBackToASixteenthWhenSnapIsOff) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter, /*enabled*/ false);
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0 + PianoRollComponent::kMinNoteLengthBeats);
}

TEST(PianoRollArrowKeyTest, NudgeClampsTheWholeGroupAtBothClipEdges) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // [0, 4)
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto idA = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0)); // [0, 1)
    const auto idB = f.doc.addNote(clipId, makeNote(2.0, 64, 1.0)); // [2, 3)
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(idB.isValid());
    f.roll.getSelectionForTest().setSelection({idA, idB});

    // One step right fits exactly (the group's end reaches the clip's end)…
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0);

    // …and the next one is clamped to nothing: the key is still consumed, but nothing moves and no
    // undo step is written. Crucially the group keeps its shape — the leading note does NOT slide
    // on while the trailing one is stuck.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 3.0);

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.0);

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 0.0) << "clamped at the clip's start";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.0);

    // Two real moves, two undo steps — the two clamped presses wrote none.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 0.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollArrowKeyTest, UpAndDownTransposeBySemitoneAndByOctaveWithShift) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0) << "a transpose never moves a note in time";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 72) << "Shift is the octave jump";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);

    // Four presses, four undo steps.
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(f.undo.canUndo());
        f.undo.undo();
    }
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollArrowKeyTest, TransposeClampsTheWholeGroupAtThePitchExtremes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);

    const auto high = f.doc.addNote(clipId, makeNote(0.0, 120, 1.0));
    const auto higher = f.doc.addNote(clipId, makeNote(1.0, 126, 1.0));
    ASSERT_TRUE(high.isValid());
    ASSERT_TRUE(higher.isValid());
    f.roll.getSelectionForTest().setSelection({high, higher});

    // +12 would take 126 past the top, so the SHARED delta is clamped to +1 — the interval between
    // the two notes survives, which per-note clamping would have destroyed.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(high)->pitch, 121);
    EXPECT_EQ(f.doc.getNote(higher)->pitch, 127);

    // Same rule at the bottom.
    const auto low = f.doc.addNote(clipId, makeNote(2.0, 1, 1.0));
    const auto lower = f.doc.addNote(clipId, makeNote(3.0, 5, 1.0));
    ASSERT_TRUE(low.isValid());
    ASSERT_TRUE(lower.isValid());
    f.roll.getSelectionForTest().setSelection({low, lower});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(low)->pitch, 0);
    EXPECT_EQ(f.doc.getNote(lower)->pitch, 4);
}

TEST(PianoRollArrowKeyTest, ArrowKeysFallThroughWhenNothingIsSelected) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)).isValid());
    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());

    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.undo.canUndo());
}

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
// 8. Alt+Left/Right note navigation
// ============================================================================
//
// Selection-only: no test in this section may ever see the doc change or the undo stack grow.

namespace {

juce::KeyPress altArrow(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::altModifier, 0); }

// Four notes whose canonical (startBeat, pitch, id) order is A, B, C, D — deliberately ADDED in a
// different order, so a walk that followed insertion (or id) order instead of the doc's would fail.
struct NavBed {
    ClipId clipId;
    NoteId a, b, c, d;
};

NavBed makeNavBed(PianoRollFixture& f) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    NavBed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.b = f.doc.addNote(bed.clipId, makeNote(0.0, 64, 1.0)); // same start as A, higher pitch
    bed.d = f.doc.addNote(bed.clipId, makeNote(2.0, 60, 1.0));
    bed.a = f.doc.addNote(bed.clipId, makeNote(0.0, 60, 1.0));
    bed.c = f.doc.addNote(bed.clipId, makeNote(1.0, 62, 1.0));
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(bed.clipId);
    return bed;
}

// The selection is a single note, and it is this one — the assertion every navigation test ends on,
// because "collapses to a single note" and "picks the right one" are one claim here, not two.
//
// Returns an AssertionResult rather than EXPECT-ing inline so a caller can attach its own "why THIS
// note" rationale with <<, and so a failure names the ids involved instead of pointing at this
// helper's line.
::testing::AssertionResult onlySelected(PianoRollFixture& f, NoteId expected) {
    const auto selected = f.roll.getSelectionForTest().getSelected();
    if (selected.size() != 1u)
        return ::testing::AssertionFailure() << "expected exactly one selected note, got " << selected.size()
                                             << " (navigation must COLLAPSE a multi-selection)";
    if (selected[0] != expected)
        return ::testing::AssertionFailure()
               << "selected note is id " << selected[0].value << ", expected id " << expected.value;
    return ::testing::AssertionSuccess();
}

} // namespace

TEST(PianoRollNavigationTest, AltRightWalksTheDocsCanonicalNoteOrder) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    ASSERT_TRUE(bed.a.isValid());

    // Sanity: the doc really does hold them in (startBeat, pitch, id) order, which is the order the
    // walk below is asserting — not the order they were added in.
    const auto& notes = f.doc.getClip(bed.clipId)->notes;
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0].id, bed.a);
    EXPECT_EQ(notes[1].id, bed.b);
    EXPECT_EQ(notes[2].id, bed.c);
    EXPECT_EQ(notes[3].id, bed.d);

    f.roll.getSelectionForTest().setSelection({bed.a});

    // A -> B is the same-start, different-pitch step: the one an order keyed on startBeat alone
    // could not make.
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.b));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.c));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.d));

    EXPECT_FALSE(f.undo.canUndo()) << "navigation is selection, not document state";
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.a)->startBeat, 0.0) << "and it never moves a note";
}

TEST(PianoRollNavigationTest, AltLeftWalksBackThroughTheSameOrder) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    f.roll.getSelectionForTest().setSelection({bed.d});

    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.c));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.b));
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.a));
    EXPECT_FALSE(f.undo.canUndo());
}

// The anchor rule, which is the whole reason a multi-selection cannot be walked "from the
// selection": forward anchors on the LAST selected note, backward on the FIRST, so the step always
// lands OUTSIDE the block instead of back inside it.
TEST(PianoRollNavigationTest, NavigatingFromAMultiSelectionCollapsesOntoTheOuterNeighbour) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);

    f.roll.getSelectionForTest().setSelection({bed.b, bed.c});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.d)) << "forward anchors on the selection's LAST note";

    f.roll.getSelectionForTest().setSelection({bed.b, bed.c});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.a)) << "backward anchors on its FIRST";
}

// At either end the key is still CONSUMED and the selection kept — the same contract a fully
// clamped nudge honours (see NudgeClampsTheWholeGroupAtBothClipEdges).
TEST(PianoRollNavigationTest, AtEitherEndTheSelectionIsKeptAndTheKeyIsStillConsumed) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);

    f.roll.getSelectionForTest().setSelection({bed.d});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, bed.d));

    f.roll.getSelectionForTest().setSelection({bed.a});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, bed.a));

    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollNavigationTest, AltArrowsFallThroughWithNothingSelected) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    ASSERT_TRUE(bed.clipId.isValid());
    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());

    // There is plenty to navigate TO — what is missing is somewhere to navigate FROM, and that is
    // what makes the key fall through to the panel instead of picking a note arbitrarily.
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty());
    EXPECT_FALSE(f.undo.canUndo());
}

// Regression: adding Alt must not have changed what the UNmodified arrows do, and Alt+Up/Down stays
// reserved rather than quietly becoming a second transpose.
TEST(PianoRollNavigationTest, PlainArrowsStillNudgeAndAltUpDownIsLeftUnhandled) {
    PianoRollFixture f;
    const auto bed = makeNavBed(f);
    f.roll.getSelectionForTest().setSelection({bed.c});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.c)->startBeat, 2.0) << "a plain arrow still nudges by the grid";
    EXPECT_TRUE(onlySelected(f, bed.c)) << "and never changes WHICH notes are selected";

    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(bed.c)->pitch, 62) << "Alt+Up/Down transposes nothing";
    EXPECT_TRUE(onlySelected(f, bed.c)) << "and navigates nowhere";
}

// Navigating off-screen scrolls the roll's OWN horizontal mapping by the minimum that makes the
// target visible — no zoom change, and nothing at all when the target is already on screen.
TEST(PianoRollNavigationTest, NavigatingToAnOffScreenNoteScrollsItIntoView) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 64.0, "Clip");
    const auto nearId = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    const auto farId = f.doc.addNote(clipId, makeNote(40.0, 60, 1.0));
    ASSERT_TRUE(nearId.isValid());
    ASSERT_TRUE(farId.isValid());
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId, /*pixelsPerBeat*/ 40.0);

    // 900 px wide, 44 of them the keys gutter -> 856 px of grid -> 21.4 beats visible at this zoom.
    const double visibleBeats = (double)(900 - PianoRollComponent::kKeysColumnWidth) / f.roll.getPixelsPerBeat();
    ASSERT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), 0.0);

    f.roll.getSelectionForTest().setSelection({nearId});
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, farId));
    // Minimal scroll RIGHT: the note's trailing edge (beat 41) sits exactly on the grid's right edge.
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 41.0 - visibleBeats, 1e-9);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 40.0) << "a navigation never reframes the zoom";

    // …and back LEFT: the note's leading edge (beat 1) sits exactly on the grid's left edge.
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_TRUE(onlySelected(f, nearId));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 1.0, 1e-9);

    // An ON-screen step moves the view by nothing at all.
    const auto midId = f.doc.addNote(clipId, makeNote(5.0, 60, 1.0));
    ASSERT_TRUE(midId.isValid());
    const double before = f.roll.getFirstVisibleBeat();
    EXPECT_TRUE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, midId));
    EXPECT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), before);

    EXPECT_FALSE(f.undo.canUndo());
}

// ============================================================================
// 9. Rebindable surface keys (ShortcutManager-resolved)
// ============================================================================

namespace {

juce::KeyPress plainPress(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress shiftPress(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::shiftModifier, 0); }

// Every action id the roll resolves. A test pins ALL of them to an explicitly invalid KeyPress
// first, then spells out only the one or two it cares about — so nothing here can be rescued (or
// broken) by whatever ShortcutManager::resetToDefaults() happens to know about these ids in any
// given build. That matters because the defaults land in ShortcutManager in a separate phase.
const char* const kRollActionIds[] = {"pianoRollNudgeLeft",         "pianoRollNudgeRight",
                                      "pianoRollTransposeUp",       "pianoRollTransposeDown",
                                      "pianoRollTransposeOctaveUp", "pianoRollTransposeOctaveDown",
                                      "pianoRollNavPrevNote",       "pianoRollNavNextNote",
                                      "pianoRollQuantise",          "timelineSnapToggle"};

void clearRollBindings(ShortcutManager& mgr) {
    for (const char* id : kRollActionIds)
        mgr.setBinding(id, juce::KeyPress());
}

// One selected note at beat 2 of a 16-beat clip, snap pinned to quarters — the bed every key test
// below nudges, transposes or navigates from.
struct KeyBed {
    ClipId clipId;
    NoteId note;
};

KeyBed makeKeyBed(PianoRollFixture& f, double startBeat = 2.0) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    KeyBed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.note = f.doc.addNote(bed.clipId, makeNote(startBeat, 60, 1.0));
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(bed.clipId);
    f.roll.getSelectionForTest().setSelection({bed.note});
    return bed;
}

} // namespace

// The no-manager path is the one every other test in this file exercises implicitly; asserted here
// explicitly so a regression in matchesAction's fallback branch fails with an obvious name.
TEST(PianoRollShortcutTest, WithNoShortcutManagerTheHardcodedDefaultsStillApply) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);
    ASSERT_EQ(f.roll.getShortcutManager(), nullptr);

    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 3.0);
    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 2.0);

    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 61);
    EXPECT_TRUE(f.roll.keyPressed(shiftPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 73) << "Shift+Up is still the octave";
    EXPECT_TRUE(f.roll.keyPressed(shiftPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 60);

    ASSERT_TRUE(f.state.snapEnabled);
    EXPECT_TRUE(f.roll.keyPressed(plainPress('q'))) << "bare Q is still the snap toggle";
    EXPECT_FALSE(f.state.snapEnabled);
}

// Rebinding one action moves the gesture WHOLESALE: the factory key stops doing anything (it is no
// longer bound to anything the roll asks about) and the new key does the nudge.
TEST(PianoRollShortcutTest, RebindingNudgeRightMovesTheGestureToTheNewKey) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("pianoRollNudgeRight", plainPress('l'));
    f.roll.setShortcutManager(&mgr);
    EXPECT_EQ(f.roll.getShortcutManager(), &mgr);

    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)))
        << "the old key is not bound to this action any more, so it must fall through";
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "and it must not have written a document edit either";

    EXPECT_TRUE(f.roll.keyPressed(plainPress('l')));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 3.0);
    EXPECT_TRUE(f.undo.canUndo());

    // Dropping the manager restores the defaults — the fallback is not a one-time decision.
    f.roll.setShortcutManager(nullptr);
    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 4.0);
}

// With a manager installed, an action whose binding is unset/invalid has NO key at all — it must
// not quietly fall back to its hardcoded default (that would resurrect a key the user cleared).
// The same rule covers an id this ShortcutManager has never heard of, which is what getBinding
// answers with an invalid KeyPress.
TEST(PianoRollShortcutTest, AnUnboundActionHasNoKeyAndNeverFallsBackToItsDefault) {
    EXPECT_FALSE(ShortcutManager().getBinding("someActionNobodyRegistered").isValid())
        << "the contract this test depends on: an unknown id resolves to an INVALID KeyPress";

    PianoRollFixture f;
    const auto bed = makeKeyBed(f);
    const bool snapBefore = f.state.snapEnabled;

    ShortcutManager mgr;
    clearRollBindings(mgr);
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.roll.keyPressed(shiftPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(shiftPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress('q')));
    EXPECT_FALSE(f.roll.keyPressed(shiftPress('q')));

    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 2.0);
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 60);
    EXPECT_EQ(f.state.snapEnabled, snapBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_FALSE(f.roll.isQuantiseFlashingForTest()) << "an unbound Q never even flashes the button";
}

// The snap toggle and the one-shot quantise are two independent bindings, and the quantise action
// is resolved FIRST — so a user who mirrors the factory pair (X / Shift+X) onto another letter gets
// the same behaviour rather than the toggle swallowing both.
TEST(PianoRollShortcutTest, SnapToggleAndQuantiseFollowTheirOwnBindings) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f, /*startBeat*/ 1.1); // off-grid, so a quantise is observable
    f.roll.getSelectionForTest().clear();              // quantise-all, no selection needed

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("timelineSnapToggle", plainPress('g'));
    mgr.setBinding("pianoRollQuantise", shiftPress('g'));
    f.roll.setShortcutManager(&mgr);

    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };
    ASSERT_TRUE(f.state.snapEnabled);

    EXPECT_FALSE(f.roll.keyPressed(plainPress('q'))) << "Q is no longer either of these actions";
    EXPECT_FALSE(f.roll.keyPressed(shiftPress('q')));
    EXPECT_EQ(toggles, 0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 1.1);

    EXPECT_TRUE(f.roll.keyPressed(plainPress('g')));
    EXPECT_FALSE(f.state.snapEnabled);
    EXPECT_EQ(toggles, 1);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 1.1) << "a snap toggle never moves a note";

    EXPECT_TRUE(f.roll.keyPressed(shiftPress('g')));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 1.0) << "Shift+G quantised, from the RAW division";
    EXPECT_FALSE(f.state.snapEnabled) << "and never flipped the switch";
    EXPECT_EQ(toggles, 1);
}

// Rebinding the octave transpose alone must not shadow (or be shadowed by) the semitone one — the
// two are separate actions, and Shift+Up is only "the octave" because that is its DEFAULT.
TEST(PianoRollShortcutTest, RebindingTheOctaveTransposeLeavesTheSemitoneOneAlone) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("pianoRollTransposeUp", plainPress(juce::KeyPress::upKey));
    mgr.setBinding("pianoRollTransposeOctaveUp", plainPress('u'));
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(shiftPress(juce::KeyPress::upKey)))
        << "Shift+Up is bound to nothing now, and must NOT decay into the plain transpose";
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 60);

    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 61);
    EXPECT_TRUE(f.roll.keyPressed(plainPress('u')));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 73);
}

// Alt+Left/Right stay resolvable too — and navigation is still selection-only, never an undo step.
TEST(PianoRollShortcutTest, NoteNavigationIsRebindableAndStillNeverTouchesTheDoc) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    const auto first = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0));
    const auto second = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    f.roll.getSelectionForTest().setSelection({first});

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("pianoRollNavNextNote", plainPress(']'));
    mgr.setBinding("pianoRollNavPrevNote", plainPress('['));
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, first));

    EXPECT_TRUE(f.roll.keyPressed(plainPress(']')));
    EXPECT_TRUE(onlySelected(f, second));
    EXPECT_TRUE(f.roll.keyPressed(plainPress('[')));
    EXPECT_TRUE(onlySelected(f, first));
    EXPECT_FALSE(f.undo.canUndo()) << "navigation is selection-only";
}

// Escape and Delete/Backspace are platform conventions, not app shortcuts: they answer identically
// with a manager installed and every roll action explicitly unbound.
TEST(PianoRollShortcutTest, EscapeAndDeleteStayFixedRegardlessOfTheManager) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);

    ShortcutManager mgr;
    clearRollBindings(mgr);
    f.roll.setShortcutManager(&mgr);

    bool closeRequested = false;
    f.roll.onCloseRequested = [&] { closeRequested = true; };

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty()) << "first Escape clears the selection";
    EXPECT_TRUE(f.roll.isOpen());
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);

    f.roll.openClip(bed.clipId);
    f.roll.getSelectionForTest().setSelection({bed.note});
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getNote(bed.note), nullptr);
    EXPECT_TRUE(f.undo.canUndo()) << "and it is still one undo step";
}

// ============================================================================
// 10. Wheel policy (ScrollPolicy.h) and the anchored zoom commands
// ============================================================================

namespace {

// The macOS axis swap, reproduced exactly: the OS moves a Shift-held wheel gesture into deltaX and
// leaves deltaY at 0. Any branch that reads deltaY alone receives nothing at all.
juce::MouseWheelDetails wheelOnX(float deltaX) {
    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaX = deltaX;
    wheel.deltaY = 0.0f;
    return wheel;
}

juce::MouseWheelDetails wheelOnY(float deltaY) {
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = deltaY;
    return wheel;
}

} // namespace

// THE regression this fixes: Cmd+Shift+wheel was reading wheel.deltaY, which macOS zeroes under
// Shift, so the vertical zoom was dead on the platform it was written on. dominantWheelDelta picks
// up whichever axis the gesture landed on.
TEST(PianoRollWheelTest, CmdShiftWheelZoomsVerticallyEvenWhenTheGestureArrivesOnDeltaX) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    const int mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheelOnX(0.4f));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "reading deltaY alone here would have been a no-op";

    // And the opposite gesture cancels it exactly (the factor is exponential in the delta).
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheelOnX(-0.4f));
    EXPECT_NEAR(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone, 1.0e-9);

    EXPECT_DOUBLE_EQ(f.state.pixelsPerBeat, 40.0) << "a vertical zoom never touches the shared view state";
}

// Same robustness for the horizontal-zoom branch: it is chosen by the modifier, so it must not care
// which axis carried the gesture either.
TEST(PianoRollWheelTest, CmdWheelZoomsHorizontallyEvenWhenTheGestureArrivesOnDeltaX) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);

    const float anchorX = 300.0f;
    const double beatUnderCursor = f.roll.xToBeat((double)anchorX);
    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, juce::ModifierKeys::commandModifier), wheelOnX(0.5f));

    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0);
    EXPECT_NEAR(f.roll.xToBeat((double)anchorX), beatUnderCursor, 1.0e-9)
        << "still anchored on the beat under the cursor";
}

// Plain wheel = pitch scroll. Natural (the default) matches what a juce::Viewport does with the same
// gesture: firstVisiblePitch_ is the TOP row's pitch, so scrolling towards the top of the content
// RAISES it. setScrollInverted flips exactly that, and nothing else.
TEST(PianoRollWheelTest, PitchScrollFollowsTheGestureByDefaultAndFlipsWhenInverted) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_FALSE(f.roll.isScrollInverted()) << "natural is the default";

    const int base = f.roll.getFirstVisiblePitchForTest();
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.5f));
    const int natural = f.roll.getFirstVisiblePitchForTest();
    EXPECT_GT(natural, base) << "a +deltaY gesture scrolls towards HIGHER pitches";

    // Undo it, then run the identical gesture inverted: it must land the same distance the other way.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(-0.5f));
    ASSERT_EQ(f.roll.getFirstVisiblePitchForTest(), base);

    f.roll.setScrollInverted(true);
    EXPECT_TRUE(f.roll.isScrollInverted());
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.5f));
    const int inverted = f.roll.getFirstVisiblePitchForTest();
    EXPECT_LT(inverted, base) << "the same gesture now scrolls the other way";
    EXPECT_EQ(base - inverted, natural - base) << "and by exactly the same number of rows";
}

// Shift+wheel (and a trackpad's own deltaX) = horizontal scroll, through the roll's OWN scroll
// origin. Same natural-by-default / invert-on-request contract as the pitch axis.
TEST(PianoRollWheelTest, HorizontalScrollFollowsTheGestureByDefaultAndFlipsWhenInverted) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 16.0, "Clip");
    f.open(clipId); // 40 px/beat, beat 4 at the gutter
    ASSERT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), 4.0);

    // 200 px per wheel unit at 40 px/beat -> 5 beats per unit, so half a unit is 2.5 beats.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheelOnY(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 6.5, 1.0e-9) << "a -delta gesture scrolls the view RIGHT";
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0) << "the lanes behind the roll keep their own scroll";

    f.roll.setScrollInverted(true);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheelOnY(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 4.0, 1.0e-9) << "the same gesture now scrolls the view LEFT";

    // A bare trackpad deltaX (no Shift at all) is the same branch and obeys the same flag.
    f.roll.setScrollInverted(false);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnX(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 6.5, 1.0e-9);
    f.roll.setScrollInverted(true);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnX(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 4.0, 1.0e-9);
}

// zoomHorizontal is the command-friendly form of the Cmd+wheel zoom: same anchored math, anchored on
// the view centre instead of the cursor.
TEST(PianoRollZoomApiTest, ZoomHorizontalKeepsTheCentreBeatAndClampsAtBothEnds) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 32.0, "Clip");
    f.open(clipId, /*pixelsPerBeat*/ 40.0);

    // 900 px wide, 44 of them the keys gutter -> the grid's centre sits at x == 44 + 428.
    const double centreX = (double)PianoRollComponent::kKeysColumnWidth + (900.0 - 44.0) * 0.5;
    const double centreBeat = f.roll.xToBeat(centreX);

    int viewChanges = 0;
    f.roll.onHorizontalViewChanged = [&] { ++viewChanges; };

    f.roll.zoomHorizontal(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 80.0);
    EXPECT_NEAR(f.roll.xToBeat(centreX), centreBeat, 1.0e-9) << "the centre beat is the zoom's fixed point";
    EXPECT_GT(viewChanges, 0) << "the ruler mirrors the roll's mapping, so it has to be told";

    f.roll.zoomHorizontal(0.5);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 40.0) << "equal-and-opposite factors cancel";
    EXPECT_NEAR(f.roll.xToBeat(centreX), centreBeat, 1.0e-9);

    // A factor that cannot mean anything is ignored rather than clamped to something.
    f.roll.zoomHorizontal(0.0);
    f.roll.zoomHorizontal(-2.0);
    f.roll.zoomHorizontal(std::nan(""));
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 40.0);

    for (int i = 0; i < 20; ++i)
        f.roll.zoomHorizontal(4.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), TimelineViewState::kMaxPixelsPerBeat);

    for (int i = 0; i < 40; ++i)
        f.roll.zoomHorizontal(0.25);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), TimelineViewState::kMinPixelsPerBeat);
}

TEST(PianoRollZoomApiTest, ZoomVerticalKeepsTheCentrePitchAndClampsAtBothEnds) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    // 160 px tall, 20 of them the header -> the grid's centre row sits at y == 20 + 70.
    const int centreY = PianoRollComponent::kHeaderHeight + (160 - PianoRollComponent::kHeaderHeight) / 2;
    const int centrePitch = f.roll.pitchForY(centreY);

    f.roll.zoomVertical(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), 20.0);
    // firstVisiblePitch_ is an int, so the anchor holds to within one row by design — the same
    // tolerance the wheel and pinch zooms accept.
    EXPECT_LE(std::abs(f.roll.pitchForY(centreY) - centrePitch), 1) << "the centre pitch stays put";

    f.roll.zoomVertical(0.0);
    f.roll.zoomVertical(-2.0);
    f.roll.zoomVertical(std::nan(""));
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), 20.0) << "a meaningless factor is ignored";

    for (int i = 0; i < 20; ++i)
        f.roll.zoomVertical(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMaxPixelsPerSemitone);

    for (int i = 0; i < 40; ++i)
        f.roll.zoomVertical(0.5);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMinPixelsPerSemitone);

    EXPECT_DOUBLE_EQ(f.state.pixelsPerBeat, 40.0) << "vertical zoom is the roll's alone";
    EXPECT_FALSE(f.undo.canUndo()) << "every zoom here is view-only";
}

// ============================================================================
// 11. Wheel-zoom DIRECTION convention: UP (the PHYSICAL gesture) zooms IN by default
// ============================================================================
// wheelGestureIsUpward (ScrollPolicy.h) recovers the physical direction from `isReversed` XOR the
// delta's sign, so "wheel up zooms in" must read identically whichever way the OS's natural-
// scrolling setting has pre-flipped the delta. These tests exercise BOTH isReversed encodings of
// "up" and "down" against both wheel-zoom branches, setZoomScrollInverted_'s effect, and that the
// resulting factor depends only on |delta| and physical direction — never on isReversed itself.
//
// The section 10 axis-swap tests above (CmdWheelZoomsAroundTheCursorBeat,
// CmdShiftWheelZoomsPitchRowsWithinClamps, and the two …EvenWhenTheGestureArrivesOnDeltaX tests) all
// construct their wheels with isReversed left at its value-initialised `false`, so their expected
// GT/LT directions are unaffected by this change (wheelGestureIsUpward(false, +delta) ==
// (dominantWheelDelta(wheel) > 0), the same sign the old raw-delta code read) — they are left as
// they were, deliberately not touched here.

namespace {
// Builds a wheel gesture for the PHYSICAL direction `up`, under a given isReversed encoding — the
// same algebra wheelGestureIsUpward itself runs: isReversed==false needs a POSITIVE delta to read as
// "up"; isReversed==true needs a NEGATIVE one. Letting a test pick `up` directly (rather than a raw
// delta sign) is the point: two calls with the same `up` and different `isReversed` must be
// answered identically by mouseWheelMove, which is exactly what these tests check.
juce::MouseWheelDetails physicalWheelGesture(float magnitude, bool up, bool isReversed) {
    juce::MouseWheelDetails wheel{};               // value-initialised: the struct has no default member initialisers
    const bool positiveDelta = (up != isReversed); // XOR
    wheel.deltaY = positiveDelta ? magnitude : -magnitude;
    wheel.isReversed = isReversed;
    return wheel;
}
} // namespace

TEST(PianoRollWheelZoomDirectionTest, HorizontalWheelUpZoomsInAndDownZoomsOutRegardlessOfIsReversed) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);
    const auto mods = juce::ModifierKeys::commandModifier;
    const float anchorX = 300.0f;

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, false));
    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "up, isReversed=false (+delta) zooms in";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, true));
    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "up, isReversed=true (-delta) also zooms in — same physical gesture";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ false, false));
    EXPECT_LT(f.roll.getPixelsPerBeat(), 40.0) << "down, isReversed=false (-delta) zooms out";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ false, true));
    EXPECT_LT(f.roll.getPixelsPerBeat(), 40.0) << "down, isReversed=true (+delta) also zooms out";
}

TEST(PianoRollWheelZoomDirectionTest, VerticalWheelUpZoomsInAndDownZoomsOutRegardlessOfIsReversed) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);
    const auto mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ true, false));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone) << "up, isReversed=false zooms in";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ true, true));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "up, isReversed=true also zooms in";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ false, false));
    EXPECT_LT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "down, isReversed=false zooms out";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ false, true));
    EXPECT_LT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "down, isReversed=true also zooms out";
}

// setZoomScrollInverted is the zoom-only preference, independent of setScrollInverted (which the
// PitchScrollFollowsTheGestureByDefaultAndFlipsWhenInverted / HorizontalScrollFollowsThe…
// tests above cover for the plain-scroll branches) — it flips BOTH wheel-zoom branches together.
TEST(PianoRollWheelZoomDirectionTest, SetZoomScrollInvertedFlipsBothWheelZoomBranches) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_FALSE(f.roll.isZoomScrollInverted()) << "natural (up zooms in) is the default";
    f.roll.setZoomScrollInverted(true);
    EXPECT_TRUE(f.roll.isZoomScrollInverted());

    const auto cmdMods = juce::ModifierKeys::commandModifier;
    const auto cmdShiftMods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    // Horizontal (Cmd+wheel): physical up now zooms OUT, down now zooms IN.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdMods), physicalWheelGesture(0.5f, /*up*/ true, false));
    EXPECT_LT(f.roll.getPixelsPerBeat(), 40.0) << "inverted: up zooms out";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdMods), physicalWheelGesture(0.5f, /*up*/ false, false));
    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "inverted: down zooms in";
    f.roll.setHorizontalView(40.0, 4.0);

    // Vertical (Cmd+Shift+wheel): the same flip, so the two branches never disagree.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdShiftMods),
                          physicalWheelGesture(0.4f, /*up*/ true, false));
    EXPECT_LT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone) << "inverted: up zooms out";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdShiftMods),
                          physicalWheelGesture(0.4f, /*up*/ false, false));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone) << "inverted: down zooms in";
}

// The zoom factor must depend on |delta| and physical direction alone — never on isReversed's own
// value, which is only a SIGN-recovery input, not a second source of magnitude or direction.
TEST(PianoRollWheelZoomDirectionTest, MagnitudeMatchesRegardlessOfIsReversedSignForTheSamePhysicalGesture) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);
    const auto mods = juce::ModifierKeys::commandModifier;

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, false));
    const double afterNatural = f.roll.getPixelsPerBeat();
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, true));
    const double afterReversed = f.roll.getPixelsPerBeat();

    EXPECT_DOUBLE_EQ(afterNatural, afterReversed)
        << "same physical gesture, same |delta| -> the exact same zoom factor regardless of isReversed";
}

// ============================================================================
// 12. Key labels (paintKeysColumn's pure per-row decision)
// ============================================================================

TEST(PianoRollKeyLabelTest, AllNotesLabelsEveryRowAtOrAboveTheReadabilityFloor) {
    using Mode = PianoRollComponent::KeyLabelMode;
    EXPECT_EQ(PianoRollComponent::keyLabelFor(60, Mode::AllNotes, 12), "C4");
    EXPECT_EQ(PianoRollComponent::keyLabelFor(61, Mode::AllNotes, 12), "C#4");
    EXPECT_EQ(PianoRollComponent::keyLabelFor(62, Mode::AllNotes, 12), "D4");
    EXPECT_EQ(PianoRollComponent::keyLabelFor(71, Mode::AllNotes, 12), "B4");
    EXPECT_EQ(PianoRollComponent::keyLabelFor(72, Mode::AllNotes, 12), "C5") << "octave rolls over at C";
}

TEST(PianoRollKeyLabelTest, RowsBelowTheReadabilityFloorFallBackToCOnlyEvenInAllNotes) {
    using Mode = PianoRollComponent::KeyLabelMode;
    EXPECT_EQ(PianoRollComponent::keyLabelFor(60, Mode::AllNotes, 8), "C4") << "the C row still labels";
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(61, Mode::AllNotes, 8).isEmpty())
        << "below the floor, even AllNotes only labels the Cs";
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(62, Mode::AllNotes, 8).isEmpty());
}

TEST(PianoRollKeyLabelTest, OctavesOnlyAlwaysLabelsOnlyTheCsRegardlessOfRowHeight) {
    using Mode = PianoRollComponent::KeyLabelMode;
    EXPECT_EQ(PianoRollComponent::keyLabelFor(60, Mode::OctavesOnly, 12), "C4");
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(61, Mode::OctavesOnly, 12).isEmpty());
    EXPECT_EQ(PianoRollComponent::keyLabelFor(60, Mode::OctavesOnly, 40), "C4")
        << "OctavesOnly stays C-only even at a tall row height";
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(64, Mode::OctavesOnly, 40).isEmpty());
}

TEST(PianoRollKeyLabelTest, OutOfMidiRangePitchYieldsNoLabelInEitherMode) {
    using Mode = PianoRollComponent::KeyLabelMode;
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(-1, Mode::AllNotes, 12).isEmpty());
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(128, Mode::AllNotes, 12).isEmpty());
    EXPECT_TRUE(PianoRollComponent::keyLabelFor(-1, Mode::OctavesOnly, 12).isEmpty());
}

TEST(PianoRollKeyLabelTest, DefaultModeIsAllNotesAndIsSettable) {
    PianoRollFixture f;
    EXPECT_EQ(f.roll.getKeyLabelMode(), PianoRollComponent::KeyLabelMode::AllNotes);
    f.roll.setKeyLabelMode(PianoRollComponent::KeyLabelMode::OctavesOnly);
    EXPECT_EQ(f.roll.getKeyLabelMode(), PianoRollComponent::KeyLabelMode::OctavesOnly);
}

// ============================================================================
// 13. Row mapping (visiblePitches_, yForPitch/pitchForY, scale context)
// ============================================================================

namespace {
// C major, root C (pitch class 0): C D E F G A B.
bool cMajorContains(int pitch) {
    static const bool kInScale[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
    return kInScale[(size_t)(((pitch % 12) + 12) % 12)];
}
} // namespace

TEST(PianoRollRowMappingTest, NoScaleContextEveryPitchIsVisibleAndMappingRoundTrips) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    ASSERT_EQ(f.roll.getVisiblePitchesForTest().size(), 128u);
    for (int pitch = 0; pitch <= 127; ++pitch) {
        const int y = f.roll.yForPitch(pitch);
        EXPECT_EQ(f.roll.pitchForY(y), pitch) << "pitch " << pitch;
    }
}

TEST(PianoRollRowMappingTest, ScaleContextWithVisibilityOffNeverHidesARow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ false);
    EXPECT_EQ(f.roll.getVisiblePitchesForTest().size(), 128u)
        << "a scale that only affects colouring must never collapse a row";
}

TEST(PianoRollRowMappingTest, VisibilityOnCollapsesEmptyOutOfScaleRowsButKeepsNotedOnes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    // C# (61) is out of C major and has a note; D# (63) is out of C major and has none.
    f.doc.addNote(clipId, makeNote(1.0, 61));
    f.open(clipId);

    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ true);
    const auto& visible = f.roll.getVisiblePitchesForTest();

    EXPECT_TRUE(std::binary_search(visible.begin(), visible.end(), 60)) << "in-scale pitch stays visible";
    EXPECT_TRUE(std::binary_search(visible.begin(), visible.end(), 61))
        << "out-of-scale pitch with a note in the open clip is never hidden";
    EXPECT_FALSE(std::binary_search(visible.begin(), visible.end(), 63))
        << "out-of-scale pitch with no note collapses out of the grid";

    // pitchForY must never land on a collapsed row, at any y in the visible component.
    for (int y = PianoRollComponent::kHeaderHeight; y < f.roll.getHeight(); ++y)
        EXPECT_NE(f.roll.pitchForY(y), 63);
}

TEST(PianoRollRowMappingTest, RoundTripThroughVisibleRowsHoldsWithFilteringActive) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ true);

    for (const int pitch : f.roll.getVisiblePitchesForTest()) {
        const int y = f.roll.yForPitch(pitch);
        EXPECT_EQ(f.roll.pitchForY(y), pitch) << "pitch " << pitch;
    }
}

TEST(PianoRollRowMappingTest, FirstVisiblePitchIsAlwaysAMemberOfVisiblePitches) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.doc.addNote(clipId, makeNote(1.0, 61)); // median pitch 61 -> openClip parks the view above it
    f.open(clipId);

    const int beforeScale = f.roll.getFirstVisiblePitchForTest();
    ASSERT_EQ(beforeScale, 68) << "openClip's median-centred framing for this fixture's clip/window size";
    ASSERT_FALSE(cMajorContains(beforeScale)) << "and it must be OUT of C major for this test to mean anything";

    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ true);
    const auto& visible = f.roll.getVisiblePitchesForTest();
    EXPECT_TRUE(std::binary_search(visible.begin(), visible.end(), f.roll.getFirstVisiblePitchForTest()))
        << "firstVisiblePitch_ must be a member of visiblePitches_ right after the rebuild";
    EXPECT_NE(f.roll.getFirstVisiblePitchForTest(), beforeScale)
        << "the pre-rebuild pitch was out-of-scale and unnoted, so the rebuild must have moved it";

    // Clearing the scale context rebuilds back to every pitch, and the invariant must still hold.
    f.roll.setScaleContext({}, false);
    EXPECT_EQ(f.roll.getVisiblePitchesForTest().size(), 128u);
    EXPECT_GE(f.roll.getFirstVisiblePitchForTest(), 0);
    EXPECT_LE(f.roll.getFirstVisiblePitchForTest(), 127);
}

// ============================================================================
// 14. Note colouring through synth::ui::resolveNoteColour (NoteColour.h)
// ============================================================================

TEST(PianoRollNoteColourTest, OutOfScaleNoteResolvesThroughTheSharedResolverToItsOutOfScaleFamily) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 61, 1.0)); // C#, out of C major
    f.open(clipId);
    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ false);

    const auto* note = f.doc.getNote(id);
    ASSERT_NE(note, nullptr);
    const auto resolved = f.roll.notePaintFor(*note);

    // Ground truth: the SAME resolver, fed the SAME no-LookAndFeel fallback (a default-constructed
    // Colors — this headless fixture's roll has no AppLookAndFeel installed) and outOfScale=true,
    // is exactly what paintNote is expected to have produced.
    const auto expected = synth::ui::resolveNoteColour(synth::theme::Colors{}, note->pitch, note->velocity,
                                                       /*selected*/ false, /*muted*/ false, /*outOfScale*/ true,
                                                       synth::ui::NoteColourOverrides{});
    EXPECT_EQ(resolved.fill, expected.fill);
    EXPECT_EQ(resolved.border, expected.border);

    // And it must genuinely differ from how the SAME note would resolve if it were in scale —
    // otherwise the scale context would not be doing anything visible at all.
    const auto asIfInScale = synth::ui::resolveNoteColour(synth::theme::Colors{}, note->pitch, note->velocity, false,
                                                          false, /*outOfScale*/ false,
                                                          synth::ui::NoteColourOverrides{});
    EXPECT_NE(resolved.fill, asIfInScale.fill);
}

TEST(PianoRollNoteColourTest, InScaleNoteIsUnaffectedByInstallingTheScaleContext) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // C, in C major
    f.open(clipId);

    const auto* note = f.doc.getNote(id);
    ASSERT_NE(note, nullptr);

    const auto before = f.roll.notePaintFor(*note); // no scale context yet
    f.roll.setScaleContext(cMajorContains, false);
    const auto after = f.roll.notePaintFor(*note);

    EXPECT_EQ(before.fill, after.fill);
    EXPECT_EQ(before.border, after.border);
}

TEST(PianoRollNoteColourTest, EmptyScaleContextMeansNothingIsEverOutOfScale) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    // Every pitch class in a chromatic run — none of them can be "out of scale" with no scale
    // installed at all, regardless of pitchVisibilityOn.
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 61, 1.0));
    f.open(clipId);
    f.roll.setScaleContext({}, /*pitchVisibilityOn*/ true);

    const auto* note = f.doc.getNote(id);
    ASSERT_NE(note, nullptr);
    const auto resolved = f.roll.notePaintFor(*note);
    const auto expected = synth::ui::resolveNoteColour(synth::theme::Colors{}, note->pitch, note->velocity, false,
                                                        false, /*outOfScale*/ false, synth::ui::NoteColourOverrides{});
    EXPECT_EQ(resolved.fill, expected.fill);
    EXPECT_EQ(resolved.border, expected.border);
}

// ============================================================================
// 15. Keys-column geometry seam (piano-style rendering) and a scale-context snapshot smoke
// ============================================================================

TEST(PianoRollKeysColumnTest, BlackKeyInsetIsNarrowerThanTheFullColumnWidth) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int inset = f.roll.blackKeyInsetForTest();
    EXPECT_GT(inset, 0);
    EXPECT_LT(inset, f.roll.getKeysColumnBounds().getWidth())
        << "a black key must draw narrower than the full column width, so the white-key colour "
           "still shows through on its right — the gap between black keys on a real keyboard";
}

TEST(PianoRollKeysColumnTest, SnapshotSmokeWithScaleContextAndAllNotesLabelsActive) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.doc.addNote(clipId, makeNote(1.0, 61, 1.0));
    f.open(clipId);
    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ true);
    f.roll.setKeyLabelMode(PianoRollComponent::KeyLabelMode::AllNotes);

    f.roll.setSize(900, 160);
    const juce::Image img = f.roll.createComponentSnapshot(f.roll.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 900);
    EXPECT_EQ(img.getHeight(), 160);
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

    f.roll.tickAutoScrollForTest();
    const int afterOneTick = f.roll.getFirstVisiblePitchForTest();
    EXPECT_GT(afterOneTick, startPitch) << "near the TOP edge the view walks upward (higher pitches)";

    const auto& visible = f.roll.getVisiblePitchesForTest();
    EXPECT_TRUE(std::binary_search(visible.begin(), visible.end(), afterOneTick))
        << "the walked-to row must itself be a VISIBLE pitch, never one the scale filter collapsed";
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

// ============================================================================
// 17. Scale assist — the header button + gutter shift, quantisePitchesToScale,
//     generateRandomNotesIntoClip, per-clip memory, PropertiesFile persistence, and the
//     ScaleAssistPanel component in isolation.
// ============================================================================

// ---- Header button: toggles the panel, shifts the grid gutter, hit-testing still works ----

TEST(PianoRollScaleAssistTest, HeaderButtonTogglesPanelAndShiftsGutter) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    ASSERT_FALSE(f.roll.getScaleAssistPanel().isVisible());
    const double beatXBefore = f.roll.beatToX(0.0);
    const int keysXBefore = f.roll.getKeysColumnBounds().getX();

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getScaleButtonBounds())));
    EXPECT_TRUE(f.roll.getScaleAssistPanel().isVisible());
    EXPECT_DOUBLE_EQ(f.roll.beatToX(0.0), beatXBefore + (double)PianoRollComponent::kScalePanelWidth)
        << "opening the panel shifts the first visible beat by exactly its width";
    EXPECT_EQ(f.roll.getKeysColumnBounds().getX(), keysXBefore + PianoRollComponent::kScalePanelWidth)
        << "the keys column moves with it";

    // A note under the SHIFTED mapping is still hit-testable at its (also shifted) rect.
    const int pitch = f.roll.getFirstVisiblePitchForTest() - 2; // a row comfortably inside the grid
    const auto id = f.doc.addNote(clipId, makeNote(1.0, pitch));
    const auto rect = f.roll.getNoteRect(id);
    EXPECT_GE(rect.getX(), f.roll.getKeysColumnBounds().getRight()) << "the note rect itself moved with the gutter";
    f.roll.mouseDown(leftClick(f.roll, centreOf(rect)));
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(id));

    // Clicking again closes it and restores the original gutter.
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getScaleButtonBounds())));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
    EXPECT_DOUBLE_EQ(f.roll.beatToX(0.0), beatXBefore);
    EXPECT_EQ(f.roll.getKeysColumnBounds().getX(), keysXBefore);
}

TEST(PianoRollScaleAssistTest, TooltipReportsForTheScaleButton) {
    PianoRollFixture f;
    EXPECT_EQ(f.roll.getTooltipFor(centreOf(f.roll.getScaleButtonBounds()).toInt()),
              juce::String(PianoRollComponent::kScaleTooltip));
}

// ---- quantisePitchesToScale ----

TEST(PianoRollScaleAssistTest, QuantiseMovesOnlyTheSelectionAndHonoursTheTieBreak) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const auto majorInC = synth::makeScale(0, 0); // presets[0] == "Major"
    ASSERT_EQ(juce::String(majorInC.name), "Major");

    // C# (61) is equidistant from C (60) and D (62), both in scale — snapPitch's tie-break must
    // land on the LOWER pitch.
    const auto outOfScale = f.doc.addNote(clipId, makeNote(0.0, 61));
    const auto alreadyIn = f.doc.addNote(clipId, makeNote(1.0, 62));

    f.roll.getSelectionForTest().setSelection({outOfScale});
    f.roll.quantisePitchesToScale(majorInC);

    EXPECT_EQ(f.doc.getNote(outOfScale)->pitch, 60) << "tie resolves to the lower pitch";
    EXPECT_EQ(f.doc.getNote(alreadyIn)->pitch, 62) << "the unselected note is untouched";
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(outOfScale)) << "the selection itself is not changed";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(outOfScale)->pitch, 61) << "ONE undo step restores the original pitch";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollScaleAssistTest, QuantiseMovesEveryNoteWhenNothingIsSelected) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const auto majorInC = synth::makeScale(0, 0);

    const auto a = f.doc.addNote(clipId, makeNote(0.0, 61));
    const auto b = f.doc.addNote(clipId, makeNote(1.0, 66)); // F# — not in C major

    ASSERT_TRUE(f.roll.getSelectionForTest().isEmpty());
    f.roll.quantisePitchesToScale(majorInC);

    EXPECT_TRUE(majorInC.contains(f.doc.getNote(a)->pitch));
    EXPECT_TRUE(majorInC.contains(f.doc.getNote(b)->pitch));
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(a)->pitch, 61);
    EXPECT_EQ(f.doc.getNote(b)->pitch, 66);
}

TEST(PianoRollScaleAssistTest, QuantiseNoOpPushesNoUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const auto majorInC = synth::makeScale(0, 0);
    f.doc.addNote(clipId, makeNote(0.0, 60)); // already in scale

    f.roll.quantisePitchesToScale(majorInC);
    EXPECT_FALSE(f.undo.canUndo()) << "every note was already in scale: no-op pushes nothing";
}

// ---- generateRandomNotesIntoClip ----

TEST(PianoRollScaleAssistTest, GenerateReplacesTheClipInOneUndoStep) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip"); // Quarter snap -> 4 grid steps
    f.open(clipId);
    const auto oldNote = f.doc.addNote(clipId, makeNote(0.5, 77));

    const auto majorInC = synth::makeScale(0, 0);
    juce::Random rng(12345);
    f.roll.generateRandomNotesIntoClip(&majorInC, 60, 71, rng); // one octave, C4..B4

    const auto* clip = f.doc.getClip(clipId);
    ASSERT_EQ(clip->notes.size(), 4u) << "one note per 1-beat grid step across a 4-beat clip";
    for (std::size_t i = 0; i < clip->notes.size(); ++i) {
        const auto& note = clip->notes[i];
        EXPECT_DOUBLE_EQ(note.startBeat, (double)i) << "notes land on divisionBeatsRaw multiples";
        EXPECT_DOUBLE_EQ(note.lengthBeats, 1.0);
        EXPECT_GE(note.pitch, 60);
        EXPECT_LE(note.pitch, 71);
        EXPECT_TRUE(majorInC.contains(note.pitch)) << "every generated pitch is in scale";
    }

    const auto selected = f.roll.getSelectionForTest().getSelected();
    ASSERT_EQ(selected.size(), 4u) << "the generated notes become the selection";
    for (const auto& note : clip->notes)
        EXPECT_TRUE(f.roll.getSelectionForTest().contains(note.id));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    const auto* restored = f.doc.getClip(clipId);
    ASSERT_EQ(restored->notes.size(), 1u) << "ONE undo step restores the pre-generation contents";
    EXPECT_EQ(restored->notes[0].id, oldNote);
    EXPECT_DOUBLE_EQ(restored->notes[0].startBeat, 0.5);
    EXPECT_EQ(restored->notes[0].pitch, 77);
}

TEST(PianoRollScaleAssistTest, GenerateIsDeterministicForTheSameSeed) {
    PianoRollFixture f1;
    const auto track1 = f1.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clip1 = f1.doc.addClip(track1, 0.0, 4.0, "Clip");
    f1.open(clip1);

    PianoRollFixture f2;
    const auto track2 = f2.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clip2 = f2.doc.addClip(track2, 0.0, 4.0, "Clip");
    f2.open(clip2);

    juce::Random r1(555);
    juce::Random r2(555);
    f1.roll.generateRandomNotesIntoClip(nullptr, 40, 50, r1);
    f2.roll.generateRandomNotesIntoClip(nullptr, 40, 50, r2);

    const auto& notes1 = f1.doc.getClip(clip1)->notes;
    const auto& notes2 = f2.doc.getClip(clip2)->notes;
    ASSERT_EQ(notes1.size(), notes2.size());
    ASSERT_FALSE(notes1.empty());
    for (std::size_t i = 0; i < notes1.size(); ++i) {
        EXPECT_EQ(notes1[i].pitch, notes2[i].pitch) << "same seed -> same draw at index " << i;
        EXPECT_DOUBLE_EQ(notes1[i].startBeat, notes2[i].startBeat);
    }
}

TEST(PianoRollScaleAssistTest, GenerateWithNullScaleAllowsAnyPitchInRange) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 2.0, "Clip");
    f.open(clipId);

    juce::Random rng(1);
    f.roll.generateRandomNotesIntoClip(nullptr, 10, 12, rng);
    for (const auto& note : f.doc.getClip(clipId)->notes) {
        EXPECT_GE(note.pitch, 10);
        EXPECT_LE(note.pitch, 12);
    }
}

// ---- Per-clip scale memory ----

TEST(PianoRollScaleAssistTest, PerClipScaleMemoryRestoresOnReopenAndDefaultsToNoScale) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "A");
    const auto clipB = f.doc.addClip(trackId, 8.0, 8.0, "B");

    f.open(clipA);
    EXPECT_FALSE(f.roll.getScaleAssistPanel().getSelectedScale().has_value()) << "never opened before -> No scale";

    // id 2 is the first built-in preset ("Major") — see ScaleAssistPanel::rebuildScaleCombo.
    f.roll.getScaleAssistPanel().getScaleCombo().setSelectedId(2, juce::sendNotificationSync);
    ASSERT_TRUE(f.roll.getScaleAssistPanel().getSelectedScale().has_value());
    EXPECT_EQ(juce::String(f.roll.getScaleAssistPanel().getSelectedScale()->name), "Major");

    f.open(clipB);
    EXPECT_FALSE(f.roll.getScaleAssistPanel().getSelectedScale().has_value())
        << "a clip never opened before starts at No scale, regardless of what another clip has";

    f.open(clipA);
    ASSERT_TRUE(f.roll.getScaleAssistPanel().getSelectedScale().has_value());
    EXPECT_EQ(juce::String(f.roll.getScaleAssistPanel().getSelectedScale()->name), "Major")
        << "clip A's scale choice survived opening a different clip in between";
}

// ---- PropertiesFile persistence: panel visibility + user scales ----

TEST(PianoRollScaleAssistTest, PanelVisibilityAndUserScalesPersistThroughAPropertiesFile) {
    auto props = makeScaleAssistTestProps("PianoRollScaleAssistTest");

    PianoRollFixture f;
    f.roll.setPropertiesFile(props.get());
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible()) << "default is closed";

    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getScaleButtonBounds())));
    ASSERT_TRUE(f.roll.getScaleAssistPanel().isVisible());
    EXPECT_TRUE(props->getBoolValue("pianoRollScalePanelVisible", false));

    auto& panel = f.roll.getScaleAssistPanel();
    panel.getCustomPitchToggle(0).setToggleState(true, juce::dontSendNotification);  // C
    panel.getCustomPitchToggle(4).setToggleState(true, juce::dontSendNotification);  // E
    panel.getCustomPitchToggle(7).setToggleState(true, juce::dontSendNotification);  // G
    panel.getCustomScaleNameEditor().setText("My Triad", false);
    panel.getSaveCustomScaleButton().onClick();

    const auto onDisk = synth::parseUserScales(props->getValue("pianoRollUserScales"));
    ASSERT_EQ(onDisk.size(), 1u);
    EXPECT_EQ(juce::String(onDisk[0].name), "My Triad");
    EXPECT_EQ(onDisk[0].mask, (std::uint16_t)((1u << 0) | (1u << 4) | (1u << 7)));

    // A second roll wired to the SAME PropertiesFile restores both independently of the first.
    PianoRollFixture f2;
    f2.roll.setPropertiesFile(props.get());
    EXPECT_TRUE(f2.roll.getScaleAssistPanel().isVisible()) << "panel-visibility restore";
    const auto restoredScales = f2.roll.getScaleAssistPanel().getUserScalesForTest();
    ASSERT_EQ(restoredScales.size(), 1u);
    EXPECT_EQ(juce::String(restoredScales[0].name), "My Triad");

    props->getFile().deleteFile();
}

TEST(PianoRollScaleAssistTest, SetPropertiesFileIsNullSafe) {
    PianoRollFixture f;
    EXPECT_NO_THROW(f.roll.setPropertiesFile(nullptr));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
}

// ============================================================================
// ScaleAssistPanel — the component in isolation, via its own accessors.
// ============================================================================

TEST(ScaleAssistPanelTest, ScaleComboIsPopulatedNoScaleFirstThenEveryBuiltInPreset) {
    ScaleAssistPanel panel;
    auto& combo = panel.getScaleCombo();
    EXPECT_EQ(combo.getItemText(0), "No scale");

    const auto& presets = synth::builtInScalePresets();
    for (std::size_t i = 0; i < presets.size(); ++i)
        EXPECT_EQ(combo.getItemText((int)(1 + i)), juce::String(presets[i].name));

    EXPECT_EQ(combo.getItemText(combo.getNumItems() - 1), "Edit custom scales...")
        << "the custom-editor affordance is always last";
}

TEST(ScaleAssistPanelTest, RootAndScaleSelectionFireOnScaleChangedWithTheRightScale) {
    ScaleAssistPanel panel;
    std::vector<std::optional<synth::MusicalScale>> fired;
    panel.onScaleChanged = [&](std::optional<synth::MusicalScale> s) { fired.push_back(s); };

    panel.getScaleCombo().setSelectedId(2, juce::sendNotificationSync); // "Major"
    ASSERT_EQ(fired.size(), 1u);
    ASSERT_TRUE(fired.back().has_value());
    EXPECT_EQ(juce::String(fired.back()->name), "Major");
    EXPECT_EQ(fired.back()->rootPitchClass, 0);

    panel.getRootCombo().setSelectedId(3, juce::sendNotificationSync); // D
    ASSERT_EQ(fired.size(), 2u);
    ASSERT_TRUE(fired.back().has_value());
    EXPECT_EQ(fired.back()->rootPitchClass, 2);
    EXPECT_EQ(juce::String(fired.back()->name), "Major") << "changing the root keeps the same scale shape";

    panel.getScaleCombo().setSelectedId(1, juce::sendNotificationSync); // "No scale"
    ASSERT_EQ(fired.size(), 3u);
    EXPECT_FALSE(fired.back().has_value());
}

TEST(ScaleAssistPanelTest, RootChangeWithNoScaleSelectedFiresNoCallback) {
    ScaleAssistPanel panel;
    int fireCount = 0;
    panel.onScaleChanged = [&](std::optional<synth::MusicalScale>) { ++fireCount; };
    panel.getRootCombo().setSelectedId(5, juce::sendNotificationSync);
    EXPECT_EQ(fireCount, 0) << "No scale has no root to recompute";
}

TEST(ScaleAssistPanelTest, PitchVisibilityToggleFiresCallbackWithItsState) {
    ScaleAssistPanel panel;
    std::vector<bool> fired;
    panel.onPitchVisibilityChanged = [&](bool on) { fired.push_back(on); };

    panel.getPitchVisibilityToggle().setToggleState(true, juce::sendNotificationSync);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_TRUE(fired.back());
    EXPECT_TRUE(panel.isPitchVisibilityOn());

    panel.getPitchVisibilityToggle().setToggleState(false, juce::sendNotificationSync);
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_FALSE(fired.back());
}

TEST(ScaleAssistPanelTest, QuantizeAndGenerateButtonsFireTheirCallbacks) {
    ScaleAssistPanel panel;
    int quantiseCount = 0;
    panel.onQuantizePitches = [&] { ++quantiseCount; };
    std::vector<std::pair<int, int>> generated;
    panel.onGenerate = [&](int lo, int hi) { generated.emplace_back(lo, hi); };

    panel.getQuantizeButton().setEnabled(true); // no scale selected yet in this bare-panel test
    panel.getQuantizeButton().onClick();
    EXPECT_EQ(quantiseCount, 1);

    panel.getMinNoteCombo().setSelectedId(37, juce::dontSendNotification); // pitch 36 == C2
    panel.getMaxNoteCombo().setSelectedId(73, juce::dontSendNotification); // pitch 72 == C5
    EXPECT_EQ(panel.getMinPitchSelection(), 36);
    EXPECT_EQ(panel.getMaxPitchSelection(), 72);

    panel.getGenerateButton().onClick();
    ASSERT_EQ(generated.size(), 1u);
    EXPECT_EQ(generated.back().first, 36);
    EXPECT_EQ(generated.back().second, 72);
}

TEST(ScaleAssistPanelTest, MinMaxNoteCombosDefaultToC2AndC5) {
    ScaleAssistPanel panel;
    EXPECT_EQ(panel.getMinPitchSelection(), 36) << "C2";
    EXPECT_EQ(panel.getMaxPitchSelection(), 72) << "C5";
}

TEST(ScaleAssistPanelTest, CustomScaleSaveRoundTripsThroughAPropertiesFileAndRefreshesTheCombo) {
    auto props = makeScaleAssistTestProps("ScaleAssistPanelTest");
    ScaleAssistPanel panel;
    panel.setPropertiesFile(props.get());
    ASSERT_TRUE(panel.getUserScalesForTest().empty());

    // Reveal the editor the same way selecting the last combo row does, fill it in, and save.
    panel.getScaleCombo().setSelectedId(panel.getScaleCombo().getItemId(panel.getScaleCombo().getNumItems() - 1),
                                        juce::sendNotificationSync);
    EXPECT_TRUE(panel.isCustomEditorVisibleForTest());

    panel.getCustomPitchToggle(0).setToggleState(true, juce::dontSendNotification);
    panel.getCustomPitchToggle(3).setToggleState(true, juce::dontSendNotification);
    panel.getCustomPitchToggle(7).setToggleState(true, juce::dontSendNotification);
    panel.getCustomScaleNameEditor().setText("Sparse", false);

    std::vector<std::optional<synth::MusicalScale>> fired;
    panel.onScaleChanged = [&](std::optional<synth::MusicalScale> s) { fired.push_back(s); };
    panel.getSaveCustomScaleButton().onClick();

    EXPECT_FALSE(panel.isCustomEditorVisibleForTest()) << "saving collapses the editor";
    ASSERT_EQ(panel.getUserScalesForTest().size(), 1u);
    EXPECT_EQ(juce::String(panel.getUserScalesForTest()[0].name), "Sparse");
    EXPECT_EQ(panel.getUserScalesForTest()[0].mask, (std::uint16_t)((1u << 0) | (1u << 3) | (1u << 7)));

    ASSERT_EQ(fired.size(), 1u) << "saving selects and applies the new scale";
    ASSERT_TRUE(fired.back().has_value());
    EXPECT_EQ(juce::String(fired.back()->name), "Sparse");

    const auto onDisk = synth::parseUserScales(props->getValue("pianoRollUserScales"));
    ASSERT_EQ(onDisk.size(), 1u);
    EXPECT_EQ(juce::String(onDisk[0].name), "Sparse");

    // The combo now offers the saved scale between the built-ins and the custom-editor row.
    const auto& presets = synth::builtInScalePresets();
    EXPECT_EQ(panel.getScaleCombo().getItemText((int)(1 + presets.size())), "Sparse");

    props->getFile().deleteFile();
}

TEST(ScaleAssistPanelTest, SavingWithNoPropertiesFileIsSessionOnly) {
    ScaleAssistPanel panel; // never wired to a PropertiesFile
    panel.getCustomScaleNameEditor().setText("Ephemeral", false);
    panel.getSaveCustomScaleButton().onClick();

    ASSERT_EQ(panel.getUserScalesForTest().size(), 1u) << "still appended in memory for this session";
    EXPECT_EQ(juce::String(panel.getUserScalesForTest()[0].name), "Ephemeral");
}

TEST(ScaleAssistPanelTest, SaveWithEmptyNameIsANoOp) {
    ScaleAssistPanel panel;
    panel.getCustomScaleNameEditor().setText("", false);
    panel.getSaveCustomScaleButton().onClick();
    EXPECT_TRUE(panel.getUserScalesForTest().empty());
}

TEST(ScaleAssistPanelTest, SetSelectionReflectsStateWithoutFiringCallbacks) {
    ScaleAssistPanel panel;
    int fireCount = 0;
    panel.onScaleChanged = [&](std::optional<synth::MusicalScale>) { ++fireCount; };
    panel.onPitchVisibilityChanged = [&](bool) { ++fireCount; };

    const auto scale = synth::makeScale(2, 1); // D, "Natural Minor"
    panel.setSelection(scale, true);

    EXPECT_EQ(fireCount, 0) << "setSelection is a REFLECTION, not a user edit";
    ASSERT_TRUE(panel.getSelectedScale().has_value());
    EXPECT_EQ(juce::String(panel.getSelectedScale()->name), "Natural Minor");
    EXPECT_EQ(panel.getSelectedScale()->rootPitchClass, 2);
    EXPECT_TRUE(panel.isPitchVisibilityOn());
    EXPECT_EQ(panel.getRootCombo().getSelectedId(), 3); // D
    EXPECT_EQ(panel.getScaleCombo().getSelectedId(), 3); // "Natural Minor" is presets[1] -> id 3
}
