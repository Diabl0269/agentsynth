// TimelineDoc range-edit tests: the tempo-aware split, left/right trims, splitting a track span at
// both range edges, deleting a range (with and without closing the gap) and the detached clipped
// copy a range copy captures.

#include "TimelineDocTestHelpers.h"

namespace {
constexpr double kSecondsPerBeat = 0.5; // 120 bpm
} // namespace

TEST_F(TimelineDocTest, SplitClipAtTempoAdvancesAudioRightHalf) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 4.0, 8.0, "take");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take.wav", 1.0));

    const auto [left, right] = doc.splitClipAtTempo(clip, 2.0, kSecondsPerBeat);
    ASSERT_TRUE(right.isValid());
    EXPECT_EQ(left, clip);
    EXPECT_DOUBLE_EQ(doc.getClip(left)->sourceStartSeconds, 1.0);
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceStartSeconds, 2.0); // 1.0 + 2 beats * 0.5 s
    EXPECT_DOUBLE_EQ(doc.getClip(right)->startBeat, 6.0);
}

TEST_F(TimelineDocTest, PlainSplitClipStillKeepsSourceOffset) {
    // splitClip's documented contract is unchanged by the tempo-aware variant.
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 8.0, "take");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take.wav", 1.0));
    const auto right = doc.splitClip(clip, 2.0).second;
    ASSERT_TRUE(right.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceStartSeconds, 1.0);
}

TEST_F(TimelineDocTest, SplitClipAtTempoRejectsBadTempo) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    EXPECT_FALSE(doc.splitClipAtTempo(clip, 2.0, 0.0).second.isValid());
    EXPECT_FALSE(doc.splitClipAtTempo(clip, 2.0, std::numeric_limits<double>::quiet_NaN()).second.isValid());
    EXPECT_EQ(doc.getTrack(track)->clips.size(), 1u);
}

TEST_F(TimelineDocTest, SplitClipAtTempoLeavesMidiClipSourceUntouched) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto right = doc.splitClipAtTempo(clip, 1.0, kSecondsPerBeat).second;
    ASSERT_TRUE(right.isValid());
    EXPECT_DOUBLE_EQ(doc.getClip(right)->sourceStartSeconds, 0.0);
}

TEST_F(TimelineDocTest, TrimClipStartCutsNotesAndAdvancesAudio) {
    const auto midiTrack = doc.addTrack(TrackKind::Midi, "M");
    const auto midi = doc.addClip(midiTrack, 4.0, 8.0, "m");
    const auto before = doc.addNote(midi, makeNote(0.5, 60, 1.0));   // ends at 1.5: dropped
    const auto straddle = doc.addNote(midi, makeNote(1.0, 62, 2.0)); // 1..3: cut at 2
    doc.addNote(midi, makeNote(4.0, 64, 1.0));                       // kept, re-based to 2
    ASSERT_TRUE(doc.setClipFades(midi, 1.0, 1.0));

    ASSERT_TRUE(doc.trimClipStart(midi, 6.0, kSecondsPerBeat));
    const auto* clip = doc.getClip(midi);
    ASSERT_NE(clip, nullptr);
    EXPECT_DOUBLE_EQ(clip->startBeat, 6.0);
    EXPECT_DOUBLE_EQ(clip->lengthBeats, 6.0); // the end stays at 12
    EXPECT_DOUBLE_EQ(clip->fadeInBeats, 0.0); // the new edge is a cut
    EXPECT_DOUBLE_EQ(clip->fadeOutBeats, 1.0);
    ASSERT_EQ(clip->notes.size(), 2u);
    EXPECT_EQ(doc.getNote(before), nullptr);
    EXPECT_EQ(clip->notes[0].id, straddle); // the survivor keeps its id
    EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(clip->notes[0].lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(clip->notes[1].startBeat, 2.0);

    const auto audioTrack = doc.addTrack(TrackKind::Audio, "A");
    const auto audio = doc.addClip(audioTrack, 0.0, 8.0, "a");
    ASSERT_TRUE(doc.setClipAsset(audio, "Audio/a.wav", 0.25));
    ASSERT_TRUE(doc.trimClipStart(audio, 3.0, kSecondsPerBeat));
    EXPECT_DOUBLE_EQ(doc.getClip(audio)->sourceStartSeconds, 1.75); // 0.25 + 3 * 0.5
}

TEST_F(TimelineDocTest, TrimClipStartRejectsOutsideAndResorts) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 0.0, 10.0, "a");
    const auto b = doc.addClip(track, 5.0, 1.0, "b");
    EXPECT_FALSE(doc.trimClipStart(a, 0.0, kSecondsPerBeat));
    EXPECT_FALSE(doc.trimClipStart(a, 10.0, kSecondsPerBeat));
    EXPECT_FALSE(doc.trimClipStart(a, 4.0, -1.0));

    ASSERT_TRUE(doc.trimClipStart(a, 7.0, kSecondsPerBeat));
    const auto& clips = doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_EQ(clips[0].id, b); // a now starts later than b
    EXPECT_EQ(clips[1].id, a);
}

TEST_F(TimelineDocTest, TrimClipEndCutsNotesPastTheEdge) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    const auto keep = doc.addNote(clip, makeNote(0.0, 60, 1.0));
    const auto straddle = doc.addNote(clip, makeNote(3.0, 62, 2.0));
    const auto after = doc.addNote(clip, makeNote(5.0, 64, 1.0));
    ASSERT_TRUE(doc.setClipFades(clip, 1.0, 1.0));

    ASSERT_TRUE(doc.trimClipEnd(clip, 4.0));
    const auto* c = doc.getClip(clip);
    EXPECT_DOUBLE_EQ(c->lengthBeats, 4.0);
    EXPECT_DOUBLE_EQ(c->fadeInBeats, 1.0);
    EXPECT_DOUBLE_EQ(c->fadeOutBeats, 0.0);
    ASSERT_EQ(c->notes.size(), 2u);
    EXPECT_EQ(c->notes[0].id, keep);
    EXPECT_EQ(c->notes[1].id, straddle);
    EXPECT_DOUBLE_EQ(c->notes[1].lengthBeats, 1.0);
    EXPECT_EQ(doc.getNote(after), nullptr);

    EXPECT_FALSE(doc.trimClipEnd(clip, 4.0)); // no longer strictly inside
    EXPECT_FALSE(doc.trimClipEnd(clip, 0.0));
}

TEST_F(TimelineDocTest, SplitClipsAtRangeEdgesSplitsOnlyTheNamedTracks) {
    const auto t1 = doc.addTrack(TrackKind::Midi, "1");
    const auto t2 = doc.addTrack(TrackKind::Midi, "2");
    const auto spanning = doc.addClip(t1, 0.0, 16.0, "long");   // crosses both edges
    const auto leftOnly = doc.addClip(t2, 2.0, 4.0, "left");    // crosses only the left edge
    const auto untouched = doc.addClip(t2, 10.0, 1.0, "inner"); // already inside
    const auto other = doc.addTrack(TrackKind::Midi, "other");
    const auto elsewhere = doc.addClip(other, 0.0, 16.0, "elsewhere");

    const auto inside = doc.splitClipsAtRangeEdges({t1, t2}, 4.0, 12.0, kSecondsPerBeat);
    EXPECT_EQ(doc.getTrack(t1)->clips.size(), 3u);
    EXPECT_EQ(doc.getTrack(t2)->clips.size(), 3u);
    EXPECT_EQ(doc.getTrack(other)->clips.size(), 1u);
    EXPECT_DOUBLE_EQ(doc.getClip(elsewhere)->lengthBeats, 16.0);

    EXPECT_DOUBLE_EQ(doc.getClip(spanning)->lengthBeats, 4.0); // the original id keeps the left piece
    EXPECT_DOUBLE_EQ(doc.getClip(leftOnly)->lengthBeats, 2.0);
    ASSERT_EQ(inside.size(), 3u);
    for (auto id : inside) {
        const auto* c = doc.getClip(id);
        ASSERT_NE(c, nullptr);
        EXPECT_GE(c->startBeat, 4.0);
        EXPECT_LE(c->startBeat + c->lengthBeats, 12.0);
    }
    EXPECT_NE(std::find(inside.begin(), inside.end(), untouched), inside.end());
}

TEST_F(TimelineDocTest, DeleteRangeRemovesTrimsAndSplits) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto spanning = doc.addClip(track, 0.0, 16.0, "long");
    doc.addNote(spanning, makeNote(5.0, 60, 1.0)); // inside the range: must disappear
    doc.addNote(spanning, makeNote(13.0, 62, 1.0));

    ASSERT_TRUE(doc.deleteRange({track}, 4.0, 12.0, false, kSecondsPerBeat));
    const auto& clips = doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_EQ(clips[0].id, spanning);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 4.0);
    EXPECT_TRUE(clips[0].notes.empty());
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 12.0);
    EXPECT_DOUBLE_EQ(clips[1].lengthBeats, 4.0);
    ASSERT_EQ(clips[1].notes.size(), 1u);
    EXPECT_DOUBLE_EQ(clips[1].notes[0].startBeat, 1.0);
}

TEST_F(TimelineDocTest, DeleteRangeAdvancesAudioOnTheRightPiece) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto whole = doc.addClip(track, 0.0, 8.0, "a");
    const auto tail = doc.addClip(track, 10.0, 4.0, "b");
    ASSERT_TRUE(doc.setClipAsset(whole, "Audio/a.wav", 0.0));
    ASSERT_TRUE(doc.setClipAsset(tail, "Audio/b.wav", 0.0));

    ASSERT_TRUE(doc.deleteRange({track}, 2.0, 12.0, false, kSecondsPerBeat));
    const auto& clips = doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_DOUBLE_EQ(clips[0].lengthBeats, 2.0);
    EXPECT_EQ(clips[1].id, tail);
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 12.0);
    EXPECT_DOUBLE_EQ(clips[1].sourceStartSeconds, 1.0); // 2 beats trimmed * 0.5 s
}

TEST_F(TimelineDocTest, DeleteRangeClosingTheGapPullsLaterClipsLeft) {
    const auto t1 = doc.addTrack(TrackKind::Midi, "1");
    const auto t2 = doc.addTrack(TrackKind::Midi, "2");
    const auto before = doc.addClip(t1, 0.0, 2.0, "before");
    const auto inside = doc.addClip(t1, 4.0, 2.0, "inside");
    const auto later = doc.addClip(t1, 10.0, 2.0, "later");
    const auto otherTrack = doc.addClip(t2, 10.0, 2.0, "other");

    ASSERT_TRUE(doc.deleteRange({t1}, 4.0, 8.0, true, kSecondsPerBeat));
    EXPECT_EQ(doc.getClip(inside), nullptr);
    EXPECT_DOUBLE_EQ(doc.getClip(before)->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(doc.getClip(later)->startBeat, 6.0);
    EXPECT_DOUBLE_EQ(doc.getClip(otherTrack)->startBeat, 10.0); // not in the range's tracks
}

TEST_F(TimelineDocTest, DeleteRangeOfEmptyTimeClosesGapOrIsANoOp) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto later = doc.addClip(track, 10.0, 2.0, "later");
    CountingListener listener;
    doc.addListener(&listener);

    EXPECT_FALSE(doc.deleteRange({track}, 2.0, 4.0, false, kSecondsPerBeat));
    EXPECT_EQ(listener.calls, 0);

    EXPECT_TRUE(doc.deleteRange({track}, 2.0, 4.0, true, kSecondsPerBeat));
    EXPECT_DOUBLE_EQ(doc.getClip(later)->startBeat, 8.0);
    doc.removeListener(&listener);
}

TEST_F(TimelineDocTest, DeleteRangeRejectsBadArguments) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    doc.addClip(track, 0.0, 8.0, "c");
    EXPECT_FALSE(doc.deleteRange({track}, 4.0, 4.0, false, kSecondsPerBeat));
    EXPECT_FALSE(doc.deleteRange({track}, 6.0, 2.0, false, kSecondsPerBeat));
    EXPECT_FALSE(doc.deleteRange({track}, -1.0, 2.0, false, kSecondsPerBeat));
    EXPECT_FALSE(doc.deleteRange({track}, 1.0, 2.0, false, 0.0));
    EXPECT_FALSE(doc.deleteRange({TrackId{999}}, 1.0, 2.0, false, kSecondsPerBeat));
    EXPECT_DOUBLE_EQ(doc.getTrack(track)->clips[0].lengthBeats, 8.0);
}

TEST_F(TimelineDocTest, ClipToRangeClipsNotesFadesAndAudio) {
    Clip clip;
    clip.id = ClipId{7};
    clip.name = "src";
    clip.startBeat = 4.0;
    clip.lengthBeats = 8.0;
    clip.assetRef = "Audio/x.wav";
    clip.sourceStartSeconds = 1.0;
    clip.fadeInBeats = 1.0;
    clip.fadeOutBeats = 1.0;
    clip.notes.push_back(makeNote(1.0, 60, 2.0)); // 5..7 absolute: straddles 6
    clip.notes.push_back(makeNote(5.0, 62, 1.0)); // 9..10 absolute: inside

    const auto fragment = TimelineDoc::clipToRange(clip, 6.0, 20.0, kSecondsPerBeat);
    ASSERT_TRUE(fragment.has_value());
    EXPECT_EQ(fragment->id, clip.id);
    EXPECT_DOUBLE_EQ(fragment->startBeat, 6.0);
    EXPECT_DOUBLE_EQ(fragment->lengthBeats, 6.0);
    EXPECT_DOUBLE_EQ(fragment->sourceStartSeconds, 2.0);
    EXPECT_DOUBLE_EQ(fragment->fadeInBeats, 0.0);  // cut edge
    EXPECT_DOUBLE_EQ(fragment->fadeOutBeats, 1.0); // original edge survives
    ASSERT_EQ(fragment->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(fragment->notes[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(fragment->notes[0].lengthBeats, 1.0);
    EXPECT_DOUBLE_EQ(fragment->notes[1].startBeat, 3.0);

    const auto inner = TimelineDoc::clipToRange(clip, 7.0, 9.0, kSecondsPerBeat);
    ASSERT_TRUE(inner.has_value());
    EXPECT_DOUBLE_EQ(inner->lengthBeats, 2.0);
    EXPECT_DOUBLE_EQ(inner->fadeOutBeats, 0.0);
    EXPECT_TRUE(inner->notes.empty()); // the straddler ends at 7, the other note starts at 9

    EXPECT_FALSE(TimelineDoc::clipToRange(clip, 12.0, 14.0, kSecondsPerBeat).has_value());
    EXPECT_FALSE(TimelineDoc::clipToRange(clip, 0.0, 4.0, kSecondsPerBeat).has_value());
}

TEST_F(TimelineDocTest, ClipToRangeMatchesWhatDeleteRangeRemoves) {
    // The copy and the delete share the cut rule: what a range copy captures is exactly what the
    // same range's delete takes away.
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 8.0, "c");
    doc.addNote(clip, makeNote(1.0, 60, 2.0));
    doc.addNote(clip, makeNote(3.5, 61, 1.0));
    doc.addNote(clip, makeNote(5.0, 62, 2.0));

    const auto fragment = TimelineDoc::clipToRange(*doc.getClip(clip), 2.0, 6.0, kSecondsPerBeat);
    ASSERT_TRUE(fragment.has_value());
    int copiedNotes = (int)fragment->notes.size();
    ASSERT_TRUE(doc.deleteRange({track}, 2.0, 6.0, false, kSecondsPerBeat));
    int remainingNotes = 0;
    for (const auto& c : doc.getTrack(track)->clips)
        remainingNotes += (int)c.notes.size();
    EXPECT_EQ(copiedNotes, 3);    // every note overlaps [2, 6)
    EXPECT_EQ(remainingNotes, 2); // the two halves outside it
}
