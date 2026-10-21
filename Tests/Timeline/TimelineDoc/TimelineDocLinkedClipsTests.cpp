#include "AppUndoManager.h"
#include "TimelineDocTestHelpers.h"

#include <functional>

// Linked clips: MIDI clips sharing one set of notes and one length (TimelineDocLinkedClips.cpp).

namespace {

struct LinkedPair {
    TrackId track;
    ClipId a;
    ClipId b;
};

LinkedPair makeLinkedPair(TimelineDoc& doc) {
    const auto track = doc.addTrack(TrackKind::Midi, "Drums");
    const auto a = doc.addClip(track, 0.0, 4.0, "Beat");
    doc.addNote(a, makeNote(0.0, 36));
    doc.addNote(a, makeNote(1.0, 38, 0.5, 90));
    const auto b = doc.duplicateClipLinked(a);
    return {track, a, b};
}

// Same notes (ignoring ids, which are unique per clip) and same length.
void expectSameContent(const TimelineDoc& doc, ClipId x, ClipId y) {
    const auto* cx = doc.getClip(x);
    const auto* cy = doc.getClip(y);
    ASSERT_NE(cx, nullptr);
    ASSERT_NE(cy, nullptr);
    EXPECT_EQ(cx->lengthBeats, cy->lengthBeats);
    ASSERT_EQ(cx->notes.size(), cy->notes.size());
    for (size_t i = 0; i < cx->notes.size(); ++i) {
        const auto& n = cx->notes[i];
        const auto& m = cy->notes[i];
        EXPECT_EQ(n.startBeat, m.startBeat);
        EXPECT_EQ(n.lengthBeats, m.lengthBeats);
        EXPECT_EQ(n.pitch, m.pitch);
        EXPECT_EQ(n.velocity, m.velocity);
        EXPECT_EQ(n.channel, m.channel);
        EXPECT_EQ(n.muted, m.muted);
        EXPECT_NE(n.id, m.id) << "note ids stay unique doc-wide";
    }
}

} // namespace

TEST(TimelineDocLinkedClipsTest, DuplicateLinkedSharesAGroupAndLandsAfterTheSource) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    ASSERT_TRUE(pair.b.isValid());
    const auto* a = doc.getClip(pair.a);
    const auto* b = doc.getClip(pair.b);
    EXPECT_NE(a->linkGroup, 0);
    EXPECT_EQ(a->linkGroup, b->linkGroup);
    EXPECT_EQ(b->startBeat, 4.0);
    EXPECT_EQ(b->name, "Beat");
    expectSameContent(doc, pair.a, pair.b);
    EXPECT_EQ(doc.countLinkedClips(pair.a), 1);
    EXPECT_EQ(doc.countLinkedClips(pair.b), 1);

    // A third copy joins the existing group rather than starting a new one.
    const auto c = doc.duplicateClipLinked(pair.b);
    EXPECT_EQ(doc.getClip(c)->linkGroup, doc.getClip(pair.a)->linkGroup);
    EXPECT_EQ(doc.countLinkedClips(pair.a), 2);
}

TEST(TimelineDocLinkedClipsTest, PlainDuplicateStaysIndependent) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto copy = doc.duplicateClip(pair.a);
    EXPECT_EQ(doc.getClip(copy)->linkGroup, 0);
    doc.addNote(copy, makeNote(3.0, 42));
    EXPECT_EQ(doc.getClip(pair.a)->notes.size(), 2u);
}

TEST(TimelineDocLinkedClipsTest, AudioClipsCannotBeLinked) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "Vox");
    const auto clip = doc.addClip(track, 0.0, 4.0, "Take");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take.wav", 0.0));
    const auto revision = doc.getRevision();
    EXPECT_FALSE(doc.duplicateClipLinked(clip).isValid());
    EXPECT_EQ(doc.getRevision(), revision);
}

// Every note mutator, applied to either member, reaches the other member in the same mutation.
TEST(TimelineDocLinkedClipsTest, EveryNoteMutationSyncsTheGroupInOneNotification) {
    const std::vector<std::pair<const char*, std::function<void(TimelineDoc&, ClipId)>>> edits = {
        {"addNote", [](TimelineDoc& d, ClipId c) { d.addNote(c, makeNote(2.0, 42)); }},
        {"clearNotes", [](TimelineDoc& d, ClipId c) { d.clearNotes(c); }},
        {"removeNote", [](TimelineDoc& d, ClipId c) { d.removeNote(d.getClip(c)->notes.front().id); }},
        {"moveNote", [](TimelineDoc& d, ClipId c) { d.moveNote(d.getClip(c)->notes.front().id, 3.0, 40); }},
        {"resizeNote", [](TimelineDoc& d, ClipId c) { d.resizeNote(d.getClip(c)->notes.front().id, 2.5); }},
        {"setNoteVelocity", [](TimelineDoc& d, ClipId c) { d.setNoteVelocity(d.getClip(c)->notes.front().id, 7); }},
        {"setNoteMuted", [](TimelineDoc& d, ClipId c) { d.setNoteMuted(d.getClip(c)->notes.front().id, true); }},
        {"quantiseNotes", [](TimelineDoc& d, ClipId c) { d.quantiseNotes(c, 4.0, 1.0); }},
        {"quantiseNoteLengths", [](TimelineDoc& d, ClipId c) { d.quantiseNoteLengths(c, 2.0); }},
        {"resizeClip", [](TimelineDoc& d, ClipId c) { d.resizeClip(c, 8.0); }},
    };
    for (const auto& [name, edit] : edits) {
        for (const bool editSecond : {false, true}) {
            TimelineDoc doc;
            const auto pair = makeLinkedPair(doc);
            const auto before = dump(doc);
            CountingListener listener;
            doc.addListener(&listener);
            edit(doc, editSecond ? pair.b : pair.a);
            doc.removeListener(&listener);
            EXPECT_NE(dump(doc), before) << name;
            EXPECT_EQ(listener.calls, 1) << name;
            expectSameContent(doc, pair.a, pair.b);
        }
    }
}

TEST(TimelineDocLinkedClipsTest, PositionNameAndMuteStayPerClip) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    ASSERT_TRUE(doc.moveClip(pair.b, 16.0));
    ASSERT_TRUE(doc.setClipName(pair.b, "Fill"));
    ASSERT_TRUE(doc.setClipMuted(pair.b, true));
    const auto* a = doc.getClip(pair.a);
    EXPECT_EQ(a->startBeat, 0.0);
    EXPECT_EQ(a->name, "Beat");
    EXPECT_FALSE(a->muted);
    EXPECT_EQ(doc.countLinkedClips(pair.a), 1);
}

TEST(TimelineDocLinkedClipsTest, LinkedEditIsOneUndoStepThatRestoresEveryMember) {
    TimelineDoc doc;
    AppUndoManager undo;
    const auto pair = makeLinkedPair(doc);
    const auto before = dump(doc);

    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { doc.addNote(pair.b, makeNote(3.0, 46)); }));
    EXPECT_EQ(doc.getClip(pair.a)->notes.size(), 3u);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(dump(doc), before);
    EXPECT_FALSE(undo.canUndo()) << "the whole group's edit was one step";
}

TEST(TimelineDocLinkedClipsTest, MakeUniqueLeavesTheGroupAndDissolvesALoneSurvivor) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto c = doc.duplicateClipLinked(pair.b);

    ASSERT_TRUE(doc.makeClipUnique(pair.a));
    EXPECT_EQ(doc.getClip(pair.a)->linkGroup, 0);
    EXPECT_EQ(doc.getClip(pair.a)->notes.size(), 2u) << "a unique clip keeps its notes";
    EXPECT_EQ(doc.countLinkedClips(pair.b), 1);

    doc.addNote(pair.a, makeNote(3.0, 49));
    EXPECT_EQ(doc.getClip(pair.b)->notes.size(), 2u) << "edits no longer reach the old group";

    ASSERT_TRUE(doc.makeClipUnique(c));
    EXPECT_EQ(doc.getClip(pair.b)->linkGroup, 0) << "a group of one is dissolved";
    EXPECT_FALSE(doc.makeClipUnique(pair.b)) << "an unlinked clip has nothing to leave";
}

TEST(TimelineDocLinkedClipsTest, DeletingAMemberKeepsTheRestLinked) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto c = doc.duplicateClipLinked(pair.b);
    ASSERT_TRUE(doc.removeClip(pair.b));
    EXPECT_EQ(doc.countLinkedClips(pair.a), 1);
    EXPECT_EQ(doc.getClip(c)->linkGroup, doc.getClip(pair.a)->linkGroup);

    ASSERT_TRUE(doc.removeClip(c));
    EXPECT_EQ(doc.getClip(pair.a)->linkGroup, 0);
}

TEST(TimelineDocLinkedClipsTest, SplitMakesBothHalvesUnique) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto c = doc.duplicateClipLinked(pair.b);
    const auto [left, right] = doc.splitClip(pair.a, 2.0);
    ASSERT_TRUE(right.isValid());
    EXPECT_EQ(doc.getClip(left)->linkGroup, 0);
    EXPECT_EQ(doc.getClip(right)->linkGroup, 0);
    EXPECT_EQ(doc.countLinkedClips(pair.b), 1) << "the other members stay linked";
    expectSameContent(doc, pair.b, c);
    EXPECT_EQ(doc.getClip(pair.b)->lengthBeats, 4.0) << "a split does not resize the other members";
}

TEST(TimelineDocLinkedClipsTest, GlueResultIsUnique) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    ASSERT_TRUE(doc.joinClips(pair.a, pair.b));
    EXPECT_EQ(doc.getClip(pair.a)->linkGroup, 0);
    EXPECT_EQ(doc.getClip(pair.a)->lengthBeats, 8.0);
}

TEST(TimelineDocLinkedClipsTest, EdgeTrimsUnlinkTheTrimmedClip) {
    for (int which = 0; which < 3; ++which) {
        TimelineDoc doc;
        const auto pair = makeLinkedPair(doc);
        const auto c = doc.duplicateClipLinked(pair.b);
        if (which == 0)
            ASSERT_TRUE(doc.resizeClipLeft(pair.b, 5.0));
        else if (which == 1)
            ASSERT_TRUE(doc.trimClipStart(pair.b, 5.0, 0.5));
        else
            ASSERT_TRUE(doc.trimClipEnd(pair.b, 6.0));
        EXPECT_EQ(doc.getClip(pair.b)->linkGroup, 0) << which;
        expectSameContent(doc, pair.a, c);
    }
}

TEST(TimelineDocLinkedClipsTest, DuplicatedTrackLinksItsCopiesToEachOtherOnly) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto copyTrack = doc.duplicateTrack(pair.track, "Drums 2", 0xff00ff00, {});
    const auto* copy = doc.getTrack(copyTrack);
    ASSERT_EQ(copy->clips.size(), 2u);
    const auto group = copy->clips[0].linkGroup;
    EXPECT_NE(group, 0);
    EXPECT_EQ(copy->clips[1].linkGroup, group);
    EXPECT_NE(group, doc.getClip(pair.a)->linkGroup);
    EXPECT_EQ(doc.countLinkedClips(pair.a), 1);
}

TEST(TimelineDocLinkedClipsTest, SerialisationWritesTheGroupOnlyWhenLinkedAndRoundTrips) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto track = doc.addTrack(TrackKind::Midi, "Keys");
    doc.addClip(track, 0.0, 4.0, "Solo");

    const auto state = doc.toVar();
    const auto& tracks = *state["tracks"].getArray();
    EXPECT_TRUE(tracks[0]["clips"][0].hasProperty("linkGroup"));
    EXPECT_FALSE(tracks[1]["clips"][0].hasProperty("linkGroup")) << "an unlinked clip omits the key";

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(state));
    EXPECT_EQ(dump(loaded), dump(doc));
    EXPECT_EQ(loaded.countLinkedClips(pair.a), 1);

    // A new group on the loaded doc never collides with the loaded one.
    const auto solo = loaded.getTrack(track)->clips.front().id;
    loaded.duplicateClipLinked(solo);
    EXPECT_NE(loaded.getClip(solo)->linkGroup, loaded.getClip(pair.a)->linkGroup);
}

TEST(TimelineDocLinkedClipsTest, LoadingAGroupOfOneOrABadGroupIsHandled) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "Keys");
    doc.addClip(track, 0.0, 4.0, "Solo");
    auto state = doc.toVar();
    auto clipVar = (*state["tracks"].getArray())[0]["clips"][0];
    clipVar.getDynamicObject()->setProperty("linkGroup", 77);

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(state));
    EXPECT_EQ(loaded.getTracks()[0].clips[0].linkGroup, 0) << "a lone member is unlinked on load";

    clipVar.getDynamicObject()->setProperty("linkGroup", -3);
    EXPECT_FALSE(loaded.fromVar(state));
    clipVar.getDynamicObject()->setProperty("linkGroup", "seven");
    EXPECT_FALSE(loaded.fromVar(state));
}

TEST(TimelineDocLinkedClipsTest, StepsPerBarIsSharedAndOneUndoStep) {
    TimelineDoc doc;
    AppUndoManager undo;
    const auto pair = makeLinkedPair(doc);

    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { EXPECT_TRUE(doc.setClipStepsPerBar(pair.a, 32)); }));
    EXPECT_EQ(doc.getClip(pair.a)->stepsPerBar, 32);
    EXPECT_EQ(doc.getClip(pair.b)->stepsPerBar, 32);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(doc.getClip(pair.a)->stepsPerBar, Clip::kDefaultStepsPerBar);
    EXPECT_EQ(doc.getClip(pair.b)->stepsPerBar, Clip::kDefaultStepsPerBar);
    EXPECT_FALSE(undo.canUndo()) << "the whole group's edit was one step";
}

TEST(TimelineDocLinkedClipsTest, DrumMuteAndSoloAreSharedAndEachOneUndoStep) {
    TimelineDoc doc;
    AppUndoManager undo;
    const auto pair = makeLinkedPair(doc);

    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { EXPECT_TRUE(doc.setClipPitchMuted(pair.b, 36, true)); }));
    EXPECT_EQ(doc.getClip(pair.a)->mutedPitches, std::vector<int>{36});
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { EXPECT_TRUE(doc.setClipPitchSoloed(pair.a, 38, true)); }));
    EXPECT_EQ(doc.getClip(pair.b)->soloPitches, std::vector<int>{38});

    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(doc.getClip(pair.a)->soloPitches.empty());
    EXPECT_TRUE(doc.getClip(pair.b)->soloPitches.empty());
    EXPECT_EQ(doc.getClip(pair.a)->mutedPitches, std::vector<int>{36});
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(doc.getClip(pair.a)->mutedPitches.empty());
    EXPECT_TRUE(doc.getClip(pair.b)->mutedPitches.empty());
    EXPECT_FALSE(undo.canUndo());
}

TEST(TimelineDocLinkedClipsTest, DrumSettingsOfAnUnlinkedClipStayItsOwn) {
    TimelineDoc doc;
    const auto pair = makeLinkedPair(doc);
    const auto other = doc.addClip(pair.track, 16.0, 4.0, "Solo");
    ASSERT_TRUE(doc.setClipStepsPerBar(other, 64));
    ASSERT_TRUE(doc.setClipPitchMuted(other, 36, true));
    EXPECT_EQ(doc.getClip(pair.a)->stepsPerBar, Clip::kDefaultStepsPerBar);
    EXPECT_TRUE(doc.getClip(pair.a)->mutedPitches.empty());
}
