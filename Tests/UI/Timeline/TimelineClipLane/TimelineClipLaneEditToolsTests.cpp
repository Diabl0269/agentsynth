// TimelineClipLaneEditToolsTests.cpp — split/duplicate/delete via the clip context-menu hook.
#include "TimelineClipLaneTestFixture.h"

#include "Music/SongKey.h"

TEST(TimelineClipLaneInteractionTest, SplitAtPointerViaMenuHook) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
    ASSERT_TRUE(clipId.isValid());

    // Strictly inside, Beat-snapped: 3.3 -> 3.0.
    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::SplitAtPointer, 3.3);

    const auto* track = f.doc.getTrackForClip(clipId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clipId)->lengthBeats, 1.0);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u);
}

TEST(TimelineClipLaneInteractionTest, SplitAtPointerRejectsOutsideTheClip) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip"); // [2, 6)
    ASSERT_TRUE(clipId.isValid());

    // 2.0 snaps to the clip's own start — not strictly inside.
    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::SplitAtPointer, 2.0);
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneInteractionTest, DuplicateViaMenuHook) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    f.selection.setSelection({clipId});

    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::Duplicate, 0.0);

    const auto* track = f.doc.getTrackForClip(clipId);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    const auto duplicateId = track->clips[1].id;
    EXPECT_NE(duplicateId, clipId);
    EXPECT_DOUBLE_EQ(track->clips[1].startBeat, 6.0) << "appended immediately after the original";
    EXPECT_TRUE(f.selection.contains(duplicateId)) << "the new clip becomes the selection";
    EXPECT_FALSE(f.selection.contains(clipId));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrackForClip(clipId)->clips.size(), 1u);
}

TEST(TimelineClipLaneInteractionTest, DeleteViaMenuHook) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 2.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());

    f.lane.applyClipContextChoice(clipId, TimelineClipLaneArea::ClipContextChoice::Delete, 0.0);
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 0u);
    ASSERT_TRUE(f.undo.canUndo());
}

namespace {

// A clip lane wired to a real transport, with one MIDI clip holding C, C# and D.
struct FitToKeyMenuFixture : ClipLaneFixture {
    synth::TransportService transport;
    ClipId clip;

    FitToKeyMenuFixture() {
        lane.setTransport(&transport);
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 0.0, 8.0, "Clip");
        doc.addNote(clip, makeNote(0.0, 60));
        doc.addNote(clip, makeNote(1.0, 61));
        doc.addNote(clip, makeNote(2.0, 62));
    }

    void setSongKey(const char* scale, int root = 0) {
        transport.setSongKey(synth::makeScale(root, synth::scalePresetIndex(scale)));
    }

    // Whether the menu has a "Fit to key" item, and whether it is enabled.
    std::pair<bool, bool> fitItem() {
        auto menu = lane.buildClipContextMenu(clip, {0, 0});
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            if (it.getItem().text == "Fit to key")
                return {true, it.getItem().isEnabled};
        return {false, false};
    }

    int pitchAt(double startBeat) const {
        for (const auto& note : doc.getClip(clip)->notes)
            if (std::abs(note.startBeat - startBeat) < 1.0e-6)
                return note.pitch;
        return -1;
    }
};

} // namespace

TEST(TimelineClipLaneInteractionTest, FitToKeyIsOfferedForAMidiClipWithNotesAndAKey) {
    FitToKeyMenuFixture f;
    f.setSongKey("Major");
    const auto [present, enabled] = f.fitItem();
    EXPECT_TRUE(present);
    EXPECT_TRUE(enabled);
}

TEST(TimelineClipLaneInteractionTest, FitToKeyIsGreyedWithNoKeyAndForAnEmptyClip) {
    FitToKeyMenuFixture f;
    auto [present, enabled] = f.fitItem();
    EXPECT_TRUE(present) << "greyed, not hidden";
    EXPECT_FALSE(enabled) << "no song key and no own key";

    f.setSongKey("Major");
    const auto track = f.doc.addTrack(TrackKind::Midi, "Track 2");
    f.clip = f.doc.addClip(track, 0.0, 4.0, "Empty");
    std::tie(present, enabled) = f.fitItem();
    EXPECT_TRUE(present);
    EXPECT_FALSE(enabled) << "nothing to fit";
}

TEST(TimelineClipLaneInteractionTest, FitToKeyMovesNotesAsOneUndoStepAndKeyChangesMoveNothing) {
    FitToKeyMenuFixture f;
    f.setSongKey("Major");
    f.lane.applyClipContextChoice(f.clip, TimelineClipLaneArea::ClipContextChoice::FitToKey, 0.0);
    EXPECT_EQ(f.pitchAt(0.0), 60);
    EXPECT_EQ(f.pitchAt(1.0), 60) << "C# goes down to C";
    EXPECT_EQ(f.pitchAt(2.0), 62);

    f.setSongKey("Major", 5);
    EXPECT_EQ(f.pitchAt(1.0), 60) << "setting a key never moves notes";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.pitchAt(1.0), 61) << "one undo restores the whole fit";
    EXPECT_EQ(f.pitchAt(0.0), 60);
}

TEST(TimelineClipLaneInteractionTest, FitToKeyPushesNoUndoStepWhenEverythingIsInKey) {
    FitToKeyMenuFixture f;
    f.setSongKey("Major");
    ASSERT_TRUE(f.doc.removeNote(f.doc.getClip(f.clip)->notes[1].id)); // the C#: C and D are already in key
    f.lane.applyClipContextChoice(f.clip, TimelineClipLaneArea::ClipContextChoice::FitToKey, 0.0);
    EXPECT_EQ(f.pitchAt(2.0), 62);
    EXPECT_FALSE(f.undo.canUndo());
}
