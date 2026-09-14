// TimelineClipEditing tool tests: Split, Glue, Erase, Mute and Draw driven through synth::ui::TimelineClipLaneArea.

#include "TimelineClipEditingToolTestHelpers.h"

// ------------------------------------------------------------- Split tool --

TEST(TimelineClipToolTest, SplitToolClickSplitsAtTheSnappedBeat) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 8.0, "c"); // x in [0, 320)
    ASSERT_TRUE(clip.isValid());
    f.lane.setActiveTool(EditTool::Split);

    // x = 140 -> beat 3.5 -> snapped (ties round up) to 4.0.
    clickWithTool(f.lane, {140.0f, f.rowCentreY(0)});

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0);
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(clips[1].lengthBeats, 4.0);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "one click was ONE undo step";
}

TEST(TimelineClipToolTest, SplitToolClickOnEmptyLaneSpaceDoesNothing) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    ASSERT_TRUE(f.doc.addClip(track, 0.0, 4.0, "c").isValid());
    f.lane.setActiveTool(EditTool::Split);
    const auto revisionBefore = f.doc.getRevision();

    clickWithTool(f.lane, {900.0f, f.rowCentreY(0)}); // far right of the clip

    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_TRUE(f.selection.isEmpty()) << "a tool click must not select either";
}

TEST(TimelineClipToolTest, SplitToolClickAtTheClipBoundaryDoesNothing) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 8.0, "c"); // x in [0, 320)
    ASSERT_TRUE(clip.isValid());
    f.lane.setActiveTool(EditTool::Split);
    const auto revisionBefore = f.doc.getRevision();

    // x = 1 -> beat 0.025 -> snapped to 0.0, exactly the clip's own start: not strictly inside.
    clickWithTool(f.lane, {1.0f, f.rowCentreY(0)});
    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);

    // x = 319 -> beat 7.975 -> snapped to 8.0, exactly the clip's own end: not strictly inside.
    clickWithTool(f.lane, {319.0f, f.rowCentreY(0)});
    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
}

// -------------------------------------------------------------- Glue tool --

TEST(TimelineClipToolTest, GlueToolJoinsWithTheNextClipAcrossAGap) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 6.0, 4.0, "b"); // gap [4, 6): legal, becomes silence
    ASSERT_TRUE(a.isValid() && b.isValid());
    EXPECT_EQ(f.lane.findGlueTarget(a), b);
    f.lane.setActiveTool(EditTool::Glue);

    clickWithTool(f.lane, clipCentre(f.lane, a));

    ASSERT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    const auto* joined = f.doc.getClip(a);
    ASSERT_NE(joined, nullptr);
    EXPECT_DOUBLE_EQ(joined->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(joined->lengthBeats, 10.0);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipToolTest, GlueToolPrunesTheSwallowedClipFromTheSelection) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 6.0, 4.0, "b"); // gap [4, 6): legal, becomes silence
    ASSERT_TRUE(a.isValid() && b.isValid());
    EXPECT_EQ(f.lane.findGlueTarget(a), b);
    f.selection.setSelection({a, b});
    f.lane.setActiveTool(EditTool::Glue);

    clickWithTool(f.lane, clipCentre(f.lane, a));

    ASSERT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_TRUE(f.selection.contains(a)) << "the survivor stays selected";
    EXPECT_FALSE(f.selection.contains(b)) << "the absorbed clip leaves the selection";
}

TEST(TimelineClipToolTest, GlueToolWithNothingAfterItLeavesNoUndoEntry) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto only = f.doc.addClip(track, 0.0, 4.0, "only");
    ASSERT_TRUE(only.isValid());
    EXPECT_FALSE(f.lane.findGlueTarget(only).isValid());
    f.lane.setActiveTool(EditTool::Glue);
    const auto revisionBefore = f.doc.getRevision();

    clickWithTool(f.lane, clipCentre(f.lane, only));

    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo()) << "a refused glue must not push an empty undo step";
}

TEST(TimelineClipToolTest, GlueTargetSkipsAnOverlappingClip) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    ASSERT_TRUE(f.doc.addClip(track, 2.0, 4.0, "overlapping").isValid()); // joinClips refuses this
    const auto after = f.doc.addClip(track, 8.0, 2.0, "after");
    ASSERT_TRUE(a.isValid() && after.isValid());

    EXPECT_EQ(f.lane.findGlueTarget(a), after) << "the first NON-overlapping clip is the target";
}

// ------------------------------------------------------------- Erase tool --

TEST(TimelineClipToolTest, EraseToolClickDeletesTheClipItHits) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 8.0, 4.0, "b");
    ASSERT_TRUE(a.isValid() && b.isValid());
    // Selection-independent: b is selected, and clicking a still erases a.
    f.selection.setSelection({b});
    f.lane.setActiveTool(EditTool::Erase);

    clickWithTool(f.lane, clipCentre(f.lane, a));

    EXPECT_EQ(f.doc.getClip(a), nullptr);
    ASSERT_NE(f.doc.getClip(b), nullptr);
    EXPECT_TRUE(f.selection.contains(b));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());
}

// -------------------------------------------------------------- Mute tool --

TEST(TimelineClipToolTest, MuteToolClickTogglesTheClipFlagBothWays) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(clip.isValid());
    ASSERT_FALSE(f.doc.getClip(clip)->muted);
    f.lane.setActiveTool(EditTool::Mute);

    clickWithTool(f.lane, clipCentre(f.lane, clip));
    EXPECT_TRUE(f.doc.getClip(clip)->muted) << "one press, one toggle — the release must not toggle back";

    clickWithTool(f.lane, clipCentre(f.lane, clip));
    EXPECT_FALSE(f.doc.getClip(clip)->muted);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getClip(clip)->muted) << "each toggle is its own undo step";
}

// -------------------------------------------------------------- Draw tool --

TEST(TimelineClipToolTest, DrawToolDragCreatesAClipOfTheDraggedLength) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    f.lane.setActiveTool(EditTool::Draw);

    const juce::Point<float> anchor(40.0f, f.rowCentreY(0));   // beat 1.0, floor-snapped to 1.0
    const juce::Point<float> release(200.0f, f.rowCentreY(0)); // beat 5.0, ceil-snapped to 5.0
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, release, anchor));
    EXPECT_FALSE(f.lane.getDrawGhostRectForTest().isEmpty()) << "the drag previews a ghost";
    f.lane.mouseUp(toolDrag(f.lane, release, anchor));

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0);
    EXPECT_TRUE(f.selection.contains(clips[0].id));
    EXPECT_TRUE(f.lane.getDrawGhostRectForTest().isEmpty()) << "the ghost is gone once the clip is real";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getTrack(track)->clips.empty());
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipToolTest, DrawToolPlainClickCreatesTheOneBarClip) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    ClipId opened;
    f.lane.onClipDoubleClicked = [&opened](ClipId id) { opened = id; };
    f.lane.setActiveTool(EditTool::Draw);

    clickWithTool(f.lane, {40.0f, f.rowCentreY(0)}); // beat 1.0, no drag

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0) << "one bar at the no-transport 4/4 fallback";
    EXPECT_EQ(opened, clips[0].id) << "the pencil click lands in the note editor, like the double-click";
}

TEST(TimelineClipToolTest, DrawToolIsInertOnAnAudioRow) {
    ToolLaneFixture f;
    const auto audio = f.doc.addTrack(TrackKind::Audio, "A");
    ASSERT_TRUE(audio.isValid());
    f.lane.setActiveTool(EditTool::Draw);
    const auto revisionBefore = f.doc.getRevision();

    const juce::Point<float> anchor(40.0f, f.rowCentreY(0));
    const juce::Point<float> release(200.0f, f.rowCentreY(0));
    f.lane.mouseDown(toolClick(f.lane, anchor));
    f.lane.mouseDrag(toolDrag(f.lane, release, anchor));
    f.lane.mouseUp(toolDrag(f.lane, release, anchor));

    EXPECT_TRUE(f.doc.getTrack(audio)->clips.empty()) << "a pencil cannot draw an asset";
    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_FALSE(f.undo.canUndo());
}
