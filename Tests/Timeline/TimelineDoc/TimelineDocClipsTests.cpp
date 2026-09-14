// TimelineDoc clip tests: ordering, resizing, move-to-track, split/duplicate/join, naming, and the audio-clip fields
// (asset ref, gain, fades) plus clip/note mute.

#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, ClipsStaySortedByStartBeat) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    doc.addClip(track, 8.0, 4.0, "late");
    doc.addClip(track, 0.0, 4.0, "early");
    doc.addClip(track, 4.0, 4.0, "middle");

    const auto* t = doc.getTrack(track);
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->clips.size(), 3u);
    EXPECT_EQ(t->clips[0].name, "early");
    EXPECT_EQ(t->clips[1].name, "middle");
    EXPECT_EQ(t->clips[2].name, "late");
}

TEST_F(TimelineDocTest, ClipTiesAreBrokenById) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 2.0, 1.0, "a");
    const auto b = doc.addClip(track, 2.0, 1.0, "b");
    const auto c = doc.addClip(track, 2.0, 1.0, "c");

    const auto* t = doc.getTrack(track);
    ASSERT_EQ(t->clips.size(), 3u);
    EXPECT_EQ(t->clips[0].id, a);
    EXPECT_EQ(t->clips[1].id, b);
    EXPECT_EQ(t->clips[2].id, c);
}

TEST_F(TimelineDocTest, MoveClipReSortsAndKeepsNotes) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto first = doc.addClip(track, 0.0, 4.0, "first");
    const auto second = doc.addClip(track, 4.0, 4.0, "second");
    ASSERT_TRUE(doc.addNote(first, makeNote(1.0, 60)).isValid());

    ASSERT_TRUE(doc.moveClip(first, 16.0));
    const auto* t = doc.getTrack(track);
    ASSERT_EQ(t->clips.size(), 2u);
    EXPECT_EQ(t->clips[0].id, second);
    EXPECT_EQ(t->clips[1].id, first);
    ASSERT_EQ(t->clips[1].notes.size(), 1u); // notes are clip-relative, they ride along
    EXPECT_DOUBLE_EQ(t->clips[1].notes[0].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(doc.getClip(first)->startBeat, 16.0);
}

TEST_F(TimelineDocTest, ResizeClipDoesNotChangeOrder) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto first = doc.addClip(track, 0.0, 4.0, "first");
    const auto second = doc.addClip(track, 4.0, 4.0, "second");

    ASSERT_TRUE(doc.resizeClip(first, 64.0));
    const auto* t = doc.getTrack(track);
    EXPECT_EQ(t->clips[0].id, first);
    EXPECT_EQ(t->clips[1].id, second);
    EXPECT_DOUBLE_EQ(doc.getClip(first)->lengthBeats, 64.0);
}

TEST_F(TimelineDocTest, InvalidClipEditsAreRejected) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revisionBefore = doc.getRevision();

    EXPECT_FALSE(doc.addClip(track, -1.0, 4.0, "negative").isValid());
    EXPECT_FALSE(doc.addClip(track, 0.0, 0.0, "zero length").isValid());
    EXPECT_FALSE(doc.addClip(TrackId{999}, 0.0, 4.0, "no track").isValid());
    EXPECT_FALSE(doc.moveClip(clip, -0.5));
    EXPECT_FALSE(doc.resizeClip(clip, -4.0));
    EXPECT_FALSE(doc.removeClip(ClipId{999}));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, ClipLookupHelpersResolveOwner) {
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_NE(doc.getTrackForClip(clip), nullptr);
    EXPECT_EQ(doc.getTrackForClip(clip)->id, track);
    EXPECT_EQ(doc.getTrackForClip(ClipId{999}), nullptr);
    ASSERT_TRUE(doc.removeClip(clip));
    EXPECT_EQ(doc.getClip(clip), nullptr);
}

// ------------------------------------------------------------------ 5. notes --

TEST_F(TimelineDocTest, NewClipDefaultsToNoAsset) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto* c = doc.getClip(clip);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->assetRef.isEmpty());
    EXPECT_DOUBLE_EQ(c->gainDb, 0.0);
    EXPECT_DOUBLE_EQ(c->fadeInBeats, 0.0);
    EXPECT_DOUBLE_EQ(c->fadeOutBeats, 0.0);
    EXPECT_DOUBLE_EQ(c->sourceStartSeconds, 0.0);
}

TEST_F(TimelineDocTest, SetClipAssetGainAndFadesMutateOnce) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revisionAfterAdd = doc.getRevision();
    const int callsAfterAdd = listener.calls;

    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 0.25));
    ASSERT_TRUE(doc.setClipGainDb(clip, -3.5));
    ASSERT_TRUE(doc.setClipFades(clip, 0.5, 1.5));

    const auto* c = doc.getClip(clip);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(c->sourceStartSeconds, 0.25);
    EXPECT_DOUBLE_EQ(c->gainDb, -3.5);
    EXPECT_DOUBLE_EQ(c->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(c->fadeOutBeats, 1.5);

    // Three effective mutations, three notifications.
    EXPECT_EQ(doc.getRevision(), revisionAfterAdd + 3);
    EXPECT_EQ(listener.calls, callsAfterAdd + 3);

    // Setting the value already stored is a no-op on all three: no bump, no notification.
    const auto revision = doc.getRevision();
    const int calls = listener.calls;
    EXPECT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 0.25));
    EXPECT_TRUE(doc.setClipGainDb(clip, -3.5));
    EXPECT_TRUE(doc.setClipFades(clip, 0.5, 1.5));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(listener.calls, calls);
}

TEST_F(TimelineDocTest, SetClipAssetRejectsEscapingPaths) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto revision = doc.getRevision();

    // Every one of these would let a bundle read a file outside itself on whoever opens it.
    const juce::StringArray escaping = {
        "/etc/passwd",           "/Users/someone/take.wav",   "../take.wav",
        "Audio/../../take.wav",  "Audio/../../../etc/passwd", "..\\take.wav",
        "C:\\Windows\\take.wav", "C:/Windows/take.wav",       "\\\\server\\share\\take.wav"};
    for (const auto& ref : escaping) {
        EXPECT_FALSE(doc.setClipAsset(clip, ref, 0.0)) << "accepted escaping assetRef: " << ref;
        EXPECT_FALSE(TimelineDoc::isValidAssetRef(ref)) << "isValidAssetRef accepted: " << ref;
    }

    // Legal ones: bundle-relative, and the empty "no asset" value every MIDI clip carries.
    for (const auto& ref : juce::StringArray{"Audio/take-1.wav", "take.wav", "Audio/Nested/take..wav", ""})
        EXPECT_TRUE(TimelineDoc::isValidAssetRef(ref)) << "rejected legal assetRef: " << ref;

    // A non-finite source offset and a non-finite gain/fade are rejected too.
    EXPECT_FALSE(doc.setClipAsset(clip, "Audio/take-1.wav", std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.setClipAsset(clip, "Audio/take-1.wav", -1.0));
    EXPECT_FALSE(doc.setClipGainDb(clip, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(doc.setClipFades(clip, -0.1, 0.0));
    EXPECT_FALSE(doc.setClipFades(clip, 0.0, std::numeric_limits<double>::quiet_NaN()));

    // Nothing was mutated by any rejection.
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_TRUE(doc.getClip(clip)->assetRef.isEmpty());
}

TEST_F(TimelineDocTest, AudioClipFieldsSurviveRoundTrip) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 4.0, 8.0, "Take 1");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(doc.setClipGainDb(clip, -6.0));
    ASSERT_TRUE(doc.setClipFades(clip, 0.25, 0.75));

    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(doc.toVar()));
    EXPECT_EQ(dump(reloaded), dump(doc));

    ASSERT_EQ(reloaded.getTracks().size(), 1u);
    EXPECT_EQ(reloaded.getTracks()[0].kind, TrackKind::Audio);
    ASSERT_EQ(reloaded.getTracks()[0].clips.size(), 1u);
    const auto& c = reloaded.getTracks()[0].clips[0];
    EXPECT_EQ(c.assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(c.sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(c.gainDb, -6.0);
    EXPECT_DOUBLE_EQ(c.fadeInBeats, 0.25);
    EXPECT_DOUBLE_EQ(c.fadeOutBeats, 0.75);
}

TEST_F(TimelineDocTest, FromVarDefaultsAudioFieldsWhenAbsent) {
    // Exactly what an older file with no audio keys at all looks like, version still 1.
    const auto* text = R"({"version":1,"tracks":[{"id":1,"kind":1,"name":"A",
        "clips":[{"id":1,"name":"c","startBeat":0.0,"lengthBeats":4.0,"notes":[]}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto& c = doc.getTracks()[0].clips[0];
    EXPECT_TRUE(c.assetRef.isEmpty());
    EXPECT_DOUBLE_EQ(c.gainDb, 0.0);
    EXPECT_DOUBLE_EQ(c.fadeInBeats, 0.0);
    EXPECT_DOUBLE_EQ(c.fadeOutBeats, 0.0);
    EXPECT_DOUBLE_EQ(c.sourceStartSeconds, 0.0);
}

TEST_F(TimelineDocTest, FromVarRejectsEscapingOrMalformedAudioFields) {
    // A hand-edited bundle must not be the way around setClipAsset's check.
    const juce::StringArray bad = {
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":"../../etc/passwd"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":"/etc/passwd"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":"C:/Windows/take.wav"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"assetRef":17}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"fadeInBeats":-1.0}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"sourceStartSeconds":-0.5}]}]})",
    };
    for (const auto& text : bad) {
        TimelineDoc target;
        EXPECT_FALSE(target.fromVar(juce::JSON::parse(text))) << "accepted malformed clip: " << text;
        EXPECT_TRUE(target.isEmpty()) << "a rejected load must leave the doc untouched";
    }
}

TEST_F(TimelineDocTest, DuplicateAndSplitCarryAudioFields) {
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Take");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/take-1.wav", 2.0));
    ASSERT_TRUE(doc.setClipGainDb(clip, -2.0));
    ASSERT_TRUE(doc.setClipFades(clip, 0.5, 1.0));

    // Duplicate is an exact copy of every audio field (no tempo map needed).
    const auto dup = doc.duplicateClip(clip);
    ASSERT_TRUE(dup.isValid());
    const auto* d = doc.getClip(dup);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(d->sourceStartSeconds, 2.0);
    EXPECT_DOUBLE_EQ(d->gainDb, -2.0);
    EXPECT_DOUBLE_EQ(d->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(d->fadeOutBeats, 1.0);

    // Split: both halves keep the asset and gain, each keeps the fade at the edge it still owns,
    // and the right half's sourceStartSeconds is deliberately NOT advanced (no tempo map here —
    // see splitClip's comment).
    const auto halves = doc.splitClip(clip, 4.0);
    ASSERT_TRUE(halves.first.isValid());
    ASSERT_TRUE(halves.second.isValid());
    const auto* left = doc.getClip(halves.first);
    const auto* right = doc.getClip(halves.second);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->assetRef, "Audio/take-1.wav");
    EXPECT_EQ(right->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(left->gainDb, -2.0);
    EXPECT_DOUBLE_EQ(right->gainDb, -2.0);
    EXPECT_DOUBLE_EQ(left->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(left->fadeOutBeats, 0.0);
    EXPECT_DOUBLE_EQ(right->fadeInBeats, 0.0);
    EXPECT_DOUBLE_EQ(right->fadeOutBeats, 1.0);
    EXPECT_DOUBLE_EQ(right->sourceStartSeconds, 2.0);
}

// -------------------------------------------------- 14. mute, rename, retrack --
// The data layer behind the Cubase-style Mute tool and the cross-lane clip drag. Mute is
// persistent state, not a transient UI mode: it round-trips, it survives every structural edit,
// and (in TimelineSnapshotTests) it is what makes content vanish from the flatten.

TEST_F(TimelineDocTest, ClipAndNoteMuteSurviveRoundTrip) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto audible = doc.addClip(track, 0.0, 4.0, "Audible");
    const auto silent = doc.addClip(track, 8.0, 4.0, "Silent");
    const auto keptNote = doc.addNote(audible, makeNote(0.0, 60));
    const auto mutedNote = doc.addNote(audible, makeNote(2.0, 67));
    ASSERT_TRUE(doc.setClipMuted(silent, true));
    ASSERT_TRUE(doc.setNoteMuted(mutedNote, true));

    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(doc.toVar()));
    EXPECT_EQ(dump(reloaded), dump(doc));

    ASSERT_NE(reloaded.getClip(silent), nullptr);
    EXPECT_TRUE(reloaded.getClip(silent)->muted);
    EXPECT_FALSE(reloaded.getClip(audible)->muted);
    ASSERT_NE(reloaded.getNote(mutedNote), nullptr);
    EXPECT_TRUE(reloaded.getNote(mutedNote)->muted);
    EXPECT_FALSE(reloaded.getNote(keptNote)->muted);
}

TEST_F(TimelineDocTest, FromVarDefaultsMutedWhenAbsent) {
    // Exactly what a file written before per-clip / per-note mute existed looks like: no "muted"
    // key anywhere, version still 1. Absent must mean audible, or every older project loads silent.
    const auto* text = R"({"version":1,"tracks":[{"id":1,"kind":0,"name":"A",
        "clips":[{"id":1,"name":"c","startBeat":0.0,"lengthBeats":4.0,
                  "notes":[{"id":1,"startBeat":0.0,"lengthBeats":1.0,"pitch":60}]}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto& clip = doc.getTracks()[0].clips[0];
    EXPECT_FALSE(clip.muted);
    ASSERT_EQ(clip.notes.size(), 1u);
    EXPECT_FALSE(clip.notes[0].muted);

    // Present but mistyped is malformed, not something to coerce — same rule every other optional
    // field here follows.
    const juce::StringArray bad = {
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"muted":"yes"}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"muted":1}]}]})",
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,
            "notes":[{"id":1,"pitch":60,"muted":"yes"}]}]}]})",
    };
    for (const auto& malformed : bad) {
        TimelineDoc target;
        EXPECT_FALSE(target.fromVar(juce::JSON::parse(malformed))) << "accepted mistyped muted: " << malformed;
        EXPECT_TRUE(target.isEmpty()) << "a rejected load must leave the doc untouched";
    }
}

TEST_F(TimelineDocTest, SetClipMutedAndSetNoteMutedMutateOnce) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    const auto note = doc.addNote(clip, makeNote(0.0, 60));
    const auto revisionAfterSetup = doc.getRevision();
    const int callsAfterSetup = listener.calls;

    EXPECT_TRUE(doc.setClipMuted(clip, true));
    EXPECT_TRUE(doc.setNoteMuted(note, true));
    EXPECT_TRUE(doc.getClip(clip)->muted);
    EXPECT_TRUE(doc.getNote(note)->muted);
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 2);
    EXPECT_EQ(listener.calls, callsAfterSetup + 2);

    // Setting the flag already stored is a no-op: true, but no bump and no notification.
    EXPECT_TRUE(doc.setClipMuted(clip, true));
    EXPECT_TRUE(doc.setNoteMuted(note, true));
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 2);
    EXPECT_EQ(listener.calls, callsAfterSetup + 2);

    // An id that doesn't resolve is a rejection, not a no-op — and mutates nothing either way.
    EXPECT_FALSE(doc.setClipMuted(ClipId{}, false));
    EXPECT_FALSE(doc.setClipMuted(ClipId{9999}, false));
    EXPECT_FALSE(doc.setNoteMuted(NoteId{}, false));
    EXPECT_FALSE(doc.setNoteMuted(NoteId{9999}, false));
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 2);
    EXPECT_EQ(listener.calls, callsAfterSetup + 2);

    // Un-muting is symmetrical.
    EXPECT_TRUE(doc.setClipMuted(clip, false));
    EXPECT_TRUE(doc.setNoteMuted(note, false));
    EXPECT_FALSE(doc.getClip(clip)->muted);
    EXPECT_FALSE(doc.getNote(note)->muted);
    EXPECT_EQ(doc.getRevision(), revisionAfterSetup + 4);
}

TEST_F(TimelineDocTest, SetClipNameTrimsAndRejectsBlank) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 4.0, "A");

    const auto revisionBefore = doc.getRevision();
    EXPECT_TRUE(doc.setClipName(clip, "  Verse 1\t"));
    EXPECT_EQ(doc.getClip(clip)->name, "Verse 1") << "the stored name must be trimmed";
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // Unchanged after trimming is a no-op, whatever whitespace the caller wrapped it in.
    const auto revisionAfterRename = doc.getRevision();
    const int callsAfterRename = listener.calls;
    EXPECT_TRUE(doc.setClipName(clip, "Verse 1"));
    EXPECT_TRUE(doc.setClipName(clip, "   Verse 1   "));
    EXPECT_EQ(doc.getRevision(), revisionAfterRename);
    EXPECT_EQ(listener.calls, callsAfterRename);

    // Empty after trimming is REJECTED, not stored: a blank clip title reads as a broken lane.
    for (const auto& blank : juce::StringArray{"", " ", "\t", "\n", "   \t\n  "})
        EXPECT_FALSE(doc.setClipName(clip, blank)) << "accepted a blank clip name";
    EXPECT_FALSE(doc.setClipName(ClipId{}, "x"));
    EXPECT_FALSE(doc.setClipName(ClipId{9999}, "x"));

    EXPECT_EQ(doc.getClip(clip)->name, "Verse 1");
    EXPECT_EQ(doc.getRevision(), revisionAfterRename);
    EXPECT_EQ(listener.calls, callsAfterRename);
}

TEST_F(TimelineDocTest, MoveClipToTrackCarriesEverythingAcross) {
    const auto from = doc.addTrack(TrackKind::Midi, "From");
    const auto to = doc.addTrack(TrackKind::Midi, "To");
    // Two clips already on the destination, so the arrival has to land in sorted position rather
    // than simply being appended.
    doc.addClip(to, 0.0, 4.0, "First");
    doc.addClip(to, 20.0, 4.0, "Last");

    const auto clip = doc.addClip(from, 0.0, 8.0, "Travelling");
    const auto low = doc.addNote(clip, makeNote(0.5, 60, 1.0, 90, 2));
    const auto high = doc.addNote(clip, makeNote(4.0, 72, 2.0, 30, 5));
    ASSERT_TRUE(doc.setNoteMuted(high, true));
    ASSERT_TRUE(doc.setClipMuted(clip, true));
    ASSERT_TRUE(doc.setClipGainDb(clip, -4.5));
    ASSERT_TRUE(doc.setClipFades(clip, 0.25, 0.75));

    const auto revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;
    ASSERT_TRUE(doc.moveClipToTrack(clip, to, 12.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1) << "a cross-track move is exactly one mutation";
    EXPECT_EQ(listener.calls, callsBefore + 1);

    EXPECT_TRUE(doc.getTrack(from)->clips.empty()) << "the clip must leave its old track";
    const auto* destTrack = doc.getTrack(to);
    ASSERT_EQ(destTrack->clips.size(), 3u);
    EXPECT_EQ(destTrack->clips[1].id, clip) << "the arrival must be re-seated in sorted position";
    EXPECT_EQ(doc.getTrackForClip(clip), destTrack);

    const auto* moved = doc.getClip(clip);
    ASSERT_NE(moved, nullptr);
    EXPECT_DOUBLE_EQ(moved->startBeat, 12.0);
    EXPECT_DOUBLE_EQ(moved->lengthBeats, 8.0);
    EXPECT_EQ(moved->name, "Travelling");
    EXPECT_TRUE(moved->muted);
    EXPECT_DOUBLE_EQ(moved->gainDb, -4.5);
    EXPECT_DOUBLE_EQ(moved->fadeInBeats, 0.25);
    EXPECT_DOUBLE_EQ(moved->fadeOutBeats, 0.75);

    // Notes are clip-relative, so the payload rides along completely untouched — same ids, same
    // clip-relative beats, same per-note mute.
    ASSERT_EQ(moved->notes.size(), 2u);
    EXPECT_EQ(moved->notes[0].id, low);
    EXPECT_DOUBLE_EQ(moved->notes[0].startBeat, 0.5);
    EXPECT_EQ(moved->notes[0].velocity, 90);
    EXPECT_EQ(moved->notes[0].channel, 2);
    EXPECT_FALSE(moved->notes[0].muted);
    EXPECT_EQ(moved->notes[1].id, high);
    EXPECT_DOUBLE_EQ(moved->notes[1].startBeat, 4.0);
    EXPECT_TRUE(moved->notes[1].muted);

    // An audio clip crosses to another Audio track with its asset intact.
    const auto audioFrom = doc.addTrack(TrackKind::Audio, "Take A");
    const auto audioTo = doc.addTrack(TrackKind::Audio, "Take B");
    const auto audioClip = doc.addClip(audioFrom, 0.0, 4.0, "Take");
    ASSERT_TRUE(doc.setClipAsset(audioClip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(doc.moveClipToTrack(audioClip, audioTo, 6.0));
    EXPECT_EQ(doc.getTrackForClip(audioClip), doc.getTrack(audioTo));
    EXPECT_EQ(doc.getClip(audioClip)->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(doc.getClip(audioClip)->sourceStartSeconds, 1.5);
}

TEST_F(TimelineDocTest, MoveClipToTrackOnItsOwnTrackBehavesLikeMoveClip) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto early = doc.addClip(track, 0.0, 4.0, "Early");
    doc.addClip(track, 8.0, 4.0, "Late");

    const auto revisionBefore = doc.getRevision();
    ASSERT_TRUE(doc.moveClipToTrack(early, track, 12.0));
    EXPECT_DOUBLE_EQ(doc.getClip(early)->startBeat, 12.0);
    EXPECT_EQ(doc.getTrack(track)->clips[1].id, early) << "an in-place move still re-sorts";
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // Moving to where it already is is the same no-op moveClip is: true, no bump.
    const int callsAfterMove = listener.calls;
    EXPECT_TRUE(doc.moveClipToTrack(early, track, 12.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);
    EXPECT_EQ(listener.calls, callsAfterMove);

    // And the kind check is skipped in place: the model tolerates an assetRef on a clip that sits
    // on a MIDI track, so dragging that clip along its own lane must not suddenly fail.
    ASSERT_TRUE(doc.setClipAsset(early, "Audio/stray.wav", 0.0));
    EXPECT_TRUE(doc.moveClipToTrack(early, track, 2.0));
    EXPECT_DOUBLE_EQ(doc.getClip(early)->startBeat, 2.0);
}

TEST_F(TimelineDocTest, MoveClipToTrackRejectsBadDestinationsWithoutMutating) {
    const auto midiFrom = doc.addTrack(TrackKind::Midi, "Midi From");
    const auto midiTo = doc.addTrack(TrackKind::Midi, "Midi To");
    const auto audioFrom = doc.addTrack(TrackKind::Audio, "Audio From");
    const auto audioTo = doc.addTrack(TrackKind::Audio, "Audio To");
    const auto automation = doc.addTrack(TrackKind::Automation, "Automation");

    const auto midiClip = doc.addClip(midiFrom, 0.0, 4.0, "Midi");
    const auto audioClip = doc.addClip(audioFrom, 0.0, 4.0, "Audio");
    ASSERT_TRUE(doc.setClipAsset(audioClip, "Audio/take-1.wav", 0.0));

    const auto revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;

    // A destination that doesn't exist.
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, TrackId{}, 4.0));
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, TrackId{9999}, 4.0));
    // A clip that doesn't exist.
    EXPECT_FALSE(doc.moveClipToTrack(ClipId{}, midiTo, 4.0));
    EXPECT_FALSE(doc.moveClipToTrack(ClipId{9999}, midiTo, 4.0));
    // Kind mismatch, both directions — and an Automation track takes neither.
    EXPECT_FALSE(doc.moveClipToTrack(audioClip, midiTo, 4.0)) << "an audio clip must not land on a MIDI track";
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, audioTo, 4.0)) << "a MIDI clip must not land on an audio track";
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, automation, 4.0));
    EXPECT_FALSE(doc.moveClipToTrack(audioClip, automation, 4.0));
    // The same start-beat validation moveClip applies.
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, midiTo, -1.0));
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, midiTo, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.moveClipToTrack(midiClip, midiTo, std::numeric_limits<double>::infinity()));

    EXPECT_EQ(doc.getRevision(), revisionBefore) << "every rejection must leave the revision alone";
    EXPECT_EQ(listener.calls, callsBefore);
    EXPECT_EQ(doc.getTrackForClip(midiClip), doc.getTrack(midiFrom));
    EXPECT_EQ(doc.getTrackForClip(audioClip), doc.getTrack(audioFrom));

    // Emptying the assetRef is what makes the same clip legal on a MIDI track: the PAYLOAD decides,
    // not the track it came from.
    ASSERT_TRUE(doc.setClipAsset(audioClip, "", 0.0));
    EXPECT_TRUE(doc.moveClipToTrack(audioClip, midiTo, 4.0));
    EXPECT_EQ(doc.getTrackForClip(audioClip), doc.getTrack(midiTo));
}

TEST_F(TimelineDocTest, SplitDuplicateAndJoinCarryMuteFlags) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Muted");
    const auto early = doc.addNote(clip, makeNote(1.0, 60));           // entirely left of the cut
    const auto straddling = doc.addNote(clip, makeNote(3.5, 64, 1.0)); // crosses the cut at 4.0
    const auto late = doc.addNote(clip, makeNote(5.0, 67));            // entirely right of the cut
    ASSERT_TRUE(doc.setClipMuted(clip, true));
    ASSERT_TRUE(doc.setNoteMuted(straddling, true));

    // Split: BOTH halves inherit the clip's mute, and the straddling note's two halves both keep
    // the note's own mute (the left half keeps the original id, the right half gets a fresh one).
    const auto halves = doc.splitClip(clip, 4.0);
    ASSERT_TRUE(halves.first.isValid());
    ASSERT_TRUE(halves.second.isValid());
    const auto* left = doc.getClip(halves.first);
    const auto* right = doc.getClip(halves.second);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_TRUE(left->muted);
    EXPECT_TRUE(right->muted) << "cutting a muted clip in two must not make half of it audible";

    ASSERT_EQ(left->notes.size(), 2u);
    EXPECT_EQ(left->notes[0].id, early);
    EXPECT_FALSE(left->notes[0].muted);
    EXPECT_EQ(left->notes[1].id, straddling);
    EXPECT_TRUE(left->notes[1].muted);
    ASSERT_EQ(right->notes.size(), 2u);
    EXPECT_DOUBLE_EQ(right->notes[0].startBeat, 0.0);
    EXPECT_EQ(right->notes[0].pitch, 64);
    EXPECT_NE(right->notes[0].id, straddling) << "the right half of a split note gets a fresh id";
    EXPECT_TRUE(right->notes[0].muted) << "a note's mute travels with it through the re-basing";
    EXPECT_EQ(right->notes[1].id, late);
    EXPECT_FALSE(right->notes[1].muted);

    // Duplicate: the copy is muted too, and each copied note keeps its own flag.
    const auto dup = doc.duplicateClip(halves.first);
    ASSERT_TRUE(dup.isValid());
    const auto* copy = doc.getClip(dup);
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->muted);
    ASSERT_EQ(copy->notes.size(), 2u);
    EXPECT_FALSE(copy->notes[0].muted);
    EXPECT_TRUE(copy->notes[1].muted);
    EXPECT_NE(copy->notes[1].id, straddling) << "a duplicate's notes get fresh ids";

    // Join: the SURVIVOR's flag wins, whichever way round the two clips are muted.
    TimelineDoc joinDoc;
    const auto joinTrack = joinDoc.addTrack(TrackKind::Midi, "Join");
    const auto audibleA = joinDoc.addClip(joinTrack, 0.0, 4.0, "A");
    const auto mutedB = joinDoc.addClip(joinTrack, 4.0, 4.0, "B");
    const auto noteInB = joinDoc.addNote(mutedB, makeNote(1.0, 72));
    ASSERT_TRUE(joinDoc.setClipMuted(mutedB, true));
    ASSERT_TRUE(joinDoc.setNoteMuted(noteInB, true));
    ASSERT_TRUE(joinDoc.joinClips(audibleA, mutedB));
    const auto* joined = joinDoc.getClip(audibleA);
    ASSERT_NE(joined, nullptr);
    EXPECT_FALSE(joined->muted) << "joinClips keeps a's muted flag, not b's";
    ASSERT_EQ(joined->notes.size(), 1u);
    EXPECT_TRUE(joined->notes[0].muted) << "a merged note keeps its own mute, whatever the clips did";

    // ...and the mirror image: a muted `a` stays muted after swallowing an audible `b`.
    const auto mutedC = joinDoc.addClip(joinTrack, 16.0, 4.0, "C");
    const auto audibleD = joinDoc.addClip(joinTrack, 20.0, 4.0, "D");
    ASSERT_TRUE(joinDoc.setClipMuted(mutedC, true));
    ASSERT_TRUE(joinDoc.joinClips(mutedC, audibleD));
    EXPECT_TRUE(joinDoc.getClip(mutedC)->muted);
}

// -------------------------------------------------------------- 16. markers --
