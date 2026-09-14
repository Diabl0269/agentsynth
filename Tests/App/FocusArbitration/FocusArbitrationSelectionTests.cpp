// Concern: Cut (one undo step per surface), Select All routing, and Repeat Selection --
// including the inactive states on the timeline surfaces with an empty selection.
#include "App/FocusArbitration/FocusArbitrationTestFixture.h"

// ============================================================================
// 9. Cut — per surface, one undo step each
// ============================================================================

TEST_F(FocusArbitrationTest, CutClipsIsOneUndoStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    const auto clip2 = doc.addClip(trackA, 8.0, 4.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());

    auto& panel = mc.getTimelinePanel();
    panel.getClipSelection().setSelection({clip1, clip2});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    ASSERT_TRUE(commandIsActive(mc, AppCommands::cutSelection));

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_TRUE(doc.getTrack(trackA)->clips.empty());
    EXPECT_TRUE(panel.canPasteClips()) << "a cut always leaves something pasteable";

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 2u) << "one Cmd+Z brings the whole cut back";
    EXPECT_TRUE(panel.canPasteClips()) << "the clipboard survives undoing the cut";
}

TEST_F(FocusArbitrationTest, CutNotesIsOneUndoStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 8.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 2.0, 1.0, 64, 100, 1});

    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    ASSERT_TRUE(commandIsActive(mc, AppCommands::cutSelection));

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_TRUE(doc.getClip(clip1)->notes.empty());
    EXPECT_TRUE(roll.canPasteNotes()) << "a cut always leaves something pasteable";
    EXPECT_FALSE(roll.hasNoteSelection());

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getClip(clip1)->notes.size(), 2u) << "one Cmd+Z brings the whole cut back";
}

// ============================================================================
// 10. Select All — per surface, the other surfaces untouched
// ============================================================================

TEST_F(FocusArbitrationTest, SelectAllRoutesPerSurface) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "B");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    const auto clip2 = doc.addClip(trackB, 8.0, 4.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 64, 100, 1});

    auto& panel = mc.getTimelinePanel();
    auto& roll = panel.getPianoRoll();
    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();

    // A) Graph.
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    const int graphSelected = editor.getSelectionCount();
    EXPECT_GT(graphSelected, 0);
    EXPECT_TRUE(panel.getClipSelection().isEmpty()) << "clips untouched by a graph Select All";
    EXPECT_FALSE(roll.hasNoteSelection()) << "notes untouched by a graph Select All";

    // B) TimelineClips.
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    EXPECT_EQ(panel.getClipSelection().size(), 2) << "every clip on every track";
    EXPECT_EQ(editor.getSelectionCount(), graphSelected) << "graph selection untouched";
    EXPECT_FALSE(roll.hasNoteSelection()) << "notes untouched by a clips Select All";

    // C) PianoRoll — openPianoRoll clears the note selection, so this really is Select All's doing.
    panel.openPianoRoll(clip1);
    ASSERT_TRUE(roll.isOpen());
    ASSERT_FALSE(roll.hasNoteSelection());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    EXPECT_EQ(roll.getSelectionForTest().getSelected().size(), 2u) << "every note in the open clip";
    EXPECT_EQ(panel.getClipSelection().size(), 2) << "clip selection untouched by a roll Select All";
    EXPECT_EQ(editor.getSelectionCount(), graphSelected) << "graph selection untouched";
}

// ============================================================================
// 11. Repeat — performRepeatSelection() skips the dialog, so tests never go modal
// ============================================================================

TEST_F(FocusArbitrationTest, RepeatSelectionTilesClips) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());

    auto& panel = mc.getTimelinePanel();
    panel.getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::repeatSelection));

    ASSERT_TRUE(mc.performRepeatSelection(3));
    ASSERT_EQ(doc.getTrack(trackA)->clips.size(), 4u) << "the source plus three copies";

    // Block length is the selection's span (4 beats), so the copies tile at 4, 8, 12.
    const auto created = panel.getClipSelection().getSelected();
    ASSERT_EQ(created.size(), 3u) << "the repeat's own copies end up selected";
    std::vector<double> starts;
    for (auto id : created)
        starts.push_back(doc.getClip(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], 4.0);
    EXPECT_DOUBLE_EQ(starts[1], 8.0);
    EXPECT_DOUBLE_EQ(starts[2], 12.0);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u) << "one undo step for the whole repeat";
}

TEST_F(FocusArbitrationTest, RepeatSelectionClampsToTheMaxRepeatCount) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());

    auto& panel = mc.getTimelinePanel();
    panel.getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    ASSERT_TRUE(mc.performRepeatSelection(1000));
    ASSERT_EQ(doc.getTrack(trackA)->clips.size(), 65u) << "the source plus a clamped 64 copies";

    const auto created = panel.getClipSelection().getSelected();
    EXPECT_EQ(created.size(), 64u) << "the count is clamped, not the requested 1000";

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u) << "one undo step for the whole (clamped) repeat";
    EXPECT_FALSE(um.canUndo());
}

TEST_F(FocusArbitrationTest, RepeatSelectionRepeatsNotes) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 16.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});

    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::repeatSelection));

    ASSERT_TRUE(mc.performRepeatSelection(3));
    ASSERT_EQ(doc.getClip(clip1)->notes.size(), 4u) << "the source plus three copies";

    const auto created = roll.getSelectionForTest().getSelected();
    ASSERT_EQ(created.size(), 3u);
    std::vector<double> starts;
    for (auto id : created)
        starts.push_back(doc.getNote(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], 1.0) << "one span (1 beat) apart";
    EXPECT_DOUBLE_EQ(starts[1], 2.0);
    EXPECT_DOUBLE_EQ(starts[2], 3.0);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getClip(clip1)->notes.size(), 1u) << "one undo step for the whole repeat";
}

// ============================================================================
// 12. Cut/Repeat inactive states on the timeline surfaces
// ============================================================================

TEST_F(FocusArbitrationTest, CutAndRepeatInactiveWithEmptyTimelineSelections) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});

    auto& panel = mc.getTimelinePanel();
    ASSERT_TRUE(panel.getClipSelection().isEmpty());

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::repeatSelection));

    // An OPEN roll with no note selection is still nothing to cut or repeat.
    panel.openPianoRoll(clip1);
    ASSERT_TRUE(panel.getPianoRoll().isOpen());
    ASSERT_FALSE(panel.getPianoRoll().hasNoteSelection());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::repeatSelection));

    auto& cm = mc.getCommandManager();
    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::repeatSelection, false));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}
