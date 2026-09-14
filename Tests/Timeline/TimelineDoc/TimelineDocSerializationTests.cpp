// TimelineDoc serialisation tests: the general toVar/fromVar round trip, id-counter survival, malformed input and cap
// rejection, and load-time repair of mis-ordered content.

#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, ToVarFromVarRoundTripsExactly) {
    buildPopulatedDoc(doc);
    const auto before = dump(doc);

    TimelineDoc loaded;
    CountingListener loadedListener;
    loaded.addListener(&loadedListener);
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    EXPECT_EQ(loadedListener.calls, 1); // exactly one notification for the whole load
    EXPECT_EQ(loaded.getRevision(), 1);
    EXPECT_EQ(dump(loaded), before);

    // Spot-check a few fields rather than trusting the string alone.
    ASSERT_EQ(loaded.getTracks().size(), 2u);
    const auto& lead = loaded.getTracks()[0];
    EXPECT_EQ(lead.name, "Lead");
    EXPECT_EQ(lead.colourArgb, 0xff112233u);
    EXPECT_EQ(lead.bindingUuid, "uuid-track-in-1");
    ASSERT_EQ(lead.clips.size(), 2u);
    EXPECT_EQ(lead.clips[0].name, "A");
    ASSERT_EQ(lead.clips[0].notes.size(), 2u);
    EXPECT_EQ(lead.clips[0].notes[0].pitch, 60);
    EXPECT_EQ(lead.clips[0].notes[0].channel, 2);
    ASSERT_EQ(lead.lanes.size(), 1u);
    EXPECT_EQ(lead.lanes[0].paramId, "cutoff");
    ASSERT_EQ(lead.lanes[0].points.size(), 2u);
    EXPECT_DOUBLE_EQ(lead.lanes[0].points[0].beat, 0.0);
    EXPECT_EQ(loaded.getTracks()[1].muted, true);

    loaded.removeListener(&loadedListener);
}

TEST_F(TimelineDocTest, IdCountersSurviveTheRoundTrip) {
    buildPopulatedDoc(doc);
    const auto lastTrack = doc.getTracks().back().id;
    const auto lastClip = doc.getTracks().back().clips.back().id;

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));

    const auto newTrack = loaded.addTrack(TrackKind::Midi, "After load");
    EXPECT_GT(newTrack.value, lastTrack.value);
    EXPECT_EQ(loaded.getTrack(newTrack)->id, newTrack);

    const auto newClip = loaded.addClip(newTrack, 0.0, 1.0, "c");
    EXPECT_GT(newClip.value, lastClip.value);
    // The new id collides with nothing that came off the file.
    int matches = 0;
    for (const auto& track : loaded.getTracks())
        for (const auto& clip : track.clips)
            if (clip.id == newClip)
                ++matches;
    EXPECT_EQ(matches, 1);
    EXPECT_EQ(loaded.getTrackForClip(newClip)->id, newTrack);
}

TEST_F(TimelineDocTest, ToVarCarriesTheFormatVersion) {
    const auto state = doc.toVar();
    ASSERT_NE(state.getDynamicObject(), nullptr);
    EXPECT_EQ(static_cast<int>(state.getDynamicObject()->getProperty("version")), TimelineDoc::kFormatVersion);
    EXPECT_TRUE(state.getDynamicObject()->getProperty("tracks").isArray());
}

TEST_F(TimelineDocTest, RoundTripSurvivesJsonText) {
    buildPopulatedDoc(doc);
    const auto text = juce::JSON::toString(doc.toVar());

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(dump(loaded), dump(doc));
}

// ------------------------------------------------------- 11. all-or-nothing --

TEST_F(TimelineDocTest, FromVarLeavesTheDocUntouchedOnMalformedInput) {
    buildPopulatedDoc(doc);
    const auto before = dump(doc);
    const auto revisionBefore = doc.getRevision();
    const auto callsBefore = listener.calls;

    const juce::StringArray malformed{
        // a note pitch outside 0..127
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":4.0,
            "notes":[{"startBeat":0.0,"lengthBeats":1.0,"pitch":200,"velocity":100,"channel":1}]}]}]})",
        // a string where a number belongs
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":"soon","lengthBeats":4.0}]}]})",
        // wrong format version
        R"({"version":2,"tracks":[]})",
        // missing version
        R"({"tracks":[]})",
        // a reserved track kind this build can't represent
        R"({"version":1,"tracks":[{"id":1,"kind":7}]})",
        // duplicate track ids
        R"({"version":1,"tracks":[{"id":1},{"id":1}]})",
        // duplicate clip ids across tracks
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":9,"startBeat":0.0,"lengthBeats":1.0}]},
            {"id":2,"clips":[{"id":9,"startBeat":0.0,"lengthBeats":1.0}]}]})",
        // two lanes bound to the same (nodeUuid, paramId)
        R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"u","paramId":"p"},
            {"id":2,"nodeUuid":"u","paramId":"p"}]}]})",
        // a lane with no parameter binding
        R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"","paramId":"p"}]}]})",
        // a zero-length clip
        R"({"version":1,"tracks":[{"id":1,"clips":[{"id":1,"startBeat":0.0,"lengthBeats":0.0}]}]})",
        // an id of zero (the invalid sentinel)
        R"({"version":1,"tracks":[{"id":0}]})",
        // an id above kMaxIdValue: the next allocation off it (id + 1) must never signed-overflow
        R"({"version":1,"tracks":[{"id":2000000000000000}]})",
        // a next-id counter above kMaxIdValue, same overflow reason
        R"({"version":1,"nextTrackId":2000000000000000,"tracks":[]})",
        // a breakpoint curve beyond Bezier
        R"({"version":1,"tracks":[{"id":1,"lanes":[{"id":1,"nodeUuid":"u","paramId":"p",
            "points":[{"beat":0.0,"value":0.5,"curve":9}]}]}]})",
        // tracks isn't an array
        R"({"version":1,"tracks":42})",
        // a track that isn't an object
        R"({"version":1,"tracks":[7]})",
    };

    for (const auto& text : malformed) {
        const auto state = juce::JSON::parse(text);
        ASSERT_FALSE(state.isVoid()) << "test fixture JSON failed to parse: " << text;
        EXPECT_FALSE(doc.fromVar(state)) << text;
        EXPECT_EQ(dump(doc), before) << text;
        EXPECT_EQ(doc.getRevision(), revisionBefore) << text;
        EXPECT_EQ(listener.calls, callsBefore) << text;
    }

    // Not an object at all.
    EXPECT_FALSE(doc.fromVar(juce::var()));
    EXPECT_FALSE(doc.fromVar(juce::var(7)));
    EXPECT_EQ(dump(doc), before);
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, FromVarRejectsCapViolations) {
    juce::Array<juce::var> tracksVar;
    for (int i = 0; i < TimelineDoc::kMaxTracks + 1; ++i) {
        juce::DynamicObject::Ptr t = new juce::DynamicObject();
        t->setProperty("id", i + 1);
        tracksVar.add(juce::var(t.get()));
    }
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", TimelineDoc::kFormatVersion);
    root->setProperty("tracks", tracksVar);

    EXPECT_FALSE(doc.fromVar(juce::var(root.get())));
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(doc.getRevision(), 0);
}

TEST_F(TimelineDocTest, FromVarFloorsIdCountersAboveTheHighestIdInTheFile) {
    // A hand-edited file whose counters are stale must not be able to reissue a live id.
    const auto* text = R"({"version":1,"nextTrackId":1,"nextClipId":1,"nextLaneId":1,
        "tracks":[{"id":50,"clips":[{"id":70,"startBeat":0.0,"lengthBeats":4.0}],
        "lanes":[{"id":90,"nodeUuid":"u","paramId":"p"}]}]})";
    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));

    const auto track = doc.getTracks()[0].id;
    EXPECT_EQ(track.value, 50);
    EXPECT_GT(doc.addTrack(TrackKind::Midi, "next").value, 50);
    EXPECT_GT(doc.addClip(track, 0.0, 1.0, "c").value, 70);
    EXPECT_GT(doc.addLane(track, "u2", "p2", makeRange(0.0f, 1.0f, 0.0f)).value, 90);
}

// -------------------------------------------------------------- 12. re-sort --

TEST_F(TimelineDocTest, FromVarReSortsMisOrderedFileContent) {
    const auto* text = R"({
        "version": 1,
        "tracks": [{
            "id": 1, "kind": 0, "name": "T",
            "clips": [
                { "id": 2, "name": "late", "startBeat": 8.0, "lengthBeats": 4.0, "notes": [] },
                { "id": 1, "name": "early", "startBeat": 0.0, "lengthBeats": 4.0, "notes": [
                    { "id": 1, "startBeat": 3.0, "lengthBeats": 1.0, "pitch": 60, "velocity": 100, "channel": 1 },
                    { "id": 2, "startBeat": 1.0, "lengthBeats": 1.0, "pitch": 64, "velocity": 100, "channel": 1 },
                    { "id": 3, "startBeat": 1.0, "lengthBeats": 1.0, "pitch": 62, "velocity": 100, "channel": 1 }
                ]}
            ],
            "lanes": [{
                "id": 1, "nodeUuid": "u", "paramId": "p",
                "range": { "minValue": 0.0, "maxValue": 1.0, "defaultValue": 0.0 },
                "points": [
                    { "beat": 4.0, "value": 0.9, "tension": 0.0, "curve": 1 },
                    { "beat": 0.0, "value": 0.1, "tension": 0.0, "curve": 1 },
                    { "beat": 4.0, "value": 0.4, "tension": 0.0, "curve": 0 }
                ]
            }]
        }]
    })";

    ASSERT_TRUE(doc.fromVar(juce::JSON::parse(text)));
    const auto& track = doc.getTracks()[0];

    ASSERT_EQ(track.clips.size(), 2u);
    EXPECT_EQ(track.clips[0].name, "early");
    EXPECT_EQ(track.clips[1].name, "late");

    const auto& notes = track.clips[0].notes;
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_DOUBLE_EQ(notes[0].startBeat, 1.0);
    EXPECT_EQ(notes[0].pitch, 62);
    EXPECT_EQ(notes[1].pitch, 64);
    EXPECT_DOUBLE_EQ(notes[2].startBeat, 3.0);

    // Two points on the same beat collapse to one, last-in-file wins — the same rule
    // addBreakpoint applies.
    ASSERT_EQ(track.lanes.size(), 1u);
    const auto& points = track.lanes[0].points;
    ASSERT_EQ(points.size(), 2u);
    EXPECT_DOUBLE_EQ(points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(points[1].beat, 4.0);
    EXPECT_DOUBLE_EQ(points[1].value, 0.4);
    EXPECT_EQ(points[1].curve, static_cast<int>(BreakpointCurve::Hold));
}
