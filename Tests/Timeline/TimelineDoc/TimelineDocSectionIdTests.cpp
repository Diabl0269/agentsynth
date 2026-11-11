// A moved section's clips stay the same clips (their ids and their notes' ids survive the move, single and group,
// make-room and cover), a copy gets new ids, undo and redo of a copy bring back the same ids, ids stay unique, and a
// section edit tells listeners which notifications are its intermediate steps and which one finishes it.

#include "AppUndoManager.h"
#include "TimelineDocSectionTestHelpers.h"

#include <set>

namespace {

// Every clip and note id in the doc appears once.
bool idsUnique(const TimelineDoc& doc) {
    std::set<std::int64_t> clips, notes;
    for (const auto& track : doc.getTracks())
        for (const auto& clip : track.clips) {
            if (!clips.insert(clip.id.value).second)
                return false;
            for (const auto& note : clip.notes)
                if (!notes.insert(note.id.value).second)
                    return false;
        }
    return true;
}

std::vector<std::int64_t> noteIds(const TimelineDoc& doc, ClipId id) {
    std::vector<std::int64_t> out;
    if (const auto* clip = doc.getClip(id))
        for (const auto& note : clip->notes)
            out.push_back(note.id.value);
    return out;
}

// Records, at every notification, whether a section edit was between its steps and the edit revision.
struct StepRecorder : TimelineDoc::Listener {
    std::vector<std::pair<bool, std::int64_t>> steps;
    void timelineChanged(const TimelineDoc& d) override {
        steps.emplace_back(d.isSectionEditUnfinished(), d.getSectionEditRevision());
    }
};

// Four sections on one track, each one clip with a note: A 0..8, B 8..12, C 12..20, D 20..24.
std::vector<PlacementId> buildFour(TimelineDoc& doc, TrackId track, std::vector<ClipId>& clips) {
    const std::vector<std::pair<double, double>> spans{{0.0, 8.0}, {8.0, 12.0}, {12.0, 20.0}, {20.0, 24.0}};
    for (const auto& [start, end] : spans) {
        clips.push_back(doc.addClip(track, start, 2.0, "c"));
        doc.addNote(clips.back(), makeNote(0.5, 60));
        doc.makeSection(start, end, "S", 0, kSpb);
    }
    std::vector<PlacementId> ids;
    for (const auto& placement : doc.getPlacements())
        ids.push_back(placement.id);
    return ids;
}

} // namespace

TEST_F(TimelineDocTest, AMovedPlacementsClipsKeepTheirIdsAndNotesInMakeRoomAndCover) {
    for (const auto mode : {SectionDropMode::MakeRoom, SectionDropMode::Cover}) {
        TimelineDoc d;
        const auto s = buildSectionSong(d);
        d.makeSection(0.0, 8.0, "Verse", 0, kSpb);
        const auto verse = d.getPlacements().front().id;
        const auto c0Notes = noteIds(d, s.c0);
        const auto bassNotes = noteIds(d, s.bass);
        const double target = mode == SectionDropMode::MakeRoom ? 4.0 : 16.0;
        ASSERT_TRUE(d.movePlacement(verse, target, mode, kSpb));
        ASSERT_NE(d.getClip(s.c0), nullptr) << "the moved clip is the same clip";
        ASSERT_NE(d.getClip(s.c1), nullptr);
        ASSERT_NE(d.getClip(s.bass), nullptr);
        EXPECT_DOUBLE_EQ(d.getClip(s.c0)->startBeat, target);
        EXPECT_DOUBLE_EQ(d.getClip(s.c1)->startBeat, target + 4.0);
        EXPECT_EQ(noteIds(d, s.c0), c0Notes) << "and its notes are the same notes";
        EXPECT_EQ(noteIds(d, s.bass), bassNotes);
        EXPECT_TRUE(idsUnique(d));
    }
}

TEST_F(TimelineDocTest, AGroupMoveKeepsEveryMembersClipIdsInMakeRoomAndCover) {
    for (const auto mode : {SectionDropMode::MakeRoom, SectionDropMode::Cover}) {
        TimelineDoc d;
        const auto track = d.addTrack(TrackKind::Midi, "T");
        std::vector<ClipId> clips;
        const auto ids = buildFour(d, track, clips);
        const auto aNotes = noteIds(d, clips[0]);
        ASSERT_TRUE(
            d.movePlacementsTogether({ids[2], ids[0]}, mode == SectionDropMode::MakeRoom ? 8.0 : 24.0, mode, kSpb));
        for (const auto id : clips)
            EXPECT_NE(d.getClip(id), nullptr) << "clip " << id.value << " is still the same clip";
        EXPECT_DOUBLE_EQ(d.getClip(clips[0])->startBeat, d.getPlacement(ids[0])->startBeat);
        EXPECT_DOUBLE_EQ(d.getClip(clips[2])->startBeat, d.getPlacement(ids[2])->startBeat);
        EXPECT_EQ(noteIds(d, clips[0]), aNotes);
        EXPECT_TRUE(idsUnique(d));
    }
}

TEST_F(TimelineDocTest, ACopyGetsNewIdsAndUndoAndRedoOfItBringBackTheSameOnes) {
    const auto s = buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb);
    AppUndoManager undo;
    PlacementId placed;
    undo.recordTimelineChange(doc, [&] { placed = doc.placeSection(verse, 12.0, SectionDropMode::MakeRoom, kSpb); });
    ASSERT_TRUE(placed.isValid());
    std::vector<std::int64_t> copies;
    for (const auto* clip : clipsOn(doc, s.a, 12.0, 20.0))
        copies.push_back(clip->id.value);
    ASSERT_EQ(copies.size(), 2u);
    for (const auto id : copies)
        EXPECT_TRUE(id != s.c0.value && id != s.c1.value) << "a copy is a new clip";
    EXPECT_NE(doc.getClip(s.c0), nullptr) << "the original stays";
    EXPECT_TRUE(idsUnique(doc));

    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(clipsOn(doc, s.a, 12.0, 20.0).empty()) << "the copy is gone";
    ASSERT_TRUE(undo.redo());
    std::vector<std::int64_t> again;
    for (const auto* clip : clipsOn(doc, s.a, 12.0, 20.0))
        again.push_back(clip->id.value);
    EXPECT_EQ(again, copies) << "the redo re-creates the same ids";
    EXPECT_TRUE(idsUnique(doc));
}

TEST_F(TimelineDocTest, ASectionEditMarksItsStepsAndItsLastStepBumpsTheEditRevisionOnce) {
    buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb);
    EXPECT_EQ(doc.getSectionEditRevision(), 0) << "making a section is not a section edit";
    const auto placement = doc.getPlacements().front().id;
    StepRecorder steps;
    doc.addListener(&steps);
    ASSERT_TRUE(doc.movePlacement(placement, 4.0, SectionDropMode::MakeRoom, kSpb));
    doc.removeListener(&steps);
    ASSERT_GE(steps.steps.size(), 2u) << "a make-room move is several steps";
    EXPECT_EQ(doc.getSectionEditRevision(), 1);
    bool finished = false;
    for (const auto& [unfinished, revision] : steps.steps) {
        if (!finished && revision == 0)
            EXPECT_TRUE(unfinished) << "every step before the last is marked";
        if (revision == 1)
            finished = true;
    }
    EXPECT_TRUE(finished);
    EXPECT_FALSE(doc.isSectionEditUnfinished());

    doc.placeSection(verse, 20.0, SectionDropMode::Cover, kSpb);
    EXPECT_EQ(doc.getSectionEditRevision(), 2);
    doc.deletePlacementAndBars(doc.getPlacements().back().id, kSpb);
    EXPECT_EQ(doc.getSectionEditRevision(), 3);
    doc.insertTime(0.0, 4.0, kSpb);
    doc.deleteTime(0.0, 4.0, kSpb);
    EXPECT_EQ(doc.getSectionEditRevision(), 3) << "plain time edits are not section edits";
    EXPECT_FALSE(doc.isSectionEditUnfinished());
}
