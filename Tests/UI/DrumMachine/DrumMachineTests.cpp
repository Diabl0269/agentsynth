// The drum machine step grid: cell mapping, editing gestures as undo steps, rows, steps per bar, the panel hosting
// and the keyboard and screen-reader paths.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "Timeline/TimelineSnapshot.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/DrumMachine/DrumMachineHost.h"
#include "UI/Layout/FadeVisibility.h"

using synth::ui::DrumMachineComponent;

namespace {

struct DrumFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    synth::TrackId track;
    ClipId clip;
    std::vector<std::pair<int, bool>> auditions;

    DrumFixture() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 320);
        grid.onAuditionNote = [this](int pitch, float, bool on) { auditions.emplace_back(pitch, on); };
        grid.openClip(clip);
    }

    juce::Point<float> centre(int pitch, int step) {
        const int row = grid.rowOfPitch(pitch);
        EXPECT_GE(row, 0) << "pitch " << pitch << " has a row";
        return centreOf(grid.cellBounds(row, step));
    }
    void click(int pitch, int step, int mods = 0) {
        const auto p = centre(pitch, step);
        grid.mouseDown(leftClick(grid, p, mods));
        grid.mouseUp(leftClick(grid, p, mods));
    }
    const synth::Clip& c() { return *doc.getClip(clip); }
};

} // namespace

TEST(DrumMachineGridTest, ClickAddsOneNoteAtTheCellStartAsOneUndoStep) {
    DrumFixture f;
    f.click(38, 4);
    ASSERT_EQ(f.c().notes.size(), 1u);
    EXPECT_EQ(f.c().notes[0].pitch, 38);
    EXPECT_DOUBLE_EQ(f.c().notes[0].startBeat, 1.0) << "step 4 of 16 per 4/4 bar is beat 1";
    EXPECT_DOUBLE_EQ(f.c().notes[0].lengthBeats, 0.25);
    EXPECT_EQ(f.c().notes[0].velocity, 100);
    f.undo.undo();
    EXPECT_TRUE(f.c().notes.empty()) << "the whole click was one undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineGridTest, AddingAHitAuditionsItOnceNoteOnThenOff) {
    DrumFixture f;
    f.click(36, 0);
    ASSERT_EQ(f.auditions.size(), 2u);
    EXPECT_EQ(f.auditions[0], std::make_pair(36, true));
    EXPECT_EQ(f.auditions[1], std::make_pair(36, false));
}

TEST(DrumMachineGridTest, ClickingAnOccupiedCellRemovesEveryNoteOfThatPitchStartingInIt) {
    DrumFixture f;
    f.doc.addNote(f.clip, makeNote(1.0, 38, 0.25));
    f.doc.addNote(f.clip, makeNote(1.1, 38, 0.1));  // same cell (step 4 spans 1.0 to 1.25)
    f.doc.addNote(f.clip, makeNote(1.0, 36, 0.25)); // other pitch, same step
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.click(38, 4);
    ASSERT_EQ(f.c().notes.size(), 1u);
    EXPECT_EQ(f.c().notes[0].pitch, 36);
    f.undo.undo();
    EXPECT_EQ(f.c().notes.size(), 3u);
}

TEST(DrumMachineGridTest, ShiftClickAddsAnAccentAndTogglesAnExistingOne) {
    DrumFixture f;
    f.click(42, 2, juce::ModifierKeys::shiftModifier);
    ASSERT_EQ(f.c().notes.size(), 1u);
    EXPECT_EQ(f.c().notes[0].velocity, 127);
    f.click(42, 2, juce::ModifierKeys::shiftModifier);
    ASSERT_EQ(f.c().notes.size(), 1u);
    EXPECT_EQ(f.c().notes[0].velocity, 100) << "toggled back off";
    f.click(42, 2, juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.c().notes[0].velocity, 127);
}

TEST(DrumMachineGridTest, DraggingAHitSetsItsLoudnessAndCommitsOnce) {
    DrumFixture f;
    f.doc.addNote(f.clip, makeNote(0.0, 36, 0.25));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    const auto start = f.centre(36, 0);
    f.grid.mouseDown(leftClick(f.grid, start));
    for (int dy = -10; dy >= -30; dy -= 10)
        f.grid.mouseDrag(leftDrag(f.grid, start + juce::Point<float>(0.0f, (float)dy), start));
    EXPECT_EQ(f.c().notes[0].velocity, 100) << "nothing is written while the drag is live";
    EXPECT_GT(f.grid.getCellVelocity(f.grid.rowOfPitch(36), 0), 100) << "but the cell previews it";
    f.grid.mouseUp(leftDrag(f.grid, start + juce::Point<float>(0.0f, -30.0f), start));
    EXPECT_EQ(f.c().notes.size(), 1u) << "a drag never removes the hit";
    EXPECT_EQ(f.c().notes[0].velocity, 127) << "100 plus 30 px of drag clamps at 127";
    f.undo.undo();
    EXPECT_EQ(f.c().notes[0].velocity, 100);
    EXPECT_FALSE(f.undo.canUndo()) << "one undo step for the whole drag";
}

TEST(DrumMachineGridTest, RowsWithHitsAreAlwaysShownIncludingOffTablePitches) {
    DrumFixture f;
    EXPECT_EQ(f.grid.getLaneCount(), 8) << "the default rows";
    EXPECT_LT(f.grid.rowOfPitch(51), 0) << "Ride is behind Show all drums";
    f.doc.addNote(f.clip, makeNote(0.0, 51, 0.25));
    f.doc.addNote(f.clip, makeNote(0.0, 70, 0.25));
    f.grid.refreshFromDoc();
    f.grid.finishMotionsForTest();
    EXPECT_GE(f.grid.rowOfPitch(51), 0) << "a pitch holding a note is always shown";
    ASSERT_GE(f.grid.rowOfPitch(70), 0) << "so is a pitch outside the table";
    EXPECT_EQ(f.grid.getLaneCount(), 10);
    f.grid.setFocusCell({f.grid.rowOfPitch(70), 0});
    EXPECT_TRUE(f.grid.getAccessibilityValueText().startsWith("Note 70, step 1"));
}

TEST(DrumMachineGridTest, ShowAllDrumsAddsTheRestOfTheKit) {
    DrumFixture f;
    f.grid.setShowAllDrums(true);
    f.grid.finishMotionsForTest();
    EXPECT_EQ(f.grid.getLaneCount(), 20);
    EXPECT_GE(f.grid.rowOfPitch(51), 0);
}

TEST(DrumMachineGridTest, StepsPerBarChangeKeepsNotesWhereTheyAreInTime) {
    DrumFixture f;
    f.doc.addNote(f.clip, makeNote(1.0, 36, 0.25));
    f.grid.refreshFromDoc();
    f.undo.clearUndoHistory();
    f.grid.getStepsCombo().setSelectedId(1, juce::sendNotificationSync); // 8 per bar
    EXPECT_EQ(f.c().stepsPerBar, 8);
    EXPECT_DOUBLE_EQ(f.c().notes[0].startBeat, 1.0);
    EXPECT_EQ(f.grid.getStepCount(), 16);
    EXPECT_EQ(f.grid.getCellVelocity(f.grid.rowOfPitch(36), 2), 100) << "beat 1 is step 2 of 8 per bar";
    f.undo.undo();
    EXPECT_EQ(f.c().stepsPerBar, 16);
}

TEST(DrumMachineGridTest, MuteAndSoloToggleThroughTheClipAsOneUndoStep) {
    DrumFixture f;
    f.grid.getRowLabels().getMuteButton(42)->setToggleState(true, juce::dontSendNotification);
    f.grid.getRowLabels().getMuteButton(42)->onClick();
    EXPECT_EQ(f.c().mutedPitches, (std::vector<int>{42}));
    f.undo.undo();
    EXPECT_TRUE(f.c().mutedPitches.empty());
}

TEST(DrumMachineGridTest, KeyboardTogglesHitAccentAndLoudness) {
    DrumFixture f;
    f.grid.setFocusCell({f.grid.rowOfPitch(38), 3});
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_EQ(f.c().notes.size(), 1u);
    EXPECT_EQ(f.c().notes[0].velocity, 100);
    EXPECT_TRUE(f.grid.getAccessibilityValueText().contains("Snare, step 4, hit, loudness 100"));
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.c().notes[0].velocity, 127);
    EXPECT_TRUE(f.grid.getAccessibilityValueText().endsWith("accent"));
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.c().notes[0].velocity, 117);
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(f.c().notes.empty());
    EXPECT_TRUE(f.grid.getAccessibilityValueText().endsWith("empty"));
}

TEST(DrumMachineGridTest, ArrowKeysMoveTheFocusCellAndEscapeCloses) {
    DrumFixture f;
    bool closed = false;
    f.grid.onCloseRequested = [&] { closed = true; };
    f.grid.setFocusCell({0, 0});
    f.grid.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    f.grid.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    EXPECT_EQ(f.grid.getFocusCell(), (DrumMachineComponent::Cell{1, 1}));
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.grid.isOpen());
    EXPECT_TRUE(closed);
}

TEST(DrumMachineGridTest, AHitGrowsInAndShrinksOutThenLandsWhenAnimationIsOffScreen) {
    DrumFixture f;
    f.click(36, 1);
    EXPECT_FALSE(f.grid.isHitMotionActive(f.grid.rowOfPitch(36), 1)) << "off screen the final state lands at once";
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.click(36, 2);
    EXPECT_TRUE(f.grid.isHitMotionActive(f.grid.rowOfPitch(36), 2));
    f.grid.finishMotionsForTest();
    EXPECT_FALSE(f.grid.isHitMotionActive(f.grid.rowOfPitch(36), 2));
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineGridTest, PaintsAHitAtItsVelocityHeight) {
    DrumFixture f;
    f.doc.addNote(f.clip, [] {
        auto n = makeNote(0.0, 36, 0.25);
        n.velocity = 64;
        return n;
    }());
    f.grid.refreshFromDoc();
    juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    f.grid.paintEntireComponent(g, false);
    const auto cell = f.grid.cellBounds(f.grid.rowOfPitch(36), 0);
    const auto laneColour = synth::ui::defaultDrumLanes().front().colour;
    const auto bottom = image.getPixelAt(cell.getCentreX(), cell.getBottom() - 3);
    const auto top = image.getPixelAt(cell.getCentreX(), cell.getY() + 3);
    EXPECT_EQ(bottom.getRed(), laneColour.getRed()) << "the lower half is filled";
    EXPECT_NE(top.getRed(), laneColour.getRed()) << "a velocity near half leaves the top empty";
}

TEST(DrumMachineGridTest, EveryOtherBeatGroupOfEmptyCellsIsShaded) {
    DrumFixture f;
    juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    f.grid.paintEntireComponent(g, false);
    const int row = f.grid.rowOfPitch(36);
    const auto pixel = [&](int step) {
        const auto cell = f.grid.cellBounds(row, step);
        return image.getPixelAt(cell.getCentreX(), cell.getCentreY());
    };
    const auto shade = [&](int step) { return pixel(step).getBrightness(); };
    // 16 steps in a 4/4 bar: a beat group is four cells, and beats 2 and 4 are the shaded ones.
    EXPECT_FLOAT_EQ(shade(0), shade(3));
    EXPECT_GT(shade(4), shade(0)) << "beat 2 is a different shade from beat 1";
    EXPECT_FLOAT_EQ(shade(4), shade(7));
    EXPECT_FLOAT_EQ(shade(0), shade(8));
    EXPECT_FLOAT_EQ(shade(4), shade(12));
    const auto& colors = synth::theme::themeOf(f.grid).colors;
    const auto expected = colors.surface.overlaidWith(colors.textPrimary.withAlpha(0.03f));
    EXPECT_NEAR(pixel(4).getRed(), expected.getRed(), 1) << "the shade is text-primary at 3% over the cell";
    EXPECT_NEAR(pixel(4).getGreen(), expected.getGreen(), 1);
    EXPECT_NEAR(pixel(4).getBlue(), expected.getBlue(), 1);
    EXPECT_NEAR(pixel(0).getRed(), colors.surface.getRed(), 1);
}

//==============================================================================
// Hosted in the timeline panel.

namespace {
struct DrumKitHost : AuditionRecordingHost {
    synth::TrackId drumTrack;
    bool drumKitRows(synth::TrackId t, synth::ui::KitRows*) const override { return t == drumTrack; }
};

struct PanelFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumKitHost host;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId drums, keys;
    ClipId drumClip, keyClip;

    PanelFixture() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        panel.setSize(1200, 360);
        panel.getViewState().pixelsPerBeat = 40.0;
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTrackHeaderHost(&host);
        drums = doc.addTrack(TrackKind::Midi, "Drums");
        keys = doc.addTrack(TrackKind::Midi, "Keys");
        host.drumTrack = drums;
        drumClip = doc.addClip(drums, 0.0, 8.0, "Beat 1");
        keyClip = doc.addClip(keys, 0.0, 8.0, "Chords");
        panel.resized();
    }
    ~PanelFixture() { panel.setTrackHeaderHost(nullptr); }

    void doubleClick(ClipId id) {
        auto& lane = panel.getClipLaneArea();
        const auto p = centreOf(lane.getClipRect(id));
        lane.mouseDoubleClick(leftClick(lane, p));
    }
};
} // namespace

TEST(DrumMachinePanelTest, DoubleClickOnADrumKitClipOpensTheGridAndOtherTracksOpenTheRoll) {
    PanelFixture f;
    f.doubleClick(f.drumClip);
    EXPECT_TRUE(f.panel.getDrumMachine().isOpen());
    EXPECT_FALSE(f.panel.isPianoRollOpen());
    EXPECT_TRUE(f.panel.clipEditorOpen());
    f.panel.closeDrumMachine();
    EXPECT_FALSE(f.panel.clipEditorOpen());

    f.doubleClick(f.keyClip);
    EXPECT_TRUE(f.panel.isPianoRollOpen());
    EXPECT_FALSE(f.panel.getDrumMachine().isOpen());
}

TEST(DrumMachinePanelTest, OpeningTheGridSetsTheRulerMappingAndTheToolbarCarve) {
    PanelFixture f;
    const auto closedTop = f.panel.getClipLaneArea().getBounds().getY();
    f.panel.openDrumMachine(f.drumClip);
    auto& grid = f.panel.getDrumMachine();
    EXPECT_LT(grid.getBounds().getY(), closedTop) << "the grid starts above the ruler: its toolbar is the top row";
    EXPECT_GE(grid.canvasTop(), DrumMachineComponent::kToolbarHeight);
    EXPECT_EQ(grid.getBounds().getBottom(), f.panel.getClipLaneArea().getBounds().getBottom());
    EXPECT_EQ(DrumMachineComponent::kToolbarHeight, PianoRollComponent::kToolbarHeight);
    f.panel.closeDrumMachine();
    EXPECT_FALSE(f.panel.getDrumMachine().isOpen());
}

TEST(DrumMachinePanelTest, SwitchingToPianoRollAndBackKeepsTheSameClip) {
    PanelFixture f;
    f.panel.openDrumMachine(f.drumClip);
    f.panel.getDrumMachine().onPianoRollRequested(f.drumClip);
    EXPECT_FALSE(f.panel.getDrumMachine().isOpen());
    ASSERT_TRUE(f.panel.isPianoRollOpen());
    EXPECT_EQ(f.panel.getPianoRoll().getClipId(), f.drumClip);

    auto* chip = f.panel.getPianoRoll().getHeaderChipButtonForTest(PianoRollComponent::HeaderButtonId::DrumMachine);
    ASSERT_NE(chip, nullptr);
    EXPECT_TRUE(chip->isVisible()) << "the roll offers the switch back on a Drum Kit track";
    chip->onClick();
    EXPECT_FALSE(f.panel.isPianoRollOpen());
    ASSERT_TRUE(f.panel.getDrumMachine().isOpen());
    EXPECT_EQ(f.panel.getDrumMachine().getClipId(), f.drumClip);
}

TEST(DrumMachinePanelTest, ThePianoRollOnAnotherTrackHasNoDrumMachineChip) {
    PanelFixture f;
    f.panel.openPianoRoll(f.keyClip);
    auto* chip = f.panel.getPianoRoll().getHeaderChipButtonForTest(PianoRollComponent::HeaderButtonId::DrumMachine);
    ASSERT_NE(chip, nullptr);
    EXPECT_FALSE(chip->isVisible());
}

TEST(DrumMachinePanelTest, AuditionRoutesToTheClipsTrack) {
    PanelFixture f;
    f.panel.openDrumMachine(f.drumClip);
    f.panel.getDrumMachine().onAuditionNote(36, 0.8f, true);
    f.panel.getDrumMachine().onAuditionNote(36, 0.8f, false);
    ASSERT_EQ(f.host.onCalls().size(), 1u);
    EXPECT_EQ(f.host.onCalls()[0].track, f.drums);
    EXPECT_EQ(f.host.offCalls().size(), 1u);
}

TEST(DrumMachinePanelTest, DeletingTheClipClosesTheGrid) {
    PanelFixture f;
    f.panel.openDrumMachine(f.drumClip);
    ASSERT_TRUE(f.doc.removeClip(f.drumClip));
    f.panel.getDrumMachine().refreshFromDoc();
    EXPECT_FALSE(f.panel.getDrumMachine().isOpen());
    EXPECT_FALSE(f.panel.clipEditorOpen());
}

//==============================================================================
// The clip settings the grid edits.

namespace {
synth::MidiNote noteAt(double beat, int pitch) {
    synth::MidiNote n;
    n.startBeat = beat;
    n.pitch = pitch;
    n.lengthBeats = 0.25;
    return n;
}

std::vector<int> snapshotPitches(const TimelineDoc& doc) {
    auto snapshot = synth::TimelineSnapshot::buildFrom(doc);
    std::vector<int> pitches;
    for (const auto& e : snapshot->notes)
        pitches.push_back(e.pitch);
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}
} // namespace

TEST(ClipDrumSettingsTest, DefaultsAreOmittedFromTheFile) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 4.0, "Beat");
    const auto text = juce::JSON::toString(doc.toVar());
    EXPECT_FALSE(text.contains("stepsPerBar"));
    EXPECT_FALSE(text.contains("mutedPitches"));
    EXPECT_FALSE(text.contains("soloPitches"));
    EXPECT_EQ(doc.getClip(clip)->stepsPerBar, 16);
}

TEST(ClipDrumSettingsTest, SettingsRoundTripThroughToVarAndFromVar) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 4.0, "Beat");
    ASSERT_TRUE(doc.setClipStepsPerBar(clip, 32));
    ASSERT_TRUE(doc.setClipPitchMuted(clip, 42, true));
    ASSERT_TRUE(doc.setClipPitchMuted(clip, 38, true));
    ASSERT_TRUE(doc.setClipPitchSoloed(clip, 36, true));

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    const auto* c = loaded.getClip(clip);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->stepsPerBar, 32);
    EXPECT_EQ(c->mutedPitches, (std::vector<int>{38, 42})) << "kept sorted";
    EXPECT_EQ(c->soloPitches, (std::vector<int>{36}));
}

TEST(ClipDrumSettingsTest, InvalidValuesAreRejected) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 4.0, "Beat");
    EXPECT_FALSE(doc.setClipStepsPerBar(clip, 12));
    EXPECT_FALSE(doc.setClipPitchMuted(clip, 200, true));
    EXPECT_EQ(doc.getClip(clip)->stepsPerBar, 16);
    EXPECT_TRUE(doc.getClip(clip)->mutedPitches.empty());
}

TEST(ClipDrumSettingsTest, DuplicateAndSplitCarryTheSettings) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 8.0, "Beat");
    ASSERT_TRUE(doc.setClipStepsPerBar(clip, 8));
    ASSERT_TRUE(doc.setClipPitchMuted(clip, 42, true));
    ASSERT_TRUE(doc.setClipPitchSoloed(clip, 36, true));

    const auto copy = doc.duplicateClip(clip);
    ASSERT_TRUE(copy.isValid());
    EXPECT_EQ(doc.getClip(copy)->stepsPerBar, 8);
    EXPECT_EQ(doc.getClip(copy)->mutedPitches, (std::vector<int>{42}));
    EXPECT_EQ(doc.getClip(copy)->soloPitches, (std::vector<int>{36}));

    const auto halves = doc.splitClip(clip, 4.0);
    ASSERT_TRUE(halves.second.isValid());
    EXPECT_EQ(doc.getClip(halves.second)->stepsPerBar, 8);
    EXPECT_EQ(doc.getClip(halves.second)->mutedPitches, (std::vector<int>{42}));
    EXPECT_EQ(doc.getClip(halves.second)->soloPitches, (std::vector<int>{36}));
}

TEST(ClipDrumSettingsTest, SnapshotSkipsMutedPitchesWithoutTouchingTheNoteFlag) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 4.0, "Beat");
    doc.addNote(clip, noteAt(0.0, 36));
    doc.addNote(clip, noteAt(0.5, 42));
    ASSERT_TRUE(doc.setClipPitchMuted(clip, 42, true));

    EXPECT_EQ(snapshotPitches(doc), (std::vector<int>{36}));
    for (const auto& n : doc.getClip(clip)->notes)
        EXPECT_FALSE(n.muted) << "a drum mute never writes the piano roll's note mute";
}

TEST(ClipDrumSettingsTest, SoloLimitsThePlayingPitchesToTheSoloedOnes) {
    TimelineDoc doc;
    const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, "Drums"), 0.0, 4.0, "Beat");
    doc.addNote(clip, noteAt(0.0, 36));
    doc.addNote(clip, noteAt(0.5, 38));
    doc.addNote(clip, noteAt(1.0, 42));
    ASSERT_TRUE(doc.setClipPitchSoloed(clip, 38, true));
    EXPECT_EQ(snapshotPitches(doc), (std::vector<int>{38}));

    ASSERT_TRUE(doc.setClipPitchSoloed(clip, 38, false));
    EXPECT_EQ(snapshotPitches(doc), (std::vector<int>{36, 38, 42})) << "no solo means everything plays";
}
