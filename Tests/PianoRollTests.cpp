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
//      once filtered (including at a fractional/half-row scroll position), firstVisiblePitch_'s
//      "always a member of visiblePitches_" invariant, and the CONTINUOUS topRowPosition_ anchor
//      firstVisiblePitch_ is derived from: a regression pin against the old whole-row integer math,
//      and its own clamp at both ends of the pitch range.
//  14. NOTE COLOURING — notePaintFor's resolveNoteColour path (NoteColour.h), asserted against the
//      SAME resolver fed the same fallback Colors, rather than against a re-derived formula.
//  15. KEYS-COLUMN geometry — the black-key inset seam, and a snapshot smoke test with a scale
//      context and AllNotes labels active (on top of the unmodified SnapshotSmoke above).
//  16. BEAT-ANCHORED drag math (a mid-drag view scroll folds into the delta rather than being
//      cancelled out by it), EDGE AUTO-SCROLL (the gated timer's arm/disarm contract and what one
//      tick does on each axis, including the vertical walk staying inside visiblePitches_ under
//      an active scale context and advancing FRACTIONALLY — no zero-progress ticks even at
//      shallow zone penetration), and FOLLOW PLAYHEAD (the page-flip, its zero-extra-repaint
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
#include "../Source/Timeline/TimelineSnapshot.h" // the "Keep" arm's overrun-is-inaudible assertion
#include "../Source/UI/EdgeAutoScroll.h"         // kEdgeZonePx, for a deliberately SHALLOW auto-scroll penetration
#include "../Source/UI/EditTool.h"
#include "../Source/UI/NoteColour.h"
#include "../Source/UI/NoteSelectionModel.h"
#include "../Source/UI/PianoRollComponent.h"
#include "../Source/UI/ScaleAssistPanel.h"
// The audition INTEGRATION tests (section 20b) drive the real panel wiring, not just the roll.
#include "../Source/UI/Theme/BuiltInThemes.h"
#include "../Source/UI/Theme/Theme.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TimelineViewState.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <tuple>

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

// WCAG relative luminance / contrast ratio — same formula NoteColourTests.cpp uses, duplicated
// (not shared) because it's a handful of pure lines private to a different translation unit.
double relativeLuminance(juce::Colour c) {
    auto channel = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
}

double contrastRatio(juce::Colour a, juce::Colour b) {
    const auto l1 = relativeLuminance(a);
    const auto l2 = relativeLuminance(b);
    const auto hi = std::max(l1, l2);
    const auto lo = std::min(l1, l2);
    return (hi + 0.05) / (lo + 0.05);
}

// Plain Euclidean RGB distance — same idiom as NoteColourTests.cpp's rgbDistance.
double rgbDistance(juce::Colour a, juce::Colour b) {
    const double dr = (double)a.getRed() - (double)b.getRed();
    const double dg = (double)a.getGreen() - (double)b.getGreen();
    const double db = (double)a.getBlue() - (double)b.getBlue();
    return std::sqrt(dr * dr + dg * dg + db * db);
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
    int headerButtonRequests = 0;
    juce::Rectangle<int> lastHeaderButtonStrip;

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

    void requestRepaintHeaderButtonStrip(juce::Rectangle<int> strip) override {
        ++headerButtonRequests;
        lastHeaderButtonStrip = strip;
        PianoRollComponent::requestRepaintHeaderButtonStrip(strip);
    }

    // The clip-overrun prompt's seam. Deliberately does NOT call the base implementation: that one
    // opens a real juce::AlertWindow, and a headless run has no message loop to answer it with (nor
    // any business creating a window). It records BOTH halves of the request — the required length
    // and the CLIP ID the prompt was raised for — because the captured id is what routes the answer,
    // and a test that only saw the length could not tell a correctly-routed Extend from one that grew
    // whichever clip happened to be open. Whichever arm a test wants to exercise, it then drives
    // applyExtendPromptAnswer(), which is exactly what the real alert callback calls.
    int extendPrompts = 0;
    double lastExtendPromptRequest = 0.0;
    ClipId lastExtendPromptClipId;
    void promptExtendClipToFitNotes(ClipId clipId, double requiredLengthBeats) override {
        ++extendPrompts;
        lastExtendPromptClipId = clipId;
        lastExtendPromptRequest = requiredLengthBeats;
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

    // Right-edge resize: both notes are still SELECTED (the undo above restored their geometry, not
    // the selection), so the grabbed edge trims the whole group by one shared delta — see the
    // multi-note resize section further down for the full contract.
    const auto rect1b = f.roll.getNoteRect(id1);
    const juce::Point<float> edge((float)rect1b.getRight() - 2.0f, (float)rect1b.getCentreY());
    const juce::Point<float> draggedEdge(edge.x + 40.0f, edge.y); // +1 beat

    f.roll.mouseDown(leftClick(f.roll, edge));
    f.roll.mouseDrag(leftDrag(f.roll, draggedEdge, edge));
    f.roll.mouseUp(leftDrag(f.roll, draggedEdge, edge));

    const auto* resized1 = f.doc.getNote(id1);
    const auto* resized2 = f.doc.getNote(id2);
    ASSERT_NE(resized1, nullptr);
    EXPECT_DOUBLE_EQ(resized1->startBeat, 2.0) << "resize never moves the start";
    EXPECT_DOUBLE_EQ(resized1->lengthBeats, 2.0);
    EXPECT_DOUBLE_EQ(resized2->lengthBeats, 2.0) << "the whole selection took the same +1 beat delta";
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

    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseDrag(leftDrag(f.roll, dragged, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseUp(leftDrag(f.roll, dragged, anchor, juce::ModifierKeys::altModifier));

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
    f.roll.mouseDown(leftClick(f.roll, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseDrag(leftDrag(f.roll, draggedFar, anchor, juce::ModifierKeys::altModifier));
    f.roll.mouseUp(leftDrag(f.roll, draggedFar, anchor, juce::ModifierKeys::altModifier));
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
    // overload). A PLAIN click on "Q" is the quantise verb (Shift+click is the snap toggle).
    f.roll.getSelectionForTest().setSelection({idA});
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));

    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6) << "unselected note is untouched";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1);
    EXPECT_FALSE(f.undo.canUndo());

    // Nothing selected: quantises EVERY note in the clip via doc.quantiseNotes.
    f.roll.getSelectionForTest().clear();
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));

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
    f.roll.mouseDown(leftClick(f.roll, qCentre)); // real change -> one undo step
    ASSERT_TRUE(f.undo.canUndo());
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "the click flashes the button";

    f.roll.mouseDown(leftClick(f.roll, qCentre)); // already quantised -> nothing to record
    EXPECT_TRUE(f.roll.isQuantiseFlashingForTest()) << "a no-op click still gives visual feedback";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1) << "ONE undo returns to the pre-quantise state, "
                                                            "so the second click wrote no step";
    EXPECT_DOUBLE_EQ(f.doc.getNote(idB)->startBeat, 2.6);
    EXPECT_FALSE(f.undo.canUndo());
}

// The SNAP chip (its own magnet-glyph chip now, no longer multiplexed onto the quantise chip by a
// modifier) toggles grid magnetism — the shared snapEnabled switch — and moves no note. Its key is
// the roll's OWN "pianoRollSnapToggle" (J); bare Q is the quantise verb, covered above.
TEST(PianoRollEditingTest, SnapChipTogglesSnapWithoutMovingNotes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    ASSERT_TRUE(idA.isValid());
    ASSERT_TRUE(f.state.snapEnabled);

    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getSnapButtonBounds())));
    EXPECT_FALSE(f.state.snapEnabled) << "a plain click on the Snap chip flips the switch off";
    EXPECT_EQ(toggles, 1);
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.1) << "toggling never moves notes";
    EXPECT_FALSE(f.undo.canUndo()) << "a view-state toggle is not a document edit";
    EXPECT_FALSE(f.roll.isQuantiseFlashingForTest())
        << "the flash belongs to the QUANTISE chip; a snap toggle has its own lit state to show";

    // With the switch off the effective (MAGNETIC) grid is gone — edits go free-hand…
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);
    // …but the DRAWN grid is untouched, which is the whole point of the split (see drawnGridBeats).
    EXPECT_DOUBLE_EQ(f.roll.getDrawnGridDivisionForTest(), 1.0) << "snap governs magnetism, never visibility";

    // …and the J key toggles it right back.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('j')));
    EXPECT_TRUE(f.state.snapEnabled);
    EXPECT_EQ(toggles, 2);
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 1.0);
}

// Bare Q (the key) is the one-shot start-quantise, and it works from the CHOSEN division even while
// the magnetism switch is off — that is the whole point of a one-shot clean-up.
TEST(PianoRollEditingTest, QKeyQuantisesEvenWhileSnapToggledOff) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    const auto idA = f.doc.addNote(clipId, makeNote(1.1, 60));
    ASSERT_TRUE(idA.isValid());

    f.state.snapEnabled = false;
    EXPECT_TRUE(f.roll.isQuantiseEnabled()) << "the one-shot reads the RAW division, not the switch";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('q')));
    EXPECT_DOUBLE_EQ(f.doc.getNote(idA)->startBeat, 1.0);
    EXPECT_FALSE(f.state.snapEnabled) << "Q quantises and never flips the switch";
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

    // Dynamic (see synth::shortcutHintFor) — no ShortcutManager installed, so it falls back to the
    // hardcoded default: bare "q", lower-cased.
    const auto tooltip = f.roll.getTooltipFor(f.roll.getQuantiseButtonBounds().getCentre());
    EXPECT_TRUE(tooltip.startsWith("Quantize note starts to the grid (q)")) << tooltip;
    // Snap is a SEPARATE chip now, with its own word and its own key.
    const auto snapTip = f.roll.getTooltipFor(f.roll.getSnapButtonBounds().getCentre());
    EXPECT_TRUE(snapTip.startsWith("Snap to grid on/off (j)")) << snapTip;
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
juce::KeyPress ctrlPress(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::ctrlModifier, 0); }

// Every action id the roll resolves. A test pins ALL of them to an explicitly invalid KeyPress
// first, then spells out only the one or two it cares about — so nothing here can be rescued (or
// broken) by whatever ShortcutManager::resetToDefaults() happens to know about these ids in any
// given build. That matters because the defaults land in ShortcutManager in a separate phase.
const char* const kRollActionIds[] = {
    "pianoRollNudgeLeft",         "pianoRollNudgeRight",          "pianoRollTransposeUp", "pianoRollTransposeDown",
    "pianoRollTransposeOctaveUp", "pianoRollTransposeOctaveDown", "pianoRollNavPrevNote", "pianoRollNavNextNote",
    "pianoRollQuantise",          "pianoRollQuantisePitches",     "timelineSnapToggle",   "pianoRollToggleScalePanel",
    "pianoRollToggleScaleFilter"};

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
    EXPECT_TRUE(f.roll.keyPressed(plainPress('j'))) << "bare J is the snap toggle";
    EXPECT_FALSE(f.state.snapEnabled);
    EXPECT_TRUE(f.roll.keyPressed(plainPress('q'))) << "and bare Q is quantise";
    EXPECT_FALSE(f.state.snapEnabled) << "quantise never flips the switch";
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

// The snap toggle and the one-shot quantise are two independent PianoRoll-category bindings — the
// roll resolves its OWN "pianoRollSnapToggle" and no longer consults the timeline's, so rebinding
// either one moves only that one.
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
    EXPECT_FALSE(f.roll.keyPressed(plainPress('j'))) << "nor is J";
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

// Ctrl+S (the actual Control key — never Cmd, which is Save Preset) toggles the scale-assist
// panel, with no ShortcutManager installed at all.
TEST(PianoRollShortcutTest, DefaultCtrlSTogglesTheScalePanel) {
    PianoRollFixture f;
    ASSERT_EQ(f.roll.getShortcutManager(), nullptr);
    ASSERT_FALSE(f.roll.getScaleAssistPanel().isVisible());

    EXPECT_TRUE(f.roll.keyPressed(ctrlPress('s')));
    EXPECT_TRUE(f.roll.getScaleAssistPanel().isVisible());

    EXPECT_TRUE(f.roll.keyPressed(ctrlPress('s')));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
}

// Plain S is bound to nothing here (Ctrl is required), so it must fall straight through.
TEST(PianoRollShortcutTest, PlainSDoesNothingToTheScalePanel) {
    PianoRollFixture f;
    ASSERT_FALSE(f.roll.getScaleAssistPanel().isVisible());
    EXPECT_FALSE(f.roll.keyPressed(plainPress('s')));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
}

// The strict no-fallback contract (setShortcutManager's class comment) applies here exactly like
// every other roll action: with a manager installed and this binding explicitly cleared, Ctrl+S
// has NO key at all — it must not quietly resurrect its hardcoded default.
TEST(PianoRollShortcutTest, WithManagerInstalledAndBindingClearedCtrlSDoesNothing) {
    PianoRollFixture f;
    ShortcutManager mgr;
    clearRollBindings(mgr);
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(ctrlPress('s')));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
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

    // Distances are asserted in PIXELS via yForPitch, not via getFirstVisiblePitchForTest: the
    // scroll position is continuous now (topRowPosition_), and the derived legacy int floors it,
    // so a half-row landing quantizes asymmetrically around an integral start by construction —
    // the symmetry claim only holds (and only matters) for the real, continuous mapping.
    const int base = f.roll.getFirstVisiblePitchForTest();
    const int yBase = f.roll.yForPitch(60);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.5f));
    const int yNatural = f.roll.yForPitch(60);
    EXPECT_GT(f.roll.getFirstVisiblePitchForTest(), base) << "a +deltaY gesture scrolls towards HIGHER pitches";
    EXPECT_GT(yNatural, yBase) << "so a fixed pitch's row moves DOWN the screen";

    // Undo it, then run the identical gesture inverted: it must land the same distance the other way.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(-0.5f));
    ASSERT_EQ(f.roll.yForPitch(60), yBase);

    f.roll.setScrollInverted(true);
    EXPECT_TRUE(f.roll.isScrollInverted());
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.5f));
    const int yInverted = f.roll.yForPitch(60);
    EXPECT_LT(yInverted, yBase) << "the same gesture now scrolls the other way";
    EXPECT_EQ(yBase - yInverted, yNatural - yBase) << "and by exactly the same number of pixels";
}

// THE regression: a trackpad's small deltaY (often 0.01-0.1 per event) scaled by
// kPitchScrollSemitonesPerWheelUnit rounds to ZERO whole rows on almost every individual event; a
// plain per-event round dropped the gesture entirely unless one event happened to be big enough to
// clear a row on its own — "only sometimes works". topRowPosition_ (a continuous double — see the
// class comment) simply never rounds at all, so N small events land on exactly the same
// (fractional) position ONE big event of the summed delta would, and hence on the same DERIVED
// firstVisiblePitch_ row.
TEST(PianoRollWheelTest, SmallPitchScrollEventsAccumulateToTheSameRowCountAsOneBigEvent) {
    PianoRollFixture accumulated;
    const auto trackA = accumulated.doc.addTrack(TrackKind::Midi, "Track 1");
    accumulated.open(accumulated.doc.addClip(trackA, 0.0, 8.0, "Clip"));
    const int base = accumulated.roll.getFirstVisiblePitchForTest();

    // Five small events, each individually below what a single-event round would register as a
    // whole row (0.1 * kPitchScrollSemitonesPerWheelUnit == 0.3 rows -- truncates to 0 alone).
    for (int i = 0; i < 5; ++i)
        accumulated.roll.mouseWheelMove(leftClick(accumulated.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
    const int afterSmallEvents = accumulated.roll.getFirstVisiblePitchForTest();
    EXPECT_GT(afterSmallEvents, base) << "the gesture must not be dropped just because no single event cleared a row";

    PianoRollFixture oneBig;
    const auto trackB = oneBig.doc.addTrack(TrackKind::Midi, "Track 1");
    oneBig.open(oneBig.doc.addClip(trackB, 0.0, 8.0, "Clip"));
    ASSERT_EQ(oneBig.roll.getFirstVisiblePitchForTest(), base) << "test premise: identical starting state";
    oneBig.roll.mouseWheelMove(leftClick(oneBig.roll, {300.0f, 90.0f}),
                               wheelOnY(0.5f)); // the SAME total delta (5 * 0.1)
    EXPECT_EQ(afterSmallEvents, oneBig.roll.getFirstVisiblePitchForTest())
        << "five small events summing to 0.5 must land on exactly the same row as one 0.5 event";
}

// THE actual smoothness fix (as opposed to the "lost small deltas" fix above): before this, the
// wheel could DROP a small gesture, but even the fixed ("carry the remainder") version still left
// firstVisiblePitch_ — the only state yForPitch read — completely UNMOVED while a fraction
// accumulated, so the grid still visibly snapped in whole ~10px rows. Now topRowPosition_ itself is
// what moves, so yForPitch of a FIXED pitch takes a proportional, sub-row PIXEL step on every single
// event, matching the horizontal axis (rollView_.firstVisibleBeat).
TEST(PianoRollWheelTest, SmallPitchScrollEventsMoveYSmoothlyBelowARow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int fixedPitch = f.roll.getFirstVisiblePitchForTest() - 5; // comfortably inside the grid
    int previousY = f.roll.yForPitch(fixedPitch);
    // 0.05 * kPitchScrollSemitonesPerWheelUnit (3.0) == 0.15 rows/event -- well under one row, but
    // (at the default 10px/row) still more than a pixel, so llround can never tie two consecutive
    // events to the same y (see the .cpp derivation this test pins).
    for (int i = 0; i < 5; ++i) {
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.05f));
        const int y = f.roll.yForPitch(fixedPitch);
        EXPECT_GT(y, previousY) << "event " << i
                                << ": the grid must move a little on EVERY event, never sit "
                                   "frozen until a whole row accumulates";
        previousY = y;
    }
}

TEST(PianoRollWheelTest, PitchScrollAccumulatorCarriesTheFractionalRemainderAcrossEvents) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const int base = f.roll.getFirstVisiblePitchForTest();

    // 0.1 * kPitchScrollSemitonesPerWheelUnit (3.0) == 0.3 rows/event -- three events sum to 0.9
    // (still under a whole row), a FOURTH tips it over 1.0. firstVisiblePitch_ itself still only
    // ever reports a WHOLE row (it is derived from floor(topRowPosition_) — see the class comment),
    // so this whole-row-crossing behaviour is unchanged even though there is no longer a separate
    // pitchScrollRemainder_ member: topRowPosition_'s own fractional part now plays that role.
    for (int i = 0; i < 3; ++i) {
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
        EXPECT_EQ(f.roll.getFirstVisiblePitchForTest(), base) << "event " << i << ": still under one row's worth";
    }
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
    EXPECT_GT(f.roll.getFirstVisiblePitchForTest(), base)
        << "the 4th event's carried fraction finally clears a whole row";
}

TEST(PianoRollWheelTest, PitchScrollFractionResetsAcrossAClipSwitch) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "A");
    const auto clipB = f.doc.addClip(trackId, 8.0, 8.0, "B");
    f.open(clipA);

    // Leave clip A's topRowPosition_ at a 0.9-row fraction -- one event short of clearing a row on
    // its own (see the previous test). If this carried into clip B unreset, a SINGLE further 0.1
    // event there (0.3 more) would tip it over 1.0 and move a row; openClip resets it to a whole
    // row (see its own comment), so it stays well under.
    for (int i = 0; i < 3; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));

    f.open(clipB);
    const int baseB = f.roll.getFirstVisiblePitchForTest();
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
    EXPECT_EQ(f.roll.getFirstVisiblePitchForTest(), baseB)
        << "clip A's pending 0.9-row fraction must not surface in clip B";
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
    // topRowPosition_ (the continuous anchor zoomVerticalAroundY actually moves) is never rounded
    // to a whole row mid-zoom any more, so the centre pitch now stays EXACTLY put rather than
    // merely "within one row".
    EXPECT_EQ(f.roll.pitchForY(centreY), centrePitch) << "the centre pitch stays put, exactly";

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

// The case the OLD int-only firstVisiblePitch_ could never even represent: the anchor is
// FRACTIONAL (mid-row) BEFORE the zoom starts, via the Cmd+Shift+wheel branch — the same public
// path a real gesture takes, not a direct call into the private zoomVerticalAroundY.
TEST(PianoRollZoomApiTest, CmdShiftWheelZoomKeepsTheAnchoredYFixedEvenFromAFractionalStart) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    f.roll.setTopRowPositionForTest(std::floor(f.roll.getTopRowPositionForTest()) + 0.5);
    ASSERT_DOUBLE_EQ(f.roll.getTopRowPositionForTest(), std::floor(f.roll.getTopRowPositionForTest()) + 0.5);

    const int anchorY = PianoRollComponent::kHeaderHeight + 37; // an arbitrary point inside the grid
    const int pitchBefore = f.roll.pitchForY(anchorY);

    const int mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.4f;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, (float)anchorY}, mods), wheel);

    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);
    EXPECT_EQ(f.roll.pitchForY(anchorY), pitchBefore)
        << "the row under the cursor stays exactly put, even though the pre-zoom anchor was fractional";
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

// Regression coverage for the "sharp key labels are nearly invisible" bug: labelColourFor
// contrasts against THAT row's own key fill (never a single fill shared by both key colours), so
// a black-key label reads near-white and a white-key label reads near-black in every built-in
// theme, with a healthy margin between the two — the perceived-brightness idiom NoteColourTests.cpp
// uses for its own theme guard.
TEST(PianoRollKeyLabelTest, LabelColourContrastsAgainstItsOwnKeyFillInEveryTheme) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        const auto whiteLabel = PianoRollComponent::labelColourFor(c.pianoKeyWhite);
        const auto blackLabel = PianoRollComponent::labelColourFor(c.pianoKeyBlack);

        EXPECT_GE(contrastRatio(whiteLabel, c.pianoKeyWhite), 3.5) << "Theme '" << t.name << "': white-key label";
        EXPECT_GE(contrastRatio(blackLabel, c.pianoKeyBlack), 3.5) << "Theme '" << t.name << "': black-key label";
        // The actual bug: a label colour resolved from ONE shared fill would put the SAME colour
        // on both key families. The two must land on opposite ends of the brightness scale.
        EXPECT_GT(rgbDistance(whiteLabel, blackLabel), 80.0)
            << "Theme '" << t.name << "': white-key and black-key labels must not be the same colour";
    }
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

// ---- topRowPosition_ — the continuous vertical scroll anchor firstVisiblePitch_ is derived from
// (see the class comment's "Vertical row mapping" section) ----

// THE regression pin: openClip lands on a whole row, and with topRowPosition_ integral,
// yForPitch's new formula must produce PIXEL-FOR-PIXEL the same result the old
// "kHeaderHeight + llround((firstRow - pitchRow) * ps)" int-only math did — reproduced verbatim
// here as the ground truth, rather than re-deriving it from yForPitch itself.
TEST(PianoRollRowMappingTest, YForPitchPinsIdenticalToTheOldIntegerMathWhenTopRowPositionIsIntegral) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getTopRowPositionForTest(), std::floor(f.roll.getTopRowPositionForTest()))
        << "test premise: openClip lands on a whole row";

    const int firstRow = f.roll.getFirstVisiblePitchForTest(); // row index == pitch value, unfiltered
    const double ps = f.roll.getPixelsPerSemitone();
    for (int pitch = 0; pitch <= 127; ++pitch) {
        const int expectedY = PianoRollComponent::kHeaderHeight + (int)std::llround((double)(firstRow - pitch) * ps);
        EXPECT_EQ(f.roll.yForPitch(pitch), expectedY) << "pitch " << pitch;
    }
}

// The round-trip test above (RoundTripThroughVisibleRowsHoldsWithFilteringActive) only ever runs
// against a WHOLE-row topRowPosition_. Plant a HALF-row one instead (10 px/semitone makes 0.5 row
// == an exact 5px, so there is no rounding ambiguity to make the round-trip flaky) and require the
// same invariant to keep holding for every visible pitch.
TEST(PianoRollRowMappingTest, RoundTripHoldsAtAHalfRowScrollOffset) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    f.roll.setTopRowPositionForTest(std::floor(f.roll.getTopRowPositionForTest()) + 0.5);
    ASSERT_DOUBLE_EQ(f.roll.getTopRowPositionForTest() - std::floor(f.roll.getTopRowPositionForTest()), 0.5);

    for (const int pitch : f.roll.getVisiblePitchesForTest()) {
        const int y = f.roll.yForPitch(pitch);
        EXPECT_EQ(f.roll.pitchForY(y), pitch) << "pitch " << pitch;
    }
}

// setTopRowPositionForTest goes through the same seam (setTopRowPosition) every real writer does,
// so planting a wildly out-of-range value exercises exactly the clamp a wheel/zoom/auto-scroll that
// keeps pushing past an end relies on.
TEST(PianoRollRowMappingTest, TopRowPositionClampsAtBothExtremes) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    // The fixture's grid (140 px of row space at the default 10px/row -> 14 rows) is far shorter
    // than the full 128-pitch range, so the lower bound is strictly tighter than the old [0, 127]
    // int clamp — this is the "never scroll past the first/last visible row" fix.
    ASSERT_GT(f.roll.getMinTopRowPositionForTest(), 0.0) << "test premise: the grid is shorter than the pitch range";

    f.roll.setTopRowPositionForTest(1.0e6);
    EXPECT_DOUBLE_EQ(f.roll.getTopRowPositionForTest(), f.roll.getMaxTopRowPositionForTest());
    EXPECT_EQ(f.roll.getFirstVisiblePitchForTest(), f.roll.getVisiblePitchesForTest().back())
        << "pinned to the HIGHEST visible pitch at the top — nothing higher to scroll to";

    f.roll.setTopRowPositionForTest(-1.0e6);
    EXPECT_DOUBLE_EQ(f.roll.getTopRowPositionForTest(), f.roll.getMinTopRowPositionForTest());
}

// Row-collapse interaction: under an active scale context, small wheel events must still walk
// topRowPosition_ smoothly (a fixed VISIBLE pitch's y moves on every event, per
// SmallPitchScrollEventsMoveYSmoothlyBelowARow above) AND every row the scroll ever lands ON must
// itself be a genuinely visible (never a scale-collapsed) pitch.
TEST(PianoRollRowMappingTest, PitchScrollUnderAnActiveScaleContextMovesSmoothlyAndNeverSurfacesAHiddenPitch) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.setScaleContext(cMajorContains, /*pitchVisibilityOn*/ true);

    const auto& visible = f.roll.getVisiblePitchesForTest();
    ASSERT_FALSE(visible.empty());
    const int fixedPitch = visible.front(); // always below whatever row the scroll below can reach

    int previousY = f.roll.yForPitch(fixedPitch);
    for (int i = 0; i < 5; ++i) {
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.05f));
        const int y = f.roll.yForPitch(fixedPitch);
        EXPECT_GT(y, previousY) << "event " << i << ": must move smoothly under filtering too";
        previousY = y;

        EXPECT_TRUE(std::binary_search(visible.begin(), visible.end(), f.roll.getFirstVisiblePitchForTest()))
            << "event " << i << ": must never step onto a row the scale filter collapsed";
    }
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
    const auto asIfInScale =
        synth::ui::resolveNoteColour(synth::theme::Colors{}, note->pitch, note->velocity, false, false,
                                     /*outOfScale*/ false, synth::ui::NoteColourOverrides{});
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
    // Dynamic (see synth::shortcutHintFor) — no ShortcutManager installed, so it falls back to the
    // hardcoded default: Ctrl + S (a chorded key is never lower-cased).
    const auto tooltip = f.roll.getTooltipFor(centreOf(f.roll.getScaleButtonBounds()).toInt());
    EXPECT_TRUE(tooltip.startsWith("Scale assist"));
    EXPECT_TRUE(tooltip.contains("Ctrl + S"));
}

TEST(PianoRollScaleAssistTest, QuantiseAndScaleTooltipsTrackALiveRebindAndClearingRestoresTheDefault) {
    PianoRollFixture f;
    ShortcutManager mgr;
    f.roll.setShortcutManager(&mgr);

    // Rebuilt LIVE on every getTooltipFor() call (no cache, no listener needed for this roll — see
    // its class comment) -- setBinding alone is enough, no saveToProperties() required.
    mgr.setBinding("pianoRollQuantise", juce::KeyPress('g', juce::ModifierKeys::noModifiers, 0));
    auto tooltip = f.roll.getTooltipFor(f.roll.getQuantiseButtonBounds().getCentre());
    EXPECT_TRUE(tooltip.contains("(g)"));
    EXPECT_FALSE(tooltip.contains("(q)"));

    // The Snap chip reads the SHARED snap action, so rebinding quantise above left it alone.
    mgr.setBinding("timelineSnapToggle", juce::KeyPress('k', juce::ModifierKeys::noModifiers, 0));
    EXPECT_TRUE(f.roll.getTooltipFor(f.roll.getSnapButtonBounds().getCentre()).contains("(k)"));

    mgr.setBinding("pianoRollToggleScalePanel", juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0));
    tooltip = f.roll.getTooltipFor(centreOf(f.roll.getScaleButtonBounds()).toInt());
    EXPECT_TRUE(tooltip.contains("M")) << "the current binding's key";
    EXPECT_FALSE(tooltip.contains("Ctrl + S")) << "not the hardcoded default anymore";

    f.roll.setShortcutManager(nullptr);
    tooltip = f.roll.getTooltipFor(f.roll.getQuantiseButtonBounds().getCentre());
    EXPECT_TRUE(tooltip.contains("(q)")) << "detached -- back to the hardcoded default";
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
    panel.getCustomPitchToggle(0).setToggleState(true, juce::dontSendNotification); // C
    panel.getCustomPitchToggle(4).setToggleState(true, juce::dontSendNotification); // E
    panel.getCustomPitchToggle(7).setToggleState(true, juce::dontSendNotification); // G
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
// 18. Scale-panel SLIDE animation (in/out — see setScalePanelVisible / AnimationDriver).
//
// The roll here is never added to a real window (isShowing() is false), so every real toggle
// snaps immediately — the pre-existing gutter tests above rely on exactly that and are left
// unmodified. The tween's own MATH is exercised directly through the progress test seam
// (setScalePanelOpenProgressForTest), the same "pump the animator's own per-frame update" idiom
// ModuleLibraryCollapseAnimationTests.cpp uses for the library sidebar's fold.
// ============================================================================

TEST(PianoRollScaleAnimationTest, ProgressSweepMovesLeftGutterWidthMonotonicallyAndFiresOnHorizontalViewChanged) {
    PianoRollFixture f;
    int viewChanges = 0;
    f.roll.onHorizontalViewChanged = [&] { ++viewChanges; };

    ASSERT_EQ(f.roll.leftGutterWidth(), PianoRollComponent::kKeysColumnWidth);
    int previousWidth = f.roll.leftGutterWidth();
    for (float p : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        f.roll.setScalePanelOpenProgressForTest(p);
        const int width = f.roll.leftGutterWidth();
        EXPECT_GE(width, previousWidth) << "progress " << p;
        previousWidth = width;
    }
    EXPECT_EQ(f.roll.leftGutterWidth(), PianoRollComponent::kKeysColumnWidth + PianoRollComponent::kScalePanelWidth);
    EXPECT_EQ(viewChanges, 5) << "the mapping genuinely moves every frame -- the ruler override has to track it";
}

TEST(PianoRollScaleAnimationTest, EndpointsMatchTheOldBinaryGutterExactly) {
    PianoRollFixture f;
    f.roll.setScalePanelOpenProgressForTest(0.0f);
    EXPECT_EQ(f.roll.leftGutterWidth(), PianoRollComponent::kKeysColumnWidth);
    f.roll.setScalePanelOpenProgressForTest(1.0f);
    EXPECT_EQ(f.roll.leftGutterWidth(), PianoRollComponent::kKeysColumnWidth + PianoRollComponent::kScalePanelWidth);
}

TEST(PianoRollScaleAnimationTest, ToggleWhileNotShowingSnapsInsteadOfAnimating) {
    PianoRollFixture f;
    ASSERT_FALSE(f.roll.isShowing()) << "test premise: headless, no real window";

    f.roll.toggleScalePanel();
    EXPECT_FALSE(f.roll.isScalePanelAnimatingForTest()) << "no VBlank reaches an off-screen component";
    EXPECT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 1.0f);
    EXPECT_TRUE(f.roll.isScalePanelTargetVisibleForTest());
    EXPECT_EQ(f.roll.leftGutterWidth(), PianoRollComponent::kKeysColumnWidth + PianoRollComponent::kScalePanelWidth);

    f.roll.toggleScalePanel();
    EXPECT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 0.0f);
    EXPECT_FALSE(f.roll.isScalePanelTargetVisibleForTest());
    EXPECT_EQ(f.roll.leftGutterWidth(), PianoRollComponent::kKeysColumnWidth);
}

TEST(PianoRollScaleAnimationTest, TogglingMidFlightReversesFromTheCurrentWidthNotFromAnExtreme) {
    PianoRollFixture f;
    f.roll.toggleScalePanel(); // opens; headless -> snaps to progress 1.0 immediately
    ASSERT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 1.0f);

    // Simulate catching the slide mid-flight (as a real VBlank frame would have left it, had one
    // run) before it settled.
    f.roll.setScalePanelOpenProgressForTest(0.6f);
    f.roll.toggleScalePanel(); // request CLOSE now
    EXPECT_FLOAT_EQ(f.roll.getScalePanelAnimFromForTest(), 0.6f)
        << "the tween's captured start point is the CURRENT width, never an extreme -- no jump";
    // Still headless, so it lands on the end state immediately either way.
    EXPECT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 0.0f);
}

TEST(PianoRollScaleAnimationTest, OpenClipAndCloseRollNeverAnimateAcrossTheSwitch) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");

    f.roll.toggleScalePanel(); // open, snaps immediately (headless)
    ASSERT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 1.0f);

    f.roll.openClip(clipId);
    EXPECT_FALSE(f.roll.isScalePanelAnimatingForTest());
    EXPECT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 1.0f)
        << "the panel's own open/closed state is unrelated to which clip is open";

    f.roll.closeRoll();
    EXPECT_FALSE(f.roll.isScalePanelAnimatingForTest());
    EXPECT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 1.0f);
}

// A PropertiesFile restore must never itself look like the panel sliding open on launch.
TEST(PianoRollScaleAnimationTest, PropertiesFileRestoreSnapsEvenThoughAnimateDefaultsToTrue) {
    auto props = makeScaleAssistTestProps("PianoRollScaleAnimationTest");
    props->setValue("pianoRollScalePanelVisible", true);

    PianoRollFixture f;
    f.roll.setPropertiesFile(props.get());
    EXPECT_FALSE(f.roll.isScalePanelAnimatingForTest());
    EXPECT_FLOAT_EQ(f.roll.getScalePanelOpenProgressForTest(), 1.0f);
    EXPECT_TRUE(f.roll.getScaleAssistPanel().isVisible());

    props->getFile().deleteFile();
}

// ============================================================================
// 19. Header button chip affordance (six chips — hover wash + resting/active fill).
// ============================================================================

TEST(PianoRollHeaderButtonTest, HoverEntersAndLeavesGateRepaintsConfinedToTheChipRect) {
    PianoRollFixture f;
    ASSERT_EQ(f.roll.getHoveredHeaderButtonForTest(), PianoRollComponent::HeaderButtonId::None);
    ASSERT_EQ(f.roll.headerButtonRequests, 0);

    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Quantise));
    EXPECT_EQ(f.roll.headerButtonRequests, 1);
    EXPECT_EQ(f.roll.lastHeaderButtonStrip, f.roll.getQuantiseButtonBounds());

    // Hovering the SAME chip again costs nothing more (the repaint invariant's state-change gate).
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantiseButtonBounds()) + juce::Point<float>(1.0f, 0.0f)));
    EXPECT_EQ(f.roll.headerButtonRequests, 1);

    // Moving to the Scale chip repaints BOTH the vacated quantise rect and the newly hovered one.
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getScaleButtonBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Scale));
    EXPECT_EQ(f.roll.headerButtonRequests, 3);

    // Leaving the header entirely clears the hover and costs exactly one more repaint.
    f.roll.mouseExit(hover(f.roll, {5.0f, 5.0f}));
    EXPECT_EQ(f.roll.getHoveredHeaderButtonForTest(), PianoRollComponent::HeaderButtonId::None);
    EXPECT_EQ(f.roll.headerButtonRequests, 4);
}

// Resting chips must be distinguishable from the header strip's own background (surfaceHi vs
// surface), and the active/toggled fill (toolActive) must be distinguishable from resting, in
// every built-in theme — token-level guards for the "buttons read as bare text" bug (they used to
// share NO fill at all with the strip, painting only a hairline outline).
TEST(PianoRollHeaderButtonTest, RestingChipFillIsDistinguishableFromTheHeaderBackgroundInEveryTheme) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        EXPECT_GT(rgbDistance(c.surfaceHi, c.surface), 10.0) << "Theme '" << t.name << "'";
    }
}

TEST(PianoRollHeaderButtonTest, ActiveChipFillIsDistinguishableFromRestingFillInEveryTheme) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        EXPECT_GT(rgbDistance(c.toolActive, c.surfaceHi), 40.0) << "Theme '" << t.name << "'";
    }
}

// ============================================================================
// 20. NOTE AUDITION (onAuditionNote) — "clicking a note plays it".
// ============================================================================
//
// The roll emits a pitch + normalised velocity + an on/off edge and knows nothing about the graph,
// so every one of these tests is a pure callback count. The contract under test is the one a stuck
// note would violate: EXACTLY ONE `false` follows every `true`, on every exit path.

namespace {

// One recorded onAuditionNote call.
struct AuditionEvent {
    int pitch = 0;
    float velocity01 = 0.0f;
    bool on = false;
};

// Wires the roll's callback into `out` and returns nothing — the fixture keeps owning the roll.
void recordAuditionInto(PianoRollFixture& f, std::vector<AuditionEvent>& out) {
    f.roll.onAuditionNote = [&out](int pitch, float velocity01, bool on) { out.push_back({pitch, velocity01, on}); };
}

} // namespace

TEST(PianoRollAuditionTest, MouseDownOnANoteSoundsItAndMouseUpReleasesIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    auto n = makeNote(1.0, 64, 1.0);
    n.velocity = 96;
    const auto id = f.doc.addNote(clipId, n);
    ASSERT_TRUE(id.isValid());

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    f.roll.mouseDown(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].pitch, 64);
    EXPECT_TRUE(events[0].on);
    EXPECT_NEAR(events[0].velocity01, 96.0f / 127.0f, 1.0e-4f) << "the note's OWN velocity, not a fixed preview level";
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), 64);

    f.roll.mouseUp(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].pitch, 64);
    EXPECT_FALSE(events[1].on);
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), -1);
}

TEST(PianoRollAuditionTest, ClickingEmptyGridSoundsNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 64, 1.0)).isValid());

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    // Empty grid well clear of the one note, and the header strip.
    const juce::Point<float> empty((float)f.roll.getNoteGridBounds().getCentreX(),
                                   (float)f.roll.getNoteGridBounds().getBottom() - 4.0f);
    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseUp(leftClick(f.roll, empty));
    EXPECT_TRUE(events.empty());
}

// A Move drag across pitches re-articulates: off the old pitch, on the new one. Dragging sideways
// inside the SAME row costs nothing — the same state-change gate the repaint invariant demands.
TEST(PianoRollAuditionTest, MoveDragRetriggersOnEachNewPitchAndNotWithinARow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    f.roll.mouseDown(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].pitch, 60);

    // Sideways only (+1 beat, same row): no new edge at all.
    const juce::Point<float> sideways(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDrag(leftDrag(f.roll, sideways, anchor));
    EXPECT_EQ(events.size(), 1u) << "a drag inside one row must not retrigger";
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), 60);

    // Up two rows (kPixelsPerSemitone == 10): one off + one on, at the NEW pitch.
    const juce::Point<float> up(sideways.x, sideways.y - 20.0f);
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor));
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[1].pitch, 60);
    EXPECT_FALSE(events[1].on) << "the old pitch is released first";
    EXPECT_EQ(events[2].pitch, 62);
    EXPECT_TRUE(events[2].on);
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), 62);

    f.roll.mouseUp(leftDrag(f.roll, up, anchor));
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[3].pitch, 62);
    EXPECT_FALSE(events[3].on) << "the release matches whatever pitch was sounding, not the original";

    // The whole gesture balances: every note-on got exactly one note-off.
    int held = 0;
    for (const auto& e : events)
        held += e.on ? 1 : -1;
    EXPECT_EQ(held, 0);
}

// The no-stuck-note paths, one per cancel route. None of them is a mouseUp.
TEST(PianoRollAuditionTest, EveryCancelPathReleasesTheHeldNote) {
    const auto runCancelCase = [](const std::function<void(PianoRollFixture&)>& cancel) {
        PianoRollFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
        f.open(clipId);
        const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
        std::vector<AuditionEvent> events;
        recordAuditionInto(f, events);

        f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getNoteRect(id))));
        ASSERT_EQ(events.size(), 1u);
        ASSERT_TRUE(f.roll.isAuditionActiveForTest());

        cancel(f);
        ASSERT_EQ(events.size(), 2u);
        EXPECT_FALSE(events[1].on);
        EXPECT_EQ(events[1].pitch, 60);
        EXPECT_FALSE(f.roll.isAuditionActiveForTest());
    };

    // A tool switch abandons the in-flight gesture — and its preview with it.
    runCancelCase([](PianoRollFixture& f) { f.roll.setActiveTool(synth::ui::EditTool::Erase); });
    // Closing the roll: no mouse-up is ever coming.
    runCancelCase([](PianoRollFixture& f) { f.roll.closeRoll(); });
    // Opening a DIFFERENT clip is the same discontinuity.
    runCancelCase([](PianoRollFixture& f) {
        const auto other = f.doc.addClip(f.doc.getTracks().front().id, 8.0, 8.0, "Other");
        f.roll.openClip(other);
    });
    // Being hidden (the panel swapping the clip lanes back in, the window closing).
    runCancelCase([](PianoRollFixture& f) {
        f.roll.setVisible(true);
        f.roll.setVisible(false);
    });
}

// The destructor is the last line of defence: the owner's synth has no way to learn the component
// went away, so the note-off has to come from here.
TEST(PianoRollAuditionTest, DestructorReleasesAHeldNote) {
    TimelineDoc doc;
    TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.snap = TimelineViewState::Snap::Quarter;
    AppUndoManager undo;

    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");

    std::vector<AuditionEvent> events;
    {
        PianoRollComponent roll{state};
        roll.setTimelineDoc(&doc);
        roll.setUndoManager(&undo);
        roll.setSize(900, 160);
        roll.openClip(clipId);
        roll.setHorizontalView(40.0, 0.0);
        const auto id = doc.addNote(clipId, makeNote(1.0, 67, 1.0));
        roll.onAuditionNote = [&events](int pitch, float v, bool on) { events.push_back({pitch, v, on}); };
        roll.mouseDown(leftClick(roll, centreOf(roll.getNoteRect(id))));
        ASSERT_EQ(events.size(), 1u);
    }
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].pitch, 67);
    EXPECT_FALSE(events[1].on);
}

// Audition belongs to the SELECT tool. An Erase/Mute/Split/Glue click is not a request to hear the
// note it is about to change.
TEST(PianoRollAuditionTest, NonSelectToolClicksDoNotAudition) {
    for (auto tool : {synth::ui::EditTool::Erase, synth::ui::EditTool::Mute, synth::ui::EditTool::Split,
                      synth::ui::EditTool::Glue, synth::ui::EditTool::Draw}) {
        PianoRollFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
        f.open(clipId);
        const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 2.0));
        ASSERT_TRUE(id.isValid());

        std::vector<AuditionEvent> events;
        recordAuditionInto(f, events);
        f.roll.setActiveTool(tool);

        const auto anchor = centreOf(f.roll.getNoteRect(id));
        f.roll.mouseDown(leftClick(f.roll, anchor));
        f.roll.mouseUp(leftClick(f.roll, anchor));
        EXPECT_TRUE(events.empty()) << "tool " << (int)tool;
    }
}

// ---------------------------------------------------------------------------
// 20b. Audition INTEGRATION — through the real TimelinePanelComponent wiring.
// ---------------------------------------------------------------------------
//
// Everything above wires roll.onAuditionNote straight to a recorder, which tests the ROLL's half of
// the contract and nothing else. The stuck note this section exists for lived in the OTHER half: the
// panel's lambda resolves which track to send to FROM THE OPEN CLIP, so any teardown that clears the
// open clip before the note-off is emitted (or deletes the clip outright) used to drop the off — and
// an audition note is deliberately exempt from every positional flush in TimelineMidiSourceModule, so
// nothing downstream would ever release it. These tests drive the whole chain: roll -> panel ->
// TrackHeaderHost.

namespace {

// Records auditionTrackNote and nothing else; every other TrackHeaderHost member is an inert stub
// (this file tests the audition path, not the track-header column).
class AuditionRecordingHost : public synth::ui::TrackHeaderHost {
public:
    struct Call {
        synth::TrackId track;
        int pitch = 0;
        int velocity = 0;
        bool on = false;
    };

    void auditionTrackNote(synth::TrackId track, int pitch, int velocity, bool noteOn) override {
        calls.push_back({track, pitch, velocity, noteOn});
    }

    std::vector<Call> onCalls() const {
        std::vector<Call> out;
        for (const auto& c : calls)
            if (c.on)
                out.push_back(c);
        return out;
    }
    std::vector<Call> offCalls() const {
        std::vector<Call> out;
        for (const auto& c : calls)
            if (!c.on)
                out.push_back(c);
        return out;
    }

    std::vector<Call> calls;

    // ---- inert stubs ----
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

// A real TimelinePanelComponent with the roll open on one clip holding one note, plus the recording
// host — i.e. exactly the production wiring, minus the graph.
struct AuditionIntegrationFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    AuditionRecordingHost host;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId trackId;
    ClipId clipId;
    NoteId noteId;

    AuditionIntegrationFixture() {
        panel.setSize(1200, 320);
        auto& state = panel.getViewState();
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;
        state.snapEnabled = true;
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTrackHeaderHost(&host);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");
        noteId = doc.addNote(clipId, makeNote(1.0, 64, 1.0));
        panel.openPianoRoll(clipId);
    }

    synth::ui::PianoRollComponent& roll() { return panel.getPianoRoll(); }

    // Presses (and holds) the note — the on edge travels the real chain.
    void pressAndHoldNote() {
        auto& r = roll();
        const auto rect = r.getNoteRect(noteId);
        r.mouseDown(leftClick(r, centreOf(rect)));
    }
};

} // namespace

TEST(PianoRollAuditionIntegrationTest, PressAndReleaseDeliverOneOnAndOneOffToTheBoundTrack) {
    AuditionIntegrationFixture f;
    ASSERT_TRUE(f.roll().isOpen());
    ASSERT_FALSE(f.roll().getNoteRect(f.noteId).isEmpty()) << "the roll must be laid out for the press to land";

    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);
    EXPECT_EQ(f.host.onCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.onCalls()[0].pitch, 64);
    EXPECT_TRUE(f.host.offCalls().empty());

    auto& r = f.roll();
    r.mouseUp(leftClick(r, centreOf(r.getNoteRect(f.noteId))));
    ASSERT_EQ(f.host.offCalls().size(), 1u);
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.offCalls()[0].pitch, 64);
}

// THE regression: closing the roll mid-hold. closeRoll() clears clipId_, so a note-off emitted after
// that point has no clip to resolve a track from — the off must already have been sent (stopAudition
// runs FIRST) and, either way, the panel routes it to the LATCHED track rather than re-resolving.
TEST(PianoRollAuditionIntegrationTest, ClosingTheRollMidHoldStillDeliversExactlyOneOff) {
    AuditionIntegrationFixture f;
    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);
    ASSERT_TRUE(f.host.offCalls().empty());

    f.panel.closePianoRoll();
    ASSERT_FALSE(f.roll().isOpen());

    ASSERT_EQ(f.host.offCalls().size(), 1u) << "a preview cut short by the roll closing must still be released";
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId) << "routed to the track the ON went to";
    EXPECT_EQ(f.host.offCalls()[0].pitch, 64);
    EXPECT_EQ(f.host.calls.size(), 2u) << "exactly one on and one off, no duplicates";
}

// The same hazard reached the other way: the edited clip is DELETED while the note is held, so
// refreshFromDoc() closes the roll from under the gesture and there is no clip left to resolve at
// all. The latch is what makes this deliverable.
TEST(PianoRollAuditionIntegrationTest, DeletingTheEditedClipMidHoldStillDeliversExactlyOneOff) {
    AuditionIntegrationFixture f;
    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);

    ASSERT_TRUE(f.doc.removeClip(f.clipId));
    EXPECT_FALSE(f.roll().isOpen()) << "the roll closes itself when the edited clip disappears";
    EXPECT_EQ(f.doc.getTrackForClip(f.clipId), nullptr) << "and the clip really is gone from the doc";

    ASSERT_EQ(f.host.offCalls().size(), 1u) << "an unresolvable clip must not swallow the note-off";
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.calls.size(), 2u);
}

// Opening a DIFFERENT clip mid-hold: the off must go to the track the ON went to, never to whichever
// track happens to own the newly-opened clip.
TEST(PianoRollAuditionIntegrationTest, OpeningAnotherClipMidHoldRoutesTheOffToTheOriginalTrack) {
    AuditionIntegrationFixture f;
    const auto otherTrack = f.doc.addTrack(TrackKind::Midi, "Track 2");
    const auto otherClip = f.doc.addClip(otherTrack, 16.0, 8.0, "Other");
    ASSERT_NE(f.trackId, otherTrack);

    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);

    f.panel.openPianoRoll(otherClip);
    ASSERT_EQ(f.host.offCalls().size(), 1u);
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId) << "the ON's track, not the newly-opened clip's";
    EXPECT_EQ(f.host.calls.size(), 2u);
}

// A note-OFF with no preceding ON (a stray callback, or one whose ON was refused because the roll
// was closed) must reach the host as NOTHING — there is no note to release, and an unmatched off
// could cut a timeline note of the same pitch short downstream.
TEST(PianoRollAuditionIntegrationTest, AnUnmatchedOffIsNotForwarded) {
    AuditionIntegrationFixture f;
    ASSERT_TRUE(f.roll().onAuditionNote != nullptr);

    f.roll().onAuditionNote(64, 1.0f, false);
    EXPECT_TRUE(f.host.calls.empty());

    // And an ON refused because the roll is closed leaves nothing latched, so its off is inert too.
    f.panel.closePianoRoll();
    f.roll().onAuditionNote(64, 1.0f, true);
    f.roll().onAuditionNote(64, 1.0f, false);
    EXPECT_TRUE(f.host.calls.empty()) << "a closed roll auditions nothing, and releases nothing";
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
// 22. QUANTIZE: the pitch-quantize verb, its header chip and both Option+Q shortcuts.
// ============================================================================

namespace {

// Picks the first built-in preset ("Major", combo id 2 — see ScaleAssistPanel::rebuildScaleCombo)
// for whichever clip is open, which is how the panel, the chip and the key all learn the scale.
void chooseMajorScaleForOpenClip(PianoRollFixture& f) {
    f.roll.getScaleAssistPanel().getScaleCombo().setSelectedId(2, juce::sendNotificationSync);
}

} // namespace

TEST(PianoRollPitchQuantiseTest, HeaderChipQuantisesPitchesAndIsANoOpWithoutAScale) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 61, 1.0)); // C#4: out of C Major
    ASSERT_TRUE(id.isValid());
    ASSERT_FALSE(f.roll.getQuantisePitchButtonBounds().isEmpty()) << "the chip has a real rect";
    EXPECT_FALSE(f.roll.isPitchQuantiseEnabled()) << "no scale chosen -> nothing to quantise INTO";

    // Clicking it with no scale is silent and writes nothing.
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds())));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61);
    EXPECT_FALSE(f.undo.canUndo());

    chooseMajorScaleForOpenClip(f);
    EXPECT_TRUE(f.roll.isPitchQuantiseEnabled());

    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds())));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "C# snapped to the nearest in-scale pitch (ties resolve DOWN)";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61);
}

// The chip is its own hit-test region — clicking it must not reach the "Q" (start-quantise) chip
// next door, and vice versa.
TEST(PianoRollPitchQuantiseTest, TheTwoQuantiseChipsDoNotOverlapAndHitIndependently) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto startChip = f.roll.getQuantiseButtonBounds();
    const auto pitchChip = f.roll.getQuantisePitchButtonBounds();
    ASSERT_FALSE(startChip.isEmpty());
    ASSERT_FALSE(pitchChip.isEmpty());
    EXPECT_FALSE(startChip.intersects(pitchChip)) << "two chips a pixel apart would make one unclickable";
    EXPECT_GT(pitchChip.getX(), startChip.getX()) << "pitch quantize sits to the RIGHT of start quantize";

    // A note that is BOTH off-grid and out of scale: each chip must move exactly one of the two.
    const auto id = f.doc.addNote(clipId, makeNote(1.1, 61, 1.0));
    f.roll.mouseDown(leftClick(f.roll, centreOf(pitchChip)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "the pitch chip moved the pitch";
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.1) << "and left the start alone";

    f.roll.mouseDown(leftClick(f.roll, centreOf(startChip)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0) << "the Q chip moved the start";
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
}

TEST(PianoRollPitchQuantiseTest, ChipHoverIsItsOwnGatedRepaintRegion) {
    PianoRollFixture f;
    ASSERT_EQ(f.roll.headerButtonRequests, 0);

    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::QuantisePitches));
    EXPECT_EQ(f.roll.headerButtonRequests, 1);
    EXPECT_EQ(f.roll.lastHeaderButtonStrip, f.roll.getQuantisePitchButtonBounds());

    // Same chip again: nothing.
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantisePitchButtonBounds()) + juce::Point<float>(1.0f, 0.0f)));
    EXPECT_EQ(f.roll.headerButtonRequests, 1);

    // Onto the neighbouring "Q" chip: the vacated rect plus the new one.
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getQuantiseButtonBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Quantise));
    EXPECT_EQ(f.roll.headerButtonRequests, 3);
}

TEST(PianoRollPitchQuantiseTest, ChipTooltipCarriesTheDynamicShortcutHint) {
    PianoRollFixture f;
    const auto tooltip = f.roll.getTooltipFor(f.roll.getQuantisePitchButtonBounds().getCentre());
    // No ShortcutManager installed, so the hint falls back to the hardcoded Option+Shift+Q.
    EXPECT_TRUE(tooltip.startsWith("Quantize note pitches into the scale (Alt + Shift + Q)")) << tooltip;
    EXPECT_TRUE(tooltip.contains("Scale Assist")) << tooltip;
}

// Both quantise keys dispatch through keyPressed with the Option-based defaults. Stored as a KEY
// CODE plus modifiers, which is what makes them survive macOS delivering Option+letter as a Unicode
// glyph — the same reasoning FocusArbitrationTests' ShiftedSymbolKeyCodes case pins for the grid
// commands.
TEST(PianoRollPitchQuantiseTest, QAndOptionShiftQDispatchTheTwoQuantiseVerbs) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto id = f.doc.addNote(clipId, makeNote(1.1, 61, 1.0));
    ASSERT_TRUE(id.isValid());

    const juce::KeyPress bareQ('q', juce::ModifierKeys::noModifiers, 0);
    const juce::KeyPress optionShiftQ(
        'q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);

    // The MORE SPECIFIC chord is matched first, so Option+Shift+Q is never swallowed by bare Q.
    EXPECT_TRUE(f.roll.keyPressed(optionShiftQ));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.1) << "pitch quantise never touches the start";

    EXPECT_TRUE(f.roll.keyPressed(bareQ));
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0);
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60) << "start quantise never touches the pitch";

    // Neither quantise key flips the snap switch — J is what does that.
    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };
    EXPECT_TRUE(f.roll.keyPressed(bareQ));
    EXPECT_EQ(toggles, 0);
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('j')));
    EXPECT_EQ(toggles, 1);
}

// With no scale chosen the pitch key falls THROUGH (returns false) rather than being swallowed —
// the same "the key wasn't applicable" contract the arrow keys follow with an empty selection.
TEST(PianoRollPitchQuantiseTest, OptionShiftQFallsThroughWithNoScaleChosen) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 61, 1.0)).isValid());

    const juce::KeyPress optionShiftQ(
        'q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0);
    EXPECT_FALSE(f.roll.keyPressed(optionShiftQ));
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollPitchQuantiseTest, PitchQuantiseHonoursTheSelectionSubset) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto idA = f.doc.addNote(clipId, makeNote(1.0, 61, 1.0));
    const auto idB = f.doc.addNote(clipId, makeNote(5.0, 63, 1.0));
    f.roll.getSelectionForTest().setSelection({idA});

    EXPECT_TRUE(f.roll.quantisePitchesToActiveScale());
    EXPECT_EQ(f.doc.getNote(idA)->pitch, 60);
    EXPECT_EQ(f.doc.getNote(idB)->pitch, 63) << "an unselected note is untouched";
    EXPECT_TRUE(f.roll.hasNoteSelection()) << "the selection is deliberately left exactly as it was";
}

// ============================================================================
// 23. GENERATE: add-to-existing vs replace (13).
// ============================================================================

TEST(PianoRollGenerateTest, ReplaceModeClearsTheClipAndAddModeKeepsIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);

    // One hand-placed note, at a pitch generation cannot produce (its range is 60..60 below).
    const auto kept = f.doc.addNote(clipId, makeNote(0.0, 30, 1.0));
    ASSERT_TRUE(kept.isValid());

    juce::Random addRng(1234);
    f.roll.generateRandomNotesIntoClip(nullptr, 60, 60, addRng, /*addToExisting=*/true);
    const auto* clip = f.doc.getClip(clipId);
    ASSERT_NE(clip, nullptr);
    EXPECT_NE(f.doc.getNote(kept), nullptr) << "add mode never clears";
    const auto addedCount = (int)clip->notes.size();
    EXPECT_GT(addedCount, 1) << "generation placed something on top of the existing note";

    // Only the NEW notes are selected — the diff, not the whole clip.
    EXPECT_EQ((int)f.roll.getSelectionForTest().getSelected().size(), addedCount - 1);
    EXPECT_FALSE(f.roll.getSelectionForTest().contains(kept)) << "the pre-existing note is not selected";

    // Replace mode on the same clip wipes everything first.
    juce::Random replaceRng(1234);
    f.roll.generateRandomNotesIntoClip(nullptr, 60, 60, replaceRng, /*addToExisting=*/false);
    EXPECT_EQ(f.doc.getNote(kept), nullptr) << "replace mode cleared the clip";
    EXPECT_EQ((int)f.doc.getClip(clipId)->notes.size(), addedCount - 1) << "only the fresh batch remains";
}

// Re-running Generate in add mode over its OWN output must not stack unison duplicates: generation
// walks the same grid steps every time, so (pitch, startBeat) is exactly the key it collides on.
TEST(PianoRollGenerateTest, AddModeSkipsNotesThatExactlyDuplicateAnExistingPitchAndStart) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);

    // A single-pitch range makes the generated pattern fully determined: one note per grid step at
    // pitch 60, so a second run duplicates EVERY note.
    juce::Random rng1(7);
    f.roll.generateRandomNotesIntoClip(nullptr, 60, 60, rng1, /*addToExisting=*/true);
    const auto firstRun = (int)f.doc.getClip(clipId)->notes.size();
    ASSERT_GT(firstRun, 0);

    juce::Random rng2(7);
    f.roll.generateRandomNotesIntoClip(nullptr, 60, 60, rng2, /*addToExisting=*/true);
    EXPECT_EQ((int)f.doc.getClip(clipId)->notes.size(), firstRun) << "every generated note was an exact duplicate";
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty()) << "nothing was added, so nothing is selected";

    // A DIFFERENT pitch at the same starts is a chord, not a duplicate — it is kept.
    juce::Random rng3(7);
    f.roll.generateRandomNotesIntoClip(nullptr, 67, 67, rng3, /*addToExisting=*/true);
    EXPECT_EQ((int)f.doc.getClip(clipId)->notes.size(), firstRun * 2);
    EXPECT_EQ((int)f.roll.getSelectionForTest().getSelected().size(), firstRun);

    // Two runs added something, the all-duplicates run in the middle added nothing — so exactly TWO
    // undo steps exist, which is how "a mutation that added nothing pushes no undo step" is visible.
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ((int)f.doc.getClip(clipId)->notes.size(), firstRun);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getClip(clipId)->notes.empty());
    EXPECT_FALSE(f.undo.canUndo()) << "the duplicate-only run recorded nothing in between";
}

TEST(PianoRollGenerateTest, AddModeIsOneUndoStepThatLeavesTheExistingNotesAlone) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);
    const auto kept = f.doc.addNote(clipId, makeNote(0.0, 30, 1.0));

    juce::Random rng(99);
    f.roll.generateRandomNotesIntoClip(nullptr, 60, 60, rng, /*addToExisting=*/true);
    ASSERT_GT((int)f.doc.getClip(clipId)->notes.size(), 1);
    ASSERT_TRUE(f.undo.canUndo());

    f.undo.undo();
    EXPECT_EQ((int)f.doc.getClip(clipId)->notes.size(), 1) << "ONE undo removed the whole generated batch";
    EXPECT_NE(f.doc.getNote(kept), nullptr);
    EXPECT_FALSE(f.undo.canUndo());
}

// The panel's toggle drives the roll's add path end to end (the wiring in PianoRollComponent's
// constructor), so a user pressing Generate with the box ticked really does overlay.
TEST(PianoRollGenerateTest, PanelToggleDrivesTheAddPathThroughTheRollsOwnHandler) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip");
    f.open(clipId);
    const auto kept = f.doc.addNote(clipId, makeNote(0.0, 30, 1.0));

    auto& panel = f.roll.getScaleAssistPanel();
    panel.getMinNoteCombo().setSelectedId(61, juce::dontSendNotification); // pitch 60
    panel.getMaxNoteCombo().setSelectedId(61, juce::dontSendNotification);

    panel.getAddToExistingToggle().setToggleState(true, juce::dontSendNotification);
    panel.getGenerateButton().onClick();
    EXPECT_NE(f.doc.getNote(kept), nullptr) << "ticked: the hand-placed note survived";
    ASSERT_GT((int)f.doc.getClip(clipId)->notes.size(), 1);

    panel.getAddToExistingToggle().setToggleState(false, juce::dontSendNotification);
    panel.getGenerateButton().onClick();
    EXPECT_EQ(f.doc.getNote(kept), nullptr) << "unticked: Generate replaced the clip";
}

// ---------------------------------------------------------------------------
// 23b. The six header chips: distinct, non-overlapping, independently hit-testable.
// ---------------------------------------------------------------------------
//
// The glyphs themselves are pixels and not worth asserting on, but the GEOMETRY is: six drawn chips
// hit-tested by position means one chip creeping over another silently steals its clicks, and the
// header only has ~220 px to lay them out in.
TEST(PianoRollHeaderButtonTest, AllSixChipsAreDistinctNonOverlappingAndInReadingOrder) {
    PianoRollFixture f;
    const std::vector<std::pair<const char*, juce::Rectangle<int>>> chips{
        {"Clips", f.roll.getBackButtonBounds()},        {"Snap", f.roll.getSnapButtonBounds()},
        {"Quantise", f.roll.getQuantiseButtonBounds()}, {"QuantisePitches", f.roll.getQuantisePitchButtonBounds()},
        {"Scale", f.roll.getScaleButtonBounds()},       {"ScaleFilter", f.roll.getScaleFilterButtonBounds()},
    };

    for (const auto& [name, rect] : chips) {
        EXPECT_FALSE(rect.isEmpty()) << name;
        EXPECT_GE(rect.getY(), 0) << name;
        EXPECT_LE(rect.getBottom(), PianoRollComponent::kHeaderHeight) << name << " must stay inside the header strip";
    }

    for (size_t i = 0; i + 1 < chips.size(); ++i) {
        EXPECT_LT(chips[i].second.getRight(), chips[i + 1].second.getX())
            << chips[i].first << " overlaps or touches " << chips[i + 1].first;
    }

    // Each chip resolves to its OWN hover id — the same seam paintHeader's lit rect reads, so a
    // mismatch here would mean the drawn wash and the clickable area had drifted apart.
    const std::vector<std::pair<PianoRollComponent::HeaderButtonId, juce::Rectangle<int>>> ids{
        {PianoRollComponent::HeaderButtonId::Back, f.roll.getBackButtonBounds()},
        {PianoRollComponent::HeaderButtonId::Snap, f.roll.getSnapButtonBounds()},
        {PianoRollComponent::HeaderButtonId::Quantise, f.roll.getQuantiseButtonBounds()},
        {PianoRollComponent::HeaderButtonId::QuantisePitches, f.roll.getQuantisePitchButtonBounds()},
        {PianoRollComponent::HeaderButtonId::Scale, f.roll.getScaleButtonBounds()},
        {PianoRollComponent::HeaderButtonId::ScaleFilter, f.roll.getScaleFilterButtonBounds()},
    };
    for (const auto& [id, rect] : ids) {
        f.roll.mouseMove(hover(f.roll, centreOf(rect)));
        EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(id)) << "chip id " << (int)id;
    }

    // And every one of them carries a tooltip except Back (an unlabelled arrow needs none).
    for (const auto& [id, rect] : ids) {
        if (id == PianoRollComponent::HeaderButtonId::Back)
            continue;
        EXPECT_FALSE(f.roll.getTooltipFor(rect.getCentre()).isEmpty()) << "chip id " << (int)id;
    }
}

// ============================================================================
// 24. SNAP vs the DRAWN grid (3.3): the chosen division stays visible with snap off.
// ============================================================================

TEST(PianoRollGridVisibilityTest, TurningSnapOffKeepsEveryGridlineButStopsMagnetism) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    // A division FINER than a beat, so there is a subdivision level to lose in the first place.
    f.state.snap = TimelineViewState::Snap::Sixteenth;
    f.state.snapEnabled = true;

    const double division = f.roll.getDrawnGridDivisionForTest();
    ASSERT_GT(division, 0.0);
    ASSERT_LT(division, 1.0) << "the bug only shows on a sub-beat level";
    const int linesWithSnapOn = f.roll.getGridLineCountForTest(division);
    ASSERT_GT(linesWithSnapOn, 0);

    f.roll.toggleSnap();
    ASSERT_FALSE(f.state.snapEnabled);

    EXPECT_DOUBLE_EQ(f.roll.getDrawnGridDivisionForTest(), division) << "the DRAWN division is snap-independent";
    EXPECT_EQ(f.roll.getGridLineCountForTest(division), linesWithSnapOn)
        << "turning snap off must not erase a single gridline";
    // Magnetism, and only magnetism, went away.
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);
}

// Snap::Off is different from "snap switched off": no division is CHOSEN, so there genuinely is no
// sub-beat level to draw. The beat and bar levels are unconditional and stay.
TEST(PianoRollGridVisibilityTest, SnapOffAsADivisionChoiceHasNoSubBeatLevelAtAll) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_DOUBLE_EQ(f.roll.getDrawnGridDivisionForTest(), 0.0);
    EXPECT_GT(f.roll.getGridLineCountForTest(1.0), 0) << "beat lines are unconditional";
}

// ============================================================================
// 25. SHOW ONLY SCALE NOTES as a header chip + Option+S (item 1), and the scale-aware
//     Up/Down transpose it enables (item 2).
// ============================================================================

TEST(PianoRollScaleFilterTest, ChipKeyAndPanelCheckboxAllDriveOneSharedState) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    auto& toggle = f.roll.getScaleAssistPanel().getPitchVisibilityToggle();
    ASSERT_FALSE(f.roll.isScaleFilterOn());
    ASSERT_FALSE(toggle.getToggleState());
    ASSERT_FALSE(f.roll.getScaleFilterButtonBounds().isEmpty()) << "the chip has a real rect";

    // 1) The CHIP. The panel's checkbox must follow it — they are two views of one flag.
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getScaleFilterButtonBounds())));
    EXPECT_TRUE(f.roll.isScaleFilterOn());
    EXPECT_TRUE(toggle.getToggleState()) << "the panel checkbox reflects the chip";
    EXPECT_TRUE(f.roll.isRowFilterActive());

    // 2) The KEY (Option+S), which must toggle the SAME flag back off.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('s', juce::ModifierKeys::altModifier, 0)));
    EXPECT_FALSE(f.roll.isScaleFilterOn());
    EXPECT_FALSE(toggle.getToggleState());

    // 3) The PANEL CHECKBOX, still wired the other way round.
    toggle.setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(f.roll.isScaleFilterOn());
    EXPECT_TRUE(f.roll.isRowFilterActive());
}

// Option+S must not be the scale PANEL's Ctrl+S wearing a different hat.
TEST(PianoRollScaleFilterTest, OptionSDoesNotToggleTheScalePanelAndCtrlSDoesNotToggleTheFilter) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    ASSERT_FALSE(f.roll.isScalePanelTargetVisibleForTest());
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('s', juce::ModifierKeys::altModifier, 0)));
    EXPECT_TRUE(f.roll.isScaleFilterOn());
    EXPECT_FALSE(f.roll.isScalePanelTargetVisibleForTest()) << "Option+S is the FILTER, not the panel";

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0)));
    EXPECT_TRUE(f.roll.isScalePanelTargetVisibleForTest());
    EXPECT_TRUE(f.roll.isScaleFilterOn()) << "Ctrl+S is the PANEL, and left the filter alone";
}

TEST(PianoRollScaleFilterTest, TheChipIsANoOpWithNoClipOpenAndTheKeyFallsThrough) {
    PianoRollFixture f;
    ASSERT_FALSE(f.roll.isOpen());
    f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getScaleFilterButtonBounds())));
    EXPECT_FALSE(f.roll.isScaleFilterOn());
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress('s', juce::ModifierKeys::altModifier, 0)))
        << "nothing open to filter: the key keeps whatever meaning it has elsewhere";
}

// The filter is remembered PER CLIP, like the scale it goes with.
TEST(PianoRollScaleFilterTest, TheFlagIsRememberedPerClip) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "A");
    const auto clipB = f.doc.addClip(trackId, 8.0, 8.0, "B");

    f.open(clipA);
    chooseMajorScaleForOpenClip(f);
    f.roll.toggleScaleFilter();
    ASSERT_TRUE(f.roll.isScaleFilterOn());

    f.open(clipB);
    EXPECT_FALSE(f.roll.isScaleFilterOn()) << "a clip never filtered before starts off";

    f.open(clipA);
    EXPECT_TRUE(f.roll.isScaleFilterOn()) << "clip A remembered";
    EXPECT_TRUE(f.roll.getScaleAssistPanel().getPitchVisibilityToggle().getToggleState())
        << "and the panel was re-reflected on reopen";
}

// ---- Item 2: Up/Down steps by SCALE DEGREE while the filter is on ----

TEST(PianoRollScaleFilterTest, ArrowUpDownStepsByScaleDegreeWithTheFilterOn) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f); // C Major: C D E F G A B == 60 62 64 65 67 69 71

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)); // C4
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});
    f.roll.toggleScaleFilter();
    ASSERT_TRUE(f.roll.isRowFilterActive());

    // C -> D is TWO semitones, and the old chromatic step would have landed on C#, a row that is not
    // even drawn any more.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 62) << "next scale degree up, not next semitone";

    // D -> E, also two semitones.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 64);

    // E -> F is ONE semitone: the step follows the SCALE's own spacing, not a fixed interval.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 65);

    // And back down again, through the same uneven spacing.
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 64);
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 62);

    // Every landing pitch was a DRAWN row — the property the whole change exists to guarantee.
    const auto& rows = f.roll.getVisiblePitchesForTest();
    EXPECT_NE(std::find(rows.begin(), rows.end(), f.doc.getNote(id)->pitch), rows.end());
}

TEST(PianoRollScaleFilterTest, ArrowUpDownStaysChromaticWithTheFilterOff) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    // A scale IS chosen — only the filter is off, which is the case that must stay chromatic.
    chooseMajorScaleForOpenClip(f);
    ASSERT_FALSE(f.roll.isRowFilterActive());

    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 61) << "C# — out of scale, and that is correct with the filter off";
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
}

// The octave pair is unchanged: twelve SEMITONES, never twelve degrees, filter or no filter.
TEST(PianoRollScaleFilterTest, ShiftUpDownStaysAnOctaveEvenWithTheFilterOn) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});
    f.roll.toggleScaleFilter();
    ASSERT_TRUE(f.roll.isRowFilterActive());

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 72) << "an octave is 12 semitones by definition";
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.doc.getNote(id)->pitch, 60);
}

// A chord keeps its SHAPE in row space, and clamps as a group at the extremes.
TEST(PianoRollScaleFilterTest, DegreeStepMovesAChordTogetherAndClampsAsAGroup) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    chooseMajorScaleForOpenClip(f);

    const auto lo = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));  // C
    const auto mid = f.doc.addNote(clipId, makeNote(1.0, 64, 1.0)); // E
    const auto hi = f.doc.addNote(clipId, makeNote(1.0, 67, 1.0));  // G
    f.roll.getSelectionForTest().setSelection({lo, mid, hi});
    f.roll.toggleScaleFilter();

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    // Each note moves ONE DEGREE: C->D, E->F, G->A. The semitone intervals change (that is what a
    // diatonic step does); the row spacing does not.
    EXPECT_EQ(f.doc.getNote(lo)->pitch, 62);
    EXPECT_EQ(f.doc.getNote(mid)->pitch, 65);
    EXPECT_EQ(f.doc.getNote(hi)->pitch, 69);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getNote(lo)->pitch, 60);
    EXPECT_EQ(f.doc.getNote(mid)->pitch, 64);
    EXPECT_EQ(f.doc.getNote(hi)->pitch, 67);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole chord moved in ONE undo step";
}

TEST(PianoRollScaleFilterTest, DegreeStepFallsThroughWithNothingSelected) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 60, 1.0)).isValid());
    f.roll.getSelectionForTest().clear();

    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.undo.canUndo());
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

// Pitch-quantize has NO button here any more — its only entry points are the roll's header chip and
// Option+Shift+Q (see the ScaleAssistPanel class comment). Generate is what remains.
TEST(ScaleAssistPanelTest, GenerateButtonFiresItsCallback) {
    ScaleAssistPanel panel;
    std::vector<std::tuple<int, int, bool>> generated;
    panel.onGenerate = [&](int lo, int hi, bool add) { generated.emplace_back(lo, hi, add); };

    panel.getMinNoteCombo().setSelectedId(37, juce::dontSendNotification); // pitch 36 == C2
    panel.getMaxNoteCombo().setSelectedId(73, juce::dontSendNotification); // pitch 72 == C5
    EXPECT_EQ(panel.getMinPitchSelection(), 36);
    EXPECT_EQ(panel.getMaxPitchSelection(), 72);

    panel.getGenerateButton().onClick();
    ASSERT_EQ(generated.size(), 1u);
    EXPECT_EQ(std::get<0>(generated.back()), 36);
    EXPECT_EQ(std::get<1>(generated.back()), 72);
    EXPECT_FALSE(std::get<2>(generated.back())) << "Add to existing is OFF by default: Generate REPLACES";
}

// The mode control's own contract: it is what widens onGenerate's third argument, and it must start
// OFF so an existing embedding sees exactly the pre-existing replace behaviour.
TEST(ScaleAssistPanelTest, AddToExistingToggleStartsOffAndTravelsOutWithGenerate) {
    ScaleAssistPanel panel;
    std::vector<bool> addFlags;
    panel.onGenerate = [&](int, int, bool add) { addFlags.push_back(add); };

    EXPECT_FALSE(panel.isAddToExistingSelected());
    panel.getGenerateButton().onClick();
    ASSERT_EQ(addFlags.size(), 1u);
    EXPECT_FALSE(addFlags.back());

    panel.getAddToExistingToggle().setToggleState(true, juce::dontSendNotification);
    EXPECT_TRUE(panel.isAddToExistingSelected());
    panel.getGenerateButton().onClick();
    ASSERT_EQ(addFlags.size(), 2u);
    EXPECT_TRUE(addFlags.back()) << "the toggle's state at press time travels with the callback";
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
    EXPECT_EQ(panel.getRootCombo().getSelectedId(), 3);  // D
    EXPECT_EQ(panel.getScaleCombo().getSelectedId(), 3); // "Natural Minor" is presets[1] -> id 3
}

// ---- Custom-scale editor: the mini-piano toggle row (labels + layout sanity) ----

TEST(ScaleAssistPanelTest, CustomEditorTogglesAreLabelledWithTheirNoteNames) {
    static const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    ScaleAssistPanel panel;
    panel.setSize(220, 400);
    // Same "Edit custom scales..." row selection every other custom-editor test uses to reveal it.
    panel.getScaleCombo().setSelectedId(panel.getScaleCombo().getItemId(panel.getScaleCombo().getNumItems() - 1),
                                        juce::sendNotificationSync);
    ASSERT_TRUE(panel.isCustomEditorVisibleForTest());

    for (int pc = 0; pc < 12; ++pc)
        EXPECT_EQ(panel.getCustomPitchToggle(pc).getButtonText(), juce::String(kNoteNames[pc])) << "pitch class " << pc;
}

// The mini keyboard's 12 toggles must all be reachable (visible, non-empty bounds) and none may
// overlap another — a sharp centred on the boundary between two naturals is easy to get wrong by
// a pixel or two, and an overlap would make one of the two keys unclickable.
TEST(ScaleAssistPanelTest, CustomEditorTwelveToggleBoundsAreVisibleAndNonOverlapping) {
    ScaleAssistPanel panel;
    panel.setSize(220, 400); // comfortably wider than kScalePanelWidth (170) so no cell degenerates
    panel.getScaleCombo().setSelectedId(panel.getScaleCombo().getItemId(panel.getScaleCombo().getNumItems() - 1),
                                        juce::sendNotificationSync);
    ASSERT_TRUE(panel.isCustomEditorVisibleForTest());

    for (int pc = 0; pc < 12; ++pc) {
        EXPECT_TRUE(panel.getCustomPitchToggle(pc).isVisible()) << "pitch class " << pc;
        EXPECT_FALSE(panel.getCustomPitchToggle(pc).getBounds().isEmpty()) << "pitch class " << pc;
    }

    for (int a = 0; a < 12; ++a)
        for (int b = a + 1; b < 12; ++b)
            EXPECT_FALSE(
                panel.getCustomPitchToggle(a).getBounds().intersects(panel.getCustomPitchToggle(b).getBounds()))
                << "pitch classes " << a << " and " << b << " overlap";
}
