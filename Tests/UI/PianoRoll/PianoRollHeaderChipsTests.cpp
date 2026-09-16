// PianoRoll header-chip tests: the six header chips' hover wash + resting/active fill affordance,
// and GENERATE — add-to-existing vs replace, plus the six chips' distinct/non-overlapping bounds.
// Shared PianoRollFixture, rgbDistance and chooseMajorScaleForOpenClip live in
// PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"

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
        {"Clips", f.roll.getBackButtonBounds()},
        {"Quantise", f.roll.getQuantiseButtonBounds()},
        {"QuantiseLength", f.roll.getQuantiseLengthButtonBounds()},
        {"QuantisePitches", f.roll.getQuantisePitchButtonBounds()},
        {"Scale", f.roll.getScaleButtonBounds()},
        {"ScaleFilter", f.roll.getScaleFilterButtonBounds()},
    };

    for (const auto& [name, rect] : chips) {
        EXPECT_FALSE(rect.isEmpty()) << name;
        EXPECT_GE(rect.getY(), 0) << name;
        EXPECT_LE(rect.getBottom(), PianoRollComponent::kToolbarHeight) << name << " must stay inside the toolbar row";
    }

    for (size_t i = 0; i + 1 < chips.size(); ++i) {
        EXPECT_LT(chips[i].second.getRight(), chips[i + 1].second.getX())
            << chips[i].first << " overlaps or touches " << chips[i + 1].first;
    }

    // Each chip resolves to its OWN hover id — the same seam paintHeader's lit rect reads, so a
    // mismatch here would mean the drawn wash and the clickable area had drifted apart.
    const std::vector<std::pair<PianoRollComponent::HeaderButtonId, juce::Rectangle<int>>> ids{
        {PianoRollComponent::HeaderButtonId::Back, f.roll.getBackButtonBounds()},
        {PianoRollComponent::HeaderButtonId::Quantise, f.roll.getQuantiseButtonBounds()},
        {PianoRollComponent::HeaderButtonId::QuantiseLength, f.roll.getQuantiseLengthButtonBounds()},
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
