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

namespace {

// Three MIDI clips of one off-key note (C#) each, a selection over them, and the real menu a right-click on one shows.
struct FitAllToKeyFixture : FitToKeyMenuFixture {
    ClipId second, third;

    FitAllToKeyFixture() {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 2");
        second = doc.addClip(track, 0.0, 8.0, "Second");
        doc.addNote(second, makeNote(0.0, 61));
        third = doc.addClip(track, 8.0, 8.0, "Third");
        doc.addNote(third, makeNote(0.0, 66)); // F#: between F and G, a tie that goes down
        selection.setSelection({clip, second, third});
    }

    int firstPitch(ClipId id) const { return doc.getClip(id)->notes.front().pitch; }

    // The "Fit all clips to key" item of the menu for a right-click on `on`: {present, enabled}; runs it when asked.
    std::pair<bool, bool> fitAllItem(ClipId on, bool press = false) {
        auto menu = lane.buildClipContextMenu(on, {0, 0});
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next()) {
            if (it.getItem().text != "Fit all clips to key")
                continue;
            if (press && it.getItem().action)
                it.getItem().action();
            return {true, it.getItem().isEnabled};
        }
        return {false, false};
    }
};

} // namespace

// Regression test for FRO1236: right-clicking one of several selected clips fits them all, in one undo step.
TEST(TimelineClipLaneInteractionTest, FitAllClipsToKeyFitsEverySelectedClipAsOneUndoStep) {
    FitAllToKeyFixture f;
    f.setSongKey("Major");
    const auto [present, enabled] = f.fitAllItem(f.second, true);
    EXPECT_TRUE(present);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(f.firstPitch(f.clip), 60);
    EXPECT_EQ(f.pitchAt(1.0), 60);
    EXPECT_EQ(f.firstPitch(f.second), 60);
    EXPECT_EQ(f.firstPitch(f.third), 65);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.pitchAt(1.0), 61);
    EXPECT_EQ(f.firstPitch(f.second), 61);
    EXPECT_EQ(f.firstPitch(f.third), 66);
    f.undo.undo(); // nothing else was recorded
    EXPECT_EQ(f.firstPitch(f.second), 61);
}

TEST(TimelineClipLaneInteractionTest, FitAllClipsToKeyIsOnlyOfferedForASelectionOfSeveralClips) {
    FitAllToKeyFixture f;
    f.setSongKey("Major");
    f.selection.setSelection({f.clip});
    EXPECT_FALSE(f.fitAllItem(f.clip).first) << "one selected clip: Fit to key covers it";
    f.selection.setSelection({f.clip, f.second});
    EXPECT_FALSE(f.fitAllItem(f.third).first) << "the clicked clip is outside the selection";
    EXPECT_TRUE(f.fitAllItem(f.second).first);
}

TEST(TimelineClipLaneInteractionTest, FitAllClipsToKeySkipsAudioAndClipsWithNoKey) {
    FitAllToKeyFixture f;
    f.setSongKey("Major");
    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "Audio");
    const auto audio = f.doc.addClip(audioTrack, 0.0, 8.0, "Audio");
    ASSERT_TRUE(f.doc.setClipAsset(audio, "Audio/take.wav", 0.0));
    f.doc.setClipScale(f.third, synth::makeScale(1, synth::scalePresetIndex("Major")), false); // C# major: F# is in key
    f.selection.setSelection({f.clip, f.second, f.third, audio});
    f.fitAllItem(f.clip, true);
    EXPECT_EQ(f.firstPitch(f.second), 60);
    EXPECT_EQ(f.firstPitch(f.third), 66) << "fitted to its own key, where F# already is";
    EXPECT_EQ(f.doc.getClip(audio)->assetRef, "Audio/take.wav");
}

TEST(TimelineClipLaneInteractionTest, FitAllClipsToKeyIsGreyedWhenNoSelectedClipCanFit) {
    FitAllToKeyFixture f;
    auto [present, enabled] = f.fitAllItem(f.clip);
    EXPECT_TRUE(present) << "greyed, not hidden";
    EXPECT_FALSE(enabled) << "no song key and no own key";
    f.fitAllItem(f.clip, true);
    EXPECT_FALSE(f.undo.canUndo());

    // Everything already in key: the item is offered, and pressing it records nothing.
    f.setSongKey("Major");
    f.doc.moveNote(f.doc.getClip(f.clip)->notes[1].id, 1.0, 62);
    f.doc.moveNote(f.doc.getClip(f.second)->notes[0].id, 0.0, 62);
    f.doc.moveNote(f.doc.getClip(f.third)->notes[0].id, 0.0, 65);
    EXPECT_TRUE(f.fitAllItem(f.clip, true).second);
    EXPECT_FALSE(f.undo.canUndo());
}

namespace {

// A lane that builds the menu a right-click asks for instead of showing it (a real juce::PopupMenu needs a display),
// with two MIDI clips on two tracks, each holding an off-key C#, under a song key of C major.
class FitMenuLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    std::vector<juce::PopupMenu::Item> clipMenu;

protected:
    void showClipContextMenu(synth::ClipId id, juce::Point<int> pos) override {
        clipMenu.clear();
        auto menu = buildClipContextMenu(id, pos);
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            clipMenu.push_back(it.getItem());
    }
};

struct RealRightClickFixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    synth::TransportService transport;
    FitMenuLane lane{state, selection};
    ClipId first, second;

    RealRightClickFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setTransport(&transport);
        lane.setSize(1200, 400);
        transport.setSongKey(synth::makeScale(0, synth::scalePresetIndex("Major")));
        first = addClip("Track 1", "First");
        second = addClip("Track 2", "Second");
    }

    ClipId addClip(const char* trackName, const char* clipName) {
        const auto clip = doc.addClip(doc.addTrack(TrackKind::Midi, trackName), 0.0, 8.0, clipName);
        doc.addNote(clip, makeNote(0.0, 61));
        return clip;
    }

    juce::Point<float> centre(ClipId id) { return centreOf(lane.getClipRect(id)); }

    void click(ClipId id, int extraFlags = 0) {
        const auto event = leftClick(lane, centre(id), extraFlags);
        lane.mouseDown(event);
        lane.mouseUp(event);
    }

    const juce::PopupMenu::Item* fitAllItem() const {
        for (const auto& item : lane.clipMenu)
            if (item.text == "Fit all clips to key")
                return &item;
        return nullptr;
    }

    int pitch(ClipId id) const { return doc.getClip(id)->notes.front().pitch; }
};

} // namespace

// Regression test for FRO1236: the item must be reachable by a real right-click.
TEST(TimelineClipLaneInteractionTest, FitAllClipsToKeyIsReachableByARealRightClickOnASelectedClip) {
    RealRightClickFixture f;
    f.click(f.first);
    f.click(f.second, juce::ModifierKeys::commandModifier);
    ASSERT_EQ(f.selection.size(), 2);

    f.lane.mouseDown(rightClick(f.lane, f.centre(f.second)));
    EXPECT_EQ(f.selection.size(), 2) << "a right-click inside the selection keeps it";
    const auto* item = f.fitAllItem();
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);

    item->action();
    EXPECT_EQ(f.pitch(f.first), 60);
    EXPECT_EQ(f.pitch(f.second), 60);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.pitch(f.first), 61);
    EXPECT_EQ(f.pitch(f.second), 61);
}

TEST(TimelineClipLaneInteractionTest, RealRightClickOnAnUnselectedClipSelectsItAndOffersNoFitAll) {
    RealRightClickFixture f;
    const auto third = f.addClip("Track 3", "Third");
    f.click(f.first);
    f.click(f.second, juce::ModifierKeys::commandModifier);

    f.lane.mouseDown(rightClick(f.lane, f.centre(third)));
    ASSERT_EQ(f.selection.size(), 1);
    EXPECT_TRUE(f.selection.contains(third));
    EXPECT_EQ(f.fitAllItem(), nullptr);
}
