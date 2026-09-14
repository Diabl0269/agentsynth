// TimelineClipEditing context-menu tests: inline rename commit and the ToggleMute/GlueWithNext context-menu hook.

#include "TimelineClipEditingToolTestHelpers.h"

TEST(TimelineClipToolTest, RenameCommitsAndABlankNameKeepsTheOldOne) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "Original");
    ASSERT_TRUE(clip.isValid());

    f.lane.renameClip(clip, "  Chorus  ");
    EXPECT_EQ(f.doc.getClip(clip)->name, "Chorus") << "setClipName trims";
    ASSERT_TRUE(f.undo.canUndo());

    f.lane.renameClip(clip, "   ");
    EXPECT_EQ(f.doc.getClip(clip)->name, "Chorus") << "a blank name is refused, not stored";

    f.undo.undo();
    EXPECT_EQ(f.doc.getClip(clip)->name, "Original") << "the refusal pushed no second undo step";
}

TEST(TimelineClipToolTest, RenameContextChoiceIsInert) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "Original");
    ASSERT_TRUE(clip.isValid());
    const auto revisionBefore = f.doc.getRevision();

    // The enum entry exists so the menu's vocabulary is enumerable; the editor it opens is the
    // real path, and renameClip() above is its commit half.
    f.lane.applyClipContextChoice(clip, TimelineClipLaneArea::ClipContextChoice::Rename, 0.0);

    EXPECT_EQ(f.doc.getRevision(), revisionBefore);
    EXPECT_EQ(f.doc.getClip(clip)->name, "Original");
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipToolTest, MuteAndGlueAreReachableFromTheContextMenuHook) {
    ToolLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "T");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 4.0, 4.0, "b");
    ASSERT_TRUE(a.isValid() && b.isValid());

    f.lane.applyClipContextChoice(a, TimelineClipLaneArea::ClipContextChoice::ToggleMute, 0.0);
    EXPECT_TRUE(f.doc.getClip(a)->muted);

    f.lane.applyClipContextChoice(a, TimelineClipLaneArea::ClipContextChoice::GlueWithNext, 0.0);
    ASSERT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(a)->lengthBeats, 8.0);
    EXPECT_TRUE(f.doc.getClip(a)->muted) << "the survivor's mute flag survives with it";
}
