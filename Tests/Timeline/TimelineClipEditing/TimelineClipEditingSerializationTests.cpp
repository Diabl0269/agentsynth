// TimelineClipEditing round-trip tests: split/join/duplicate survives toVar/fromVar, and fromVar rejects duplicate or
// missing note ids.

#include "TimelineClipEditingTestHelpers.h"

#include <algorithm>
#include <vector>

// ---------------------------------------------------------------- round trip --

TEST_F(TimelineClipEditingTest, RoundTripSurvivesSplitJoinAndDuplicate) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    doc.addNote(clip, makeNote(1.0, 60));
    doc.addNote(clip, makeNote(5.0, 64));

    const auto splitResult = doc.splitClip(clip, 4.0);
    const auto leftId = splitResult.first;
    ASSERT_TRUE(leftId.isValid());
    ASSERT_TRUE(splitResult.second.isValid());

    const auto dup = doc.duplicateClip(leftId);
    ASSERT_TRUE(dup.isValid());

    const auto otherTrack = doc.addTrack(TrackKind::Midi, "T2");
    const auto a = doc.addClip(otherTrack, 0.0, 2.0, "a");
    const auto b = doc.addClip(otherTrack, 2.0, 2.0, "b");
    doc.addNote(a, makeNote(0.5, 50));
    doc.addNote(b, makeNote(0.5, 55));
    ASSERT_TRUE(doc.joinClips(a, b));

    const auto before = juce::JSON::toString(doc.toVar());

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    EXPECT_EQ(juce::JSON::toString(loaded.toVar()), before);

    // Id counters floor correctly: a note added after load doesn't collide with anything that
    // came off the file.
    std::vector<NoteId> allNoteIds;
    for (const auto& t : loaded.getTracks())
        for (const auto& c : t.clips)
            for (const auto& n : c.notes)
                allNoteIds.push_back(n.id);

    const auto newNote = loaded.addNote(leftId, makeNote(7.0, 30));
    ASSERT_TRUE(newNote.isValid());
    EXPECT_EQ(std::count(allNoteIds.begin(), allNoteIds.end(), newNote), 0);

    const auto newClip = loaded.addClip(track, 100.0, 1.0, "after load");
    ASSERT_TRUE(newClip.isValid());
    EXPECT_GT(newClip.value, dup.value);
}

TEST_F(TimelineClipEditingTest, FromVarRejectsDuplicateNoteIdsWithinAClip) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":4.0,
        "notes":[{"id":5,"startBeat":0.0,"lengthBeats":1.0,"pitch":60,"velocity":100,"channel":1},
                 {"id":5,"startBeat":1.0,"lengthBeats":1.0,"pitch":61,"velocity":100,"channel":1}]}]}]})";
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(text)));
    EXPECT_TRUE(doc.isEmpty());
}

TEST_F(TimelineClipEditingTest, FromVarRejectsDuplicateNoteIdsAcrossClips) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"clips":[
        {"id":1,"startBeat":0.0,"lengthBeats":4.0,
         "notes":[{"id":7,"startBeat":0.0,"lengthBeats":1.0,"pitch":60,"velocity":100,"channel":1}]},
        {"id":2,"startBeat":4.0,"lengthBeats":4.0,
         "notes":[{"id":7,"startBeat":0.0,"lengthBeats":1.0,"pitch":61,"velocity":100,"channel":1}]}
    ]}]})";
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(text)));
    EXPECT_TRUE(doc.isEmpty());
}

TEST_F(TimelineClipEditingTest, FromVarRejectsMissingNoteId) {
    const auto* text = R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":4.0,
        "notes":[{"startBeat":0.0,"lengthBeats":1.0,"pitch":60,"velocity":100,"channel":1}]}]}]})";
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(text)));
    EXPECT_TRUE(doc.isEmpty());
}
