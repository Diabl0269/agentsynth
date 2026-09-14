// TimelineClipEditing drag tests: the Split tool's hover preview repaint budget, the Select tool's Alt-copy, plain move
// drag, and cross-track drags.

#include "TimelineClipEditingTestHelpers.h"
#include "TimelineClipEditingToolTestHelpers.h"

// ------------------------------------------------ Split tool hover preview --

namespace {
// The repaint-count seam, same subclass-and-count idiom PianoRollComponent's playhead strip uses.
class CountingToolLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    int previewRepaints = 0;

protected:
    void requestToolPreviewRepaint(juce::Rectangle<int> region) override {
        ++previewRepaints;
        TimelineClipLaneArea::requestToolPreviewRepaint(region);
    }
};
} // namespace

TEST(TimelineClipToolTest, SplitPreviewRepaintsOnlyWhenTheSnappedBeatChanges) {
    TimelineDoc doc;
    TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = TimelineViewState::Snap::Quarter;
    state.snapEnabled = true;
    state.rowHeightScale = 1.0;
    state.trackScrollY = 0.0;
    synth::ui::ClipSelectionModel selection;
    CountingToolLane lane{state, selection};
    lane.setTimelineDoc(&doc);
    lane.setSize(1200, 400);

    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c"); // x in [0, 320)
    ASSERT_TRUE(clip.isValid());
    lane.setActiveTool(EditTool::Split);
    lane.previewRepaints = 0;

    const float y = (float)(lane.getRowHeight() / 2);

    lane.mouseMove(hoverAt(lane, {100.0f, y})); // beat 2.5 -> snapped 3.0
    ASSERT_TRUE(lane.getSplitPreviewForTest().has_value());
    EXPECT_DOUBLE_EQ(lane.getSplitPreviewForTest()->beat, 3.0);
    EXPECT_EQ(lane.previewRepaints, 1);

    // Both of these still snap to 3.0 — the preview is unchanged, so nothing repaints.
    lane.mouseMove(hoverAt(lane, {105.0f, y})); // beat 2.625
    lane.mouseMove(hoverAt(lane, {125.0f, y})); // beat 3.125
    EXPECT_EQ(lane.previewRepaints, 1) << "movement inside one snap cell must cost zero repaints";
    EXPECT_DOUBLE_EQ(lane.getSplitPreviewForTest()->beat, 3.0);

    lane.mouseMove(hoverAt(lane, {140.0f, y})); // beat 3.5 -> snapped 4.0
    EXPECT_EQ(lane.previewRepaints, 2) << "crossing into the next cell costs exactly one";
    EXPECT_DOUBLE_EQ(lane.getSplitPreviewForTest()->beat, 4.0);

    // Leaving the lanes drops the line (one more repaint, over where it was).
    lane.mouseExit(hoverAt(lane, {140.0f, y}));
    EXPECT_FALSE(lane.getSplitPreviewForTest().has_value());
    EXPECT_EQ(lane.previewRepaints, 3);
}

// ------------------------------------------------------- Alt-drag copy ------

TEST(TimelineClipToolTest, AltDragCopiesTheSelectionInOneUndoStep) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.addNote(clip, makeNote(1.0, 64)).isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2.0 beats at 40 px/beat
    const int alt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(toolClick(f.lane, anchor, alt));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor, alt));
    EXPECT_TRUE(f.lane.isCopyDragForTest());
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 0.0) << "the original must not move mid-drag";
    f.lane.mouseUp(toolDrag(f.lane, dragged, anchor, alt));

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 0.0) << "the original stayed exactly where it was";
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 2.0);
    EXPECT_EQ(clips[1].notes.size(), 1u) << "a copy is a deep copy";

    const auto selected = f.selection.getSelected();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], clips[1].id) << "the COPY ends up selected";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole copy-drag was ONE undo step";
}

TEST(TimelineClipToolTest, AltClickWithoutADragCopiesNothing) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    clickWithTool(f.lane, clipCentre(f.lane, clip), juce::ModifierKeys::altModifier);

    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

// The user-visible half of the Alt copy-drag, and the one the doc assertions above could not
// see: what is on SCREEN between mouseDown and mouseUp. The originals must not move on either
// axis (the doc is untouched mid-drag either way, so only the effective geometry can prove it),
// and the ghosts must carry the delta instead.
TEST(TimelineClipToolTest, AltDragLeavesEveryOriginalInPlaceWhileTheGhostsCarryTheDelta) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto first = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto second = f.doc.addClip(track, 8.0, 2.0, "b");
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    f.selection.setSelection({first, second});

    const auto anchor = clipCentre(f.lane, first);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y); // +2.0 beats at 40 px/beat
    const int alt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(toolClick(f.lane, anchor, alt));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor, alt));
    ASSERT_TRUE(f.lane.isCopyDragForTest());

    // Both originals paint at their DOC geometry — this is the regression: before the fix the
    // start followed previewDeltaBeats_ while only the row was copy-guarded, so the original
    // slid sideways under the pointer and the gesture read as a move.
    const auto firstGeometry = f.lane.getEffectiveGeometryForTest(first);
    ASSERT_TRUE(firstGeometry.has_value());
    EXPECT_DOUBLE_EQ(firstGeometry->first, 0.0) << "an Alt-dragged original must not move mid-drag";
    EXPECT_DOUBLE_EQ(firstGeometry->second, 4.0);
    const auto secondGeometry = f.lane.getEffectiveGeometryForTest(second);
    ASSERT_TRUE(secondGeometry.has_value());
    EXPECT_DOUBLE_EQ(secondGeometry->first, 8.0) << "every original in the selection, not just the grabbed one";
    EXPECT_DOUBLE_EQ(secondGeometry->second, 2.0);

    // The ghosts are what moved — one per dragged clip, each at origin + the shared delta.
    const int rowHeight = f.lane.getRowHeight();
    const auto ghosts = f.lane.getDragGhostRectsForTest();
    ASSERT_EQ(ghosts.size(), 2u);
    EXPECT_EQ(ghosts[0], synth::ui::TimelineClipLaneArea::computeClipRect(f.state, 0, 2.0, 4.0, rowHeight));
    EXPECT_EQ(ghosts[1], synth::ui::TimelineClipLaneArea::computeClipRect(f.state, 0, 10.0, 2.0, rowHeight));

    f.lane.mouseUp(toolDrag(f.lane, dragged, anchor, alt));
    EXPECT_TRUE(f.lane.getDragGhostRectsForTest().empty()) << "the ghosts end with the gesture";
}

TEST(TimelineClipToolTest, AltDragAcrossRowsPutsOnlyTheGhostOnTheDestinationRow) {
    ToolLaneFixture f;
    const auto upper = f.doc.addTrack(TrackKind::Midi, "Upper");
    ASSERT_TRUE(f.doc.addTrack(TrackKind::Midi, "Lower").isValid());
    const auto clip = f.doc.addClip(upper, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y + f.rowHeightF()); // one row down, +2 beats
    const int alt = juce::ModifierKeys::altModifier;

    f.lane.mouseDown(toolClick(f.lane, anchor, alt));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor, alt));
    ASSERT_TRUE(f.lane.isCopyDragForTest());
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 1);

    const auto geometry = f.lane.getEffectiveGeometryForTest(clip);
    ASSERT_TRUE(geometry.has_value());
    EXPECT_DOUBLE_EQ(geometry->first, 0.0) << "the vertical half of the drag must not move the original either";

    const auto ghosts = f.lane.getDragGhostRectsForTest();
    ASSERT_EQ(ghosts.size(), 1u);
    EXPECT_EQ(ghosts[0], synth::ui::TimelineClipLaneArea::computeClipRect(f.state, 1, 2.0, 4.0, f.lane.getRowHeight()))
        << "the ghost is the only thing on the destination row";
}

// The other side of the same guard: a PLAIN move-drag still previews on the original, which is
// what makes a normal drag look like the clip following the pointer.
TEST(TimelineClipToolTest, PlainMoveDragStillPreviewsTheDeltaOnTheOriginal) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dragged(anchor.x + 80.0f, anchor.y);

    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dragged, anchor));
    EXPECT_FALSE(f.lane.isCopyDragForTest());

    const auto geometry = f.lane.getEffectiveGeometryForTest(clip);
    ASSERT_TRUE(geometry.has_value());
    EXPECT_DOUBLE_EQ(geometry->first, 2.0) << "a plain drag previews on the clip itself";
    EXPECT_DOUBLE_EQ(geometry->second, 4.0);
    EXPECT_TRUE(f.lane.getDragGhostRectsForTest().empty()) << "and draws no ghosts at all";

    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 0.0) << "the DOC still only changes on mouseUp";
    f.lane.mouseUp(toolDrag(f.lane, dragged, anchor));
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 2.0);
}

// ------------------------------------------------------- Cross-track drag ---

TEST(TimelineClipToolTest, CrossTrackDragMovesTheClipToTheRowBelow) {
    ToolLaneFixture f;
    const auto upper = f.doc.addTrack(TrackKind::Midi, "Upper");
    const auto lower = f.doc.addTrack(TrackKind::Midi, "Lower");
    const auto clip = f.doc.addClip(upper, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    const auto note = f.doc.addNote(clip, makeNote(1.0, 64, 2.0, 90, 3));
    ASSERT_TRUE(note.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dropped(anchor.x + 80.0f, anchor.y + f.rowHeightF()); // one row down, +2 beats
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 1);
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    EXPECT_TRUE(f.doc.getTrack(upper)->clips.empty());
    ASSERT_EQ(f.doc.getTrack(lower)->clips.size(), 1u);
    const auto& moved = f.doc.getTrack(lower)->clips[0];
    EXPECT_EQ(moved.id, clip) << "a cross-track move keeps the clip's identity";
    EXPECT_DOUBLE_EQ(moved.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(moved.lengthBeats, 4.0);
    EXPECT_EQ(moved.name, "c");
    ASSERT_EQ(moved.notes.size(), 1u);
    EXPECT_EQ(moved.notes[0].id, note);
    EXPECT_DOUBLE_EQ(moved.notes[0].startBeat, 1.0) << "notes are clip-relative, so they travel untouched";
    EXPECT_EQ(moved.notes[0].velocity, 90);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(upper)->clips.size(), 1u);
}

TEST(TimelineClipToolTest, CrossTrackDragOntoAKindMismatchClampsToTheSameLane) {
    ToolLaneFixture f;
    const auto midi = f.doc.addTrack(TrackKind::Midi, "Midi");
    const auto audio = f.doc.addTrack(TrackKind::Audio, "Audio");
    const auto clip = f.doc.addClip(midi, 0.0, 4.0, "c"); // no assetRef -> a MIDI clip
    ASSERT_TRUE(clip.isValid());
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dropped(anchor.x + 80.0f, anchor.y + f.rowHeightF()); // onto the Audio row
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 0) << "an illegal drop clamps the row delta, it does not refuse";
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    EXPECT_TRUE(f.doc.getTrack(audio)->clips.empty());
    ASSERT_EQ(f.doc.getTrack(midi)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(midi)->clips[0].startBeat, 2.0) << "the horizontal half of the drag still lands";
}

TEST(TimelineClipToolTest, AudioClipDragsOntoAnotherAudioTrack) {
    ToolLaneFixture f;
    const auto first = f.doc.addTrack(TrackKind::Audio, "A1");
    const auto second = f.doc.addTrack(TrackKind::Audio, "A2");
    const auto clip = f.doc.addClip(first, 0.0, 4.0, "take");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(f.doc.setClipGainDb(clip, -3.0));
    f.selection.setSelection({clip});

    const auto anchor = clipCentre(f.lane, clip);
    const juce::Point<float> dropped(anchor.x, anchor.y + f.rowHeightF());
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    ASSERT_EQ(f.doc.getTrack(second)->clips.size(), 1u);
    const auto& moved = f.doc.getTrack(second)->clips[0];
    EXPECT_EQ(moved.assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(moved.sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(moved.gainDb, -3.0);
}

TEST(TimelineClipToolTest, MixedKindGroupDragClampsTheWholeGroupToItsOriginalTracks) {
    ToolLaneFixture f;
    const auto midiTrack = f.doc.addTrack(TrackKind::Midi, "Midi");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio1");
    const auto audioTrack2 = f.doc.addTrack(TrackKind::Audio, "Audio2");
    const auto midiClip = f.doc.addClip(midiTrack, 0.0, 4.0, "m");
    const auto audioClip = f.doc.addClip(audioTrack, 0.0, 4.0, "a");
    ASSERT_TRUE(midiClip.isValid() && audioClip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(audioClip, "Audio/take.wav", 0.0));
    // Selected together: the MIDI clip's one-row-down destination (the Audio track) is a kind
    // mismatch, which clamps the row delta for the WHOLE group, even though the audio clip's own
    // one-row-down destination (the second Audio track) would have been legal on its own.
    f.selection.setSelection({midiClip, audioClip});

    const auto anchor = clipCentre(f.lane, midiClip);
    const juce::Point<float> dropped(anchor.x, anchor.y + f.rowHeightF()); // one row down, no horizontal move
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, dropped, anchor));
    EXPECT_EQ(f.lane.getPreviewRowDeltaForTest(), 0) << "one mismatched clip in the group clamps them all";
    f.lane.mouseUp(toolDrag(f.lane, dropped, anchor));

    ASSERT_EQ(f.doc.getTrack(midiTrack)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(midiTrack)->clips[0].startBeat, 0.0);
    ASSERT_EQ(f.doc.getTrack(audioTrack)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(audioTrack)->clips[0].startBeat, 0.0);
    EXPECT_TRUE(f.doc.getTrack(audioTrack2)->clips.empty()) << "neither clip crossed tracks";
}

// -------------------------------------------------------------- Rename ------
