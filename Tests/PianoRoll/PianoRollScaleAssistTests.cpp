// PianoRoll scale-assist tests: the header button + gutter shift, quantisePitchesToScale,
// generateRandomNotesIntoClip, per-clip memory, PropertiesFile persistence, the scale-panel SLIDE
// animation, SHOW ONLY SCALE NOTES as a header chip + Option+S and the scale-aware Up/Down
// transpose it enables, and the ScaleAssistPanel component in isolation via its own accessors.
// Shared PianoRollFixture and makeScaleAssistTestProps live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "../../Source/Timeline/MusicalScale.h"

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
