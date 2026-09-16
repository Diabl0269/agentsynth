// PianoRoll painting/geometry tests: KEY LABELS (paintKeysColumn's pure per-row decision), ROW
// MAPPING (yForPitch/pitchForY through visiblePitches_, scale-context filtering), NOTE COLOURING
// through synth::ui::resolveNoteColour, KEYS-COLUMN geometry (the black-key inset seam plus a
// snapshot smoke test), and the TOOLBAR ROW's three-band vertical layout above the ruler.
// Shared PianoRollFixture, cMajorContains and rgbDistance live in PianoRollTestHelpers.h; the WCAG
// contrast helpers below are single-use here, so they stay local.

#include "PianoRollTestHelpers.h"

#include "UI/PianoRoll/NoteColour.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"

namespace {
// WCAG relative luminance / contrast ratio — same formula NoteColourTests.cpp uses, duplicated
// (not shared) because it's a handful of pure lines private to this translation unit.
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
} // namespace

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
    for (int y = f.roll.canvasTop(); y < f.roll.getHeight(); ++y)
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
// "canvasTop() + llround((firstRow - pitchRow) * ps)" int-only math did — reproduced verbatim
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
        const int expectedY = f.roll.canvasTop() + (int)std::llround((double)(firstRow - pitch) * ps);
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
// 28. TOOLBAR ROW ABOVE THE RULER — the roll's three-band vertical layout.
// ============================================================================

TEST(PianoRollLayoutTest, ChipsStayInTheToolbarRowAndTheCanvasStartsBelowTheRulerBand) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    // No band by default: the layout collapses to toolbar-then-canvas, i.e. the original geometry.
    EXPECT_EQ(f.roll.getRulerBandHeight(), 0);
    EXPECT_EQ(f.roll.canvasTop(), PianoRollComponent::kToolbarHeight);

    constexpr int kBand = 18;
    f.roll.setRulerBandHeight(kBand);
    EXPECT_EQ(f.roll.getRulerBandHeight(), kBand);
    EXPECT_EQ(f.roll.canvasTop(), PianoRollComponent::kToolbarHeight + kBand);

    // The chips did NOT move: they are the top row, above the band.
    for (const auto& rect :
         {f.roll.getBackButtonBounds(), f.roll.getQuantiseButtonBounds(), f.roll.getQuantisePitchButtonBounds(),
          f.roll.getScaleButtonBounds(), f.roll.getScaleFilterButtonBounds()}) {
        EXPECT_LE(rect.getBottom(), PianoRollComponent::kToolbarHeight);
    }

    // The canvas regions start BELOW the band — no overlap with either row above them.
    EXPECT_EQ(f.roll.getKeysColumnBounds().getY(), f.roll.canvasTop());
    EXPECT_EQ(f.roll.getNoteGridBounds().getY(), f.roll.canvasTop());
    EXPECT_GE(f.roll.getKeysColumnBounds().getY(), PianoRollComponent::kToolbarHeight + kBand);

    // And the row mapping followed: the top row's y is the canvas top, not the toolbar's bottom.
    EXPECT_EQ(f.roll.yForPitch(f.roll.getFirstVisiblePitchForTest()), f.roll.canvasTop());
}

TEST(PianoRollLayoutTest, SettingTheSameRulerBandHeightTwiceIsANoOp) {
    PianoRollFixture f;
    f.roll.setRulerBandHeight(14);
    const auto keysBefore = f.roll.getKeysColumnBounds();
    f.roll.setRulerBandHeight(14);
    EXPECT_EQ(f.roll.getKeysColumnBounds(), keysBefore);
    // Negatives are clamped rather than inverting the layout.
    f.roll.setRulerBandHeight(-5);
    EXPECT_EQ(f.roll.getRulerBandHeight(), 0);
}

// A band taller than the component must not produce a negative-height canvas.
TEST(PianoRollLayoutTest, AnAbsurdlyTallBandDegradesToAnEmptyCanvasRatherThanNegativeBounds) {
    PianoRollFixture f;
    f.roll.setSize(900, 40);
    f.roll.setRulerBandHeight(500);
    EXPECT_GE(f.roll.getKeysColumnBounds().getHeight(), 0);
    EXPECT_GE(f.roll.getNoteGridBounds().getHeight(), 0);
}

// The whole point of the exercise, asserted against the REAL panel: toolbar row, then ruler, then
// canvas, tiling the lanes region top to bottom with no overlap.
TEST(PianoRollLayoutTest, ThePanelPutsTheToolbarRowAboveTheRulerWithNoOverlap) {
    AuditionIntegrationFixture f;
    auto& r = f.roll();
    ASSERT_TRUE(r.isOpen());

    const auto rollBounds = r.getBounds();
    const auto rulerBounds = f.panel.getRuler().getBounds();
    ASSERT_FALSE(rollBounds.isEmpty());
    ASSERT_FALSE(rulerBounds.isEmpty());

    // 1. The toolbar row is the TOP of the roll's rect, and the ruler starts exactly where it ends.
    EXPECT_EQ(rulerBounds.getY(), rollBounds.getY() + PianoRollComponent::kToolbarHeight)
        << "the ruler sits immediately below the toolbar row, not above it";
    EXPECT_GT(rulerBounds.getY(), rollBounds.getY()) << "the toolbar is ABOVE the ruler";

    // 2. The roll knows the band's height, so its canvas starts below the ruler.
    EXPECT_EQ(r.getRulerBandHeight(), rulerBounds.getHeight());
    const int canvasTopInPanel = rollBounds.getY() + r.canvasTop();
    EXPECT_EQ(canvasTopInPanel, rulerBounds.getBottom()) << "the note canvas begins where the ruler ends";

    // 3. The three bands tile without overlapping: chips above the ruler, canvas below it.
    for (const auto& chip : {r.getQuantiseButtonBounds(), r.getScaleButtonBounds(), r.getScaleFilterButtonBounds()}) {
        const int chipBottomInPanel = rollBounds.getY() + chip.getBottom();
        EXPECT_LE(chipBottomInPanel, rulerBounds.getY()) << "a chip overlaps the ruler";
    }
    EXPECT_GE(rollBounds.getY() + r.getKeysColumnBounds().getY(), rulerBounds.getBottom());

    // Closing collapses it: the ruler returns to the top of the lanes region and the roll drops back
    // to exactly the clip-lane rect (an invisible component left sitting over the ruler reads as a
    // bug). The BAND height stays pushed in, deliberately — see the note in the panel's resized():
    // openPianoRoll frames a clip before the re-layout, so canvasTop() has to be right beforehand.
    const int rulerTopWhileOpen = rulerBounds.getY();
    f.panel.closePianoRoll();
    EXPECT_EQ(f.panel.getRuler().getBounds().getY(), rollBounds.getY())
        << "closed, the ruler is the TOP row of the lanes region again";
    EXPECT_LT(f.panel.getRuler().getBounds().getY(), rulerTopWhileOpen) << "i.e. it moved back up";
    EXPECT_EQ(r.getBounds(), f.panel.getClipLaneArea().getBounds())
        << "closed, the roll occupies exactly the clip-lane rect again";
    EXPECT_EQ(r.getRulerBandHeight(), rulerBounds.getHeight()) << "the band stays known while closed";
}

// The framing-order guarantee the unconditional band push exists for: the very FIRST clip opened must
// be framed against the real canvas height, not against a canvasTop() that is still missing the ruler
// band. A clip framed too tall centres its notes wrong and zooms to the wrong fit.
TEST(PianoRollLayoutTest, TheFirstClipOpenedIsFramedAgainstTheRealCanvasTop) {
    AuditionIntegrationFixture f; // its ctor calls openPianoRoll for the first time
    auto& r = f.roll();
    ASSERT_TRUE(r.isOpen());

    const auto rulerHeight = f.panel.getRuler().getBounds().getHeight();
    ASSERT_GT(rulerHeight, 0);
    EXPECT_EQ(r.canvasTop(), PianoRollComponent::kToolbarHeight + rulerHeight)
        << "canvasTop() already accounted for the ruler band when the clip was framed";
    // The framed row mapping must land the top row exactly at that canvas top.
    EXPECT_EQ(r.yForPitch(r.getFirstVisiblePitchForTest()), r.canvasTop());
}
