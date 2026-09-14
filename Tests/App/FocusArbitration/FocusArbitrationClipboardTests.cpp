// Concern: the one focus-ownership rule (MainComponent::resolveEditSurface) for Cmd+C/V/D
// across the Graph, TimelineClips and PianoRoll surfaces -- see MainComponent.h's
// EditSurface/resolveEditSurface comment and docs/shortcuts.md for the production rule this pins.
#include "FocusArbitrationTestFixture.h"

// ============================================================================
// 1. Graph surface — unchanged behaviour
// ============================================================================

TEST_F(FocusArbitrationTest, CopyPasteDuplicateOnGraphUnchanged) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();
    auto& graph = mc.getAudioEngine().getGraph();

    editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {200, 200}));
    editor.selectAllModules();
    ASSERT_GT(editor.getSelectionCount(), 0);
    const int afterOneModule = graph.getNumNodes();

    // asynchronously = false: the async path posts a CommandMessage and needs a message loop.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(editor.canPaste());

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_GT(graph.getNumNodes(), afterOneModule);

    const int afterPaste = graph.getNumNodes();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_GT(graph.getNumNodes(), afterPaste);
}

// ============================================================================
// 1b. Graph surface — Cut is copy + the ordinary delete path, as ONE undo step
//
// The graph has no cut verb of its own: perform() composes copySelection() (clipboard only, no
// graph mutation, no undo entry) with deleteSelection() (one recordStructuralChange, the same
// transaction the Delete key uses). This pins both halves — the clipboard is filled AND a single
// undo brings every cut module back.
// ============================================================================

TEST_F(FocusArbitrationTest, CutModulesIsOneGraphUndoStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();

    // Two specific nodes rather than selectAllModules(): the cut has to come back as ONE step, and
    // that is only meaningful with more than one module in the transaction.
    auto nodeA = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    auto nodeB = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(nodeA, nullptr);
    ASSERT_NE(nodeB, nullptr);
    editor.setSelectedNodes({nodeA->nodeID, nodeB->nodeID});
    ASSERT_EQ(editor.getSelectionCount(), 2);

    const int nodesBefore = graph.getNumNodes();
    ASSERT_TRUE(commandIsActive(mc, AppCommands::cutSelection));

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_EQ(graph.getNumNodes(), nodesBefore - 2);
    EXPECT_TRUE(editor.canPaste()) << "the copy half ran before the delete half";
    EXPECT_EQ(editor.getSelectionCount(), 0);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "one Cmd+Z brings the whole cut back";
    EXPECT_TRUE(editor.canPaste()) << "the clipboard survives undoing the cut";
}

// ============================================================================
// 1c. Graph surface — Cut needs a selection; Repeat is unsupported here outright
// ============================================================================

TEST_F(FocusArbitrationTest, CutInactiveAndRepeatUnsupportedOnGraph) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();
    ASSERT_EQ(editor.getSelectionCount(), 0) << "a fresh canvas starts with nothing selected";

    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));

    // Repeat stays inactive on the graph even WITH a selection — it is a time-axis verb and the
    // canvas has no time axis (Duplicate is the graph's answer). performRepeatSelection agrees, so
    // a direct/scripted caller gets the same "no" the greyed-out menu row does.
    editor.selectAllModules();
    ASSERT_GT(editor.getSelectionCount(), 0);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::cutSelection)) << "Cut follows the selection";
    EXPECT_FALSE(commandIsActive(mc, AppCommands::repeatSelection));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::repeatSelection, false));
    EXPECT_FALSE(mc.performRepeatSelection(3));
}

// ============================================================================
// 2. TimelineClips surface — copy/paste rebased at the (snapped) playhead
// ============================================================================

TEST_F(FocusArbitrationTest, CopyPasteClipsRebasedAtPlayhead) {
    // This test's whole point is pinning the DOCUMENTED DEFAULT snap (Quarter, enabled) that
    // TimelinePanelComponent restores at construction — unlike every test below that calls
    // pinSnapOff() (which overrides the in-memory state AFTER construction and so never actually
    // exercises restoreViewPreferences()), so reset the persisted keys to absent before building
    // MainComponent and put the developer's real values back afterward.
    PersistedKeysGuard snapGuard({"timelineSnap", "timelineSnapEnabled"});
    resetTimelineSnapKeysToDefault();

    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "B");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 60, 100, 1});
    const auto clip2 = doc.addClip(trackB, 8.0, 2.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());

    auto& clipSelection = mc.getTimelinePanel().getClipSelection();
    clipSelection.setSelection({clip1, clip2});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    // Locate the transport, then drive one deterministic host block so the LocateBeat command
    // drains and the position snapshot reflects it — see the file header's Hosted-mode rule.
    auto& transport = engine.getTransport();
    transport.locateBeat(20.25);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 20.25);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(mc.getTimelinePanel().canPasteClips());

    const int clipCountBefore = (int)doc.getTrack(trackA)->clips.size() + (int)doc.getTrack(trackB)->clips.size();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));

    const auto pasted = clipSelection.getSelected();
    ASSERT_EQ(pasted.size(), 2u) << "pasted clips end up selected";

    // Default snap is Beat (1.0 beat) at the transport's default 4/4 -> snapBeat(20.25, 4) == 20.0.
    const double expectedEarliest = mc.getTimelinePanel().getViewState().snapBeat(20.25, 4.0);
    ASSERT_DOUBLE_EQ(expectedEarliest, 20.0);

    std::vector<double> starts;
    for (auto id : pasted)
        starts.push_back(doc.getClip(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], expectedEarliest) << "earliest pasted clip lands at the snapped playhead";
    EXPECT_DOUBLE_EQ(starts[1], expectedEarliest + 8.0) << "relative offset (8 beats) preserved";

    // Landed back on their ORIGINAL tracks.
    bool sawTrackA = false, sawTrackB = false;
    for (auto id : pasted) {
        const auto* track = doc.getTrackForClip(id);
        ASSERT_NE(track, nullptr);
        sawTrackA |= track->id == trackA;
        sawTrackB |= track->id == trackB;
        // The clip that landed on trackA is the C1 copy and must carry its note through.
        if (track->id == trackA) {
            const auto* clip = doc.getClip(id);
            ASSERT_EQ(clip->notes.size(), 1u);
            EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 1.0);
        }
    }
    EXPECT_TRUE(sawTrackA);
    EXPECT_TRUE(sawTrackB);

    const int clipCountAfter = (int)doc.getTrack(trackA)->clips.size() + (int)doc.getTrack(trackB)->clips.size();
    EXPECT_EQ(clipCountAfter, clipCountBefore + 2);

    // ONE undo step removes both pasted clips together.
    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    const int clipCountAfterUndo = (int)doc.getTrack(trackA)->clips.size() + (int)doc.getTrack(trackB)->clips.size();
    EXPECT_EQ(clipCountAfterUndo, clipCountBefore) << "one Cmd+Z must remove the whole pasted group";
}

// ============================================================================
// 3. TimelineClips surface — missing-track fallback
// ============================================================================

TEST_F(FocusArbitrationTest, PasteWithMissingTrackFallsBack) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "B");
    const auto clipOnB = doc.addClip(trackB, 4.0, 2.0, "OnB");
    ASSERT_TRUE(clipOnB.isValid());

    auto& clipSelection = mc.getTimelinePanel().getClipSelection();
    clipSelection.setSelection({clipOnB});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));

    // The clip's original track disappears before the paste.
    ASSERT_TRUE(doc.removeTrack(trackB));
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));

    const auto pasted = clipSelection.getSelected();
    ASSERT_EQ(pasted.size(), 1u) << "falls back to the remaining MIDI track rather than being dropped";
    const auto* fallbackTrack = doc.getTrackForClip(pasted[0]);
    ASSERT_NE(fallbackTrack, nullptr);
    EXPECT_EQ(fallbackTrack->id, trackA);

    // Now there is NO Midi track left at all — the clip must be skipped outright.
    ASSERT_TRUE(doc.removeTrack(trackA));
    ASSERT_TRUE(doc.getTracks().empty());
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_TRUE(doc.getTracks().empty()) << "nothing to fall back to -> the clip is skipped, not misplaced";
}

// ============================================================================
// 4. TimelineClips surface — duplicate, one undo step
// ============================================================================

TEST_F(FocusArbitrationTest, DuplicateClipsOneStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    const auto clip2 = doc.addClip(trackA, 8.0, 4.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());

    auto& clipSelection = mc.getTimelinePanel().getClipSelection();
    clipSelection.setSelection({clip1, clip2});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));

    const auto selected = clipSelection.getSelected();
    ASSERT_EQ(selected.size(), 2u) << "the new copies end up selected";
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 4u);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 2u) << "one undo step removes both duplicates together";
}

// ============================================================================
// 5. PianoRoll surface — C/V/D with nothing to act on
//
// The v1 gap (an unconditional setActive(false) on this surface) is GONE: the three verbs now route
// into the roll's own note clipboard, so "inactive" has to come from the roll's real state — no
// selection, and a clipboard with nowhere to go — rather than from the command wiring refusing to
// look. Same arrangement the old gap test used, so a regression that re-hardcodes setActive(false)
// still passes here and fails the two tests below it.
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollSurfaceCVDInactiveWithNothingSelected) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    mc.getTimelinePanel().getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    // The roll was never opened and nothing is selected in it: Copy/Duplicate have no notes, and
    // Paste has neither a clipboard nor an open clip (canPasteNotes needs BOTH).
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_FALSE(roll.isOpen());
    ASSERT_FALSE(roll.hasNoteSelection());
    for (auto cmdId : {AppCommands::copySelection, AppCommands::pasteSelection, AppCommands::duplicateSelection,
                       AppCommands::cutSelection}) {
        EXPECT_FALSE(commandIsActive(mc, cmdId))
            << "command " << cmdId << " must be inactive on an empty piano-roll surface";
    }

    auto& cm = mc.getCommandManager();
    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_EQ(doc.getRevision(), revisionBefore) << "an inactive command must never touch the doc";
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u);

    // Opening the roll alone does NOT make Paste available — the note clipboard is still empty.
    mc.getTimelinePanel().openPianoRoll(clip1);
    ASSERT_TRUE(roll.isOpen());
    EXPECT_FALSE(commandIsActive(mc, AppCommands::pasteSelection))
        << "an open clip with an empty note clipboard still has nothing to paste";
}

// ============================================================================
// 5b. PianoRoll surface — the verbs are ACTIVE once notes are selected
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollCopyDuplicateCutActiveWithNoteSelection) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 8.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 64, 100, 1});

    // openPianoRoll clears the note selection, so select AFTER opening.
    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.isOpen());
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    EXPECT_TRUE(commandIsActive(mc, AppCommands::copySelection));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::duplicateSelection));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::pasteSelection)) << "nothing copied yet";

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(roll.canPasteNotes());
    EXPECT_TRUE(commandIsActive(mc, AppCommands::pasteSelection)) << "a filled note clipboard enables Paste";

    // Duplicate lands the block one span (2 beats) later: 2.0 and 3.0.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    ASSERT_EQ(doc.getClip(clip1)->notes.size(), 4u);

    // The clipboard survives losing the selection; Copy/Duplicate/Cut do not.
    roll.getSelectionForTest().clear();
    EXPECT_FALSE(commandIsActive(mc, AppCommands::copySelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::duplicateSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::pasteSelection));
}

// ============================================================================
// 5c. PianoRoll surface — Paste anchors on the LIVE transport beat
//
// The roll has no transport of its own: pasteNotesAtPlayhead anchors on the last beat something
// PUSHED into it via setPlayheadBeat, and a stopped transport never pushes one. This pins the
// priming step in perform()'s PianoRoll paste arm — without it the anchor would be a stale 0.0,
// which (being outside the edited clip) would collapse to the clip's start.
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollPasteAnchorsOnPrimedPlayhead) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    // Deliberately NOT at beat 0: an un-primed paste would anchor at a stale 0.0, which is outside
    // this clip and collapses to its start — a result this test can tell apart from the real one.
    const auto clip1 = doc.addClip(trackA, 4.0, 8.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 64, 100, 1});

    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.isOpen());
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    // Locate, then drain one host block so the position snapshot reflects it (file-header rule).
    // The transport is STOPPED throughout — which is precisely the case that needs priming.
    auto& transport = engine.getTransport();
    transport.locateBeat(6.0);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 6.0);
    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));

    // Absolute beat 6.0 inside a clip starting at 4.0 -> clip-relative anchor 2.0 (snap pinned off).
    const auto pasted = roll.getSelectionForTest().getSelected();
    ASSERT_EQ(pasted.size(), 2u) << "the pasted block becomes the selection";
    std::vector<double> starts;
    for (auto id : pasted)
        starts.push_back(doc.getNote(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], 2.0) << "anchored on the LIVE transport beat, not a stale 0.0";
    EXPECT_DOUBLE_EQ(starts[1], 3.0) << "the block keeps its internal shape";
    EXPECT_EQ(doc.getClip(clip1)->notes.size(), 4u);
}
