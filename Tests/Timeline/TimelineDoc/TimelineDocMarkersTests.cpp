// TimelineDoc marker tests: add/remove/move/rename/colour, sort order, caps, and the round trip (including absent-key
// and mis-ordered-file repair).

#include "TimelineDocTestHelpers.h"

TEST_F(TimelineDocTest, FreshDocHasNoMarkers) {
    EXPECT_TRUE(doc.getMarkers().empty());
    EXPECT_FALSE(MarkerId{}.isValid());
    EXPECT_EQ(doc.getMarker(MarkerId{}), nullptr);
}

TEST_F(TimelineDocTest, AddMarkerAssignsIncreasingIdsAndKeepsThemSorted) {
    const auto late = doc.addMarker(8.0, "Chorus", 0xff112233);
    const auto early = doc.addMarker(2.0, "Intro", 0xff445566);
    ASSERT_TRUE(late.isValid());
    ASSERT_TRUE(early.isValid());
    EXPECT_LT(late.value, early.value) << "ids are handed out in call order, not beat order";

    // ...but the list itself is in beat order.
    ASSERT_EQ(doc.getMarkers().size(), 2u);
    EXPECT_EQ(doc.getMarkers()[0].id, early);
    EXPECT_EQ(doc.getMarkers()[1].id, late);
    EXPECT_DOUBLE_EQ(doc.getMarkers()[0].beat, 2.0);
    EXPECT_EQ(doc.getMarkers()[0].text, "Intro");
    EXPECT_EQ(doc.getMarkers()[0].colourArgb, 0xff445566u);
    EXPECT_EQ(doc.getRevision(), 2);
    EXPECT_EQ(listener.calls, 2);
}

TEST_F(TimelineDocTest, MarkerTiesAreBrokenById) {
    const auto first = doc.addMarker(4.0, "A", 0xff000000);
    const auto second = doc.addMarker(4.0, "B", 0xff000000);
    ASSERT_EQ(doc.getMarkers().size(), 2u);
    EXPECT_EQ(doc.getMarkers()[0].id, first) << "same beat: the lower id sorts first";
    EXPECT_EQ(doc.getMarkers()[1].id, second);
}

TEST_F(TimelineDocTest, AnEmptyMarkerLabelIsLegalAndIsNotTrimmedAway) {
    const auto blank = doc.addMarker(1.0, "", 0xff123456);
    ASSERT_TRUE(blank.isValid()) << "an unlabelled flag is a usable cue point";
    EXPECT_EQ(doc.getMarker(blank)->text, "");

    // Unlike setClipName, a whitespace label is stored verbatim rather than trimmed and refused:
    // a marker is identified by its position, not its name.
    const auto spaced = doc.addMarker(2.0, "  ", 0xff123456);
    ASSERT_TRUE(spaced.isValid());
    EXPECT_EQ(doc.getMarker(spaced)->text, "  ");
}

TEST_F(TimelineDocTest, InvalidMarkerAddsAreRejectedWithoutMutating) {
    const auto revisionBefore = doc.getRevision();
    const auto callsBefore = listener.calls;

    EXPECT_FALSE(doc.addMarker(-1.0, "Negative", 0xff000000).isValid());
    EXPECT_FALSE(doc.addMarker(std::numeric_limits<double>::quiet_NaN(), "NaN", 0xff000000).isValid());
    EXPECT_FALSE(doc.addMarker(std::numeric_limits<double>::infinity(), "Inf", 0xff000000).isValid());
    // Rejected, never truncated — kMaxMarkerTextLength + 1 characters.
    EXPECT_FALSE(
        doc.addMarker(0.0, juce::String::repeatedString("x", TimelineDoc::kMaxMarkerTextLength + 1), 0xff000000)
            .isValid());

    EXPECT_TRUE(doc.getMarkers().empty());
    EXPECT_EQ(doc.getRevision(), revisionBefore);
    EXPECT_EQ(listener.calls, callsBefore);

    // Exactly at the cap is fine — the boundary is inclusive.
    EXPECT_TRUE(
        doc.addMarker(0.0, juce::String::repeatedString("x", TimelineDoc::kMaxMarkerTextLength), 0xff000000).isValid());
}

TEST_F(TimelineDocTest, RemovedMarkerIdIsNeverReused) {
    const auto first = doc.addMarker(0.0, "One", 0xff000000);
    ASSERT_TRUE(doc.removeMarker(first));
    EXPECT_EQ(doc.getMarker(first), nullptr);
    EXPECT_TRUE(doc.getMarkers().empty());

    const auto second = doc.addMarker(0.0, "Two", 0xff000000);
    EXPECT_NE(first, second);
    EXPECT_GT(second.value, first.value);

    // Removing something that isn't there is a rejection, not a no-op success.
    EXPECT_FALSE(doc.removeMarker(first));
}

TEST_F(TimelineDocTest, MoveMarkerReSortsAndIsANoOpAtTheSameBeat) {
    const auto a = doc.addMarker(0.0, "A", 0xff000000);
    const auto b = doc.addMarker(4.0, "B", 0xff000000);
    ASSERT_EQ(doc.getMarkers()[0].id, a);

    ASSERT_TRUE(doc.moveMarker(a, 8.0));
    ASSERT_EQ(doc.getMarkers().size(), 2u);
    EXPECT_EQ(doc.getMarkers()[0].id, b) << "the moved marker re-seats at its new sorted position";
    EXPECT_EQ(doc.getMarkers()[1].id, a);
    // The move carries the marker's own fields with it.
    EXPECT_EQ(doc.getMarker(a)->text, "A");
    EXPECT_DOUBLE_EQ(doc.getMarker(a)->beat, 8.0);

    const auto revisionBefore = doc.getRevision();
    EXPECT_TRUE(doc.moveMarker(a, 8.0)) << "moving to where it already is reports success";
    EXPECT_EQ(doc.getRevision(), revisionBefore) << "...but bumps nothing";

    EXPECT_FALSE(doc.moveMarker(a, -0.5));
    EXPECT_FALSE(doc.moveMarker(a, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.moveMarker(MarkerId{999}, 1.0));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, MarkerSettersMutateOnceAndNoOpOnTheValueAlreadyThere) {
    const auto id = doc.addMarker(0.0, "Intro", 0xff112233);
    auto revision = doc.getRevision();
    auto calls = listener.calls;

    ASSERT_TRUE(doc.setMarkerText(id, "Verse"));
    EXPECT_EQ(doc.getMarker(id)->text, "Verse");
    EXPECT_EQ(doc.getRevision(), ++revision);
    EXPECT_EQ(listener.calls, ++calls);

    ASSERT_TRUE(doc.setMarkerText(id, "Verse")) << "the text it already has: success...";
    EXPECT_EQ(doc.getRevision(), revision) << "...with no revision bump";
    EXPECT_EQ(listener.calls, calls) << "...and no notification";

    ASSERT_TRUE(doc.setMarkerColour(id, 0xff445566));
    EXPECT_EQ(doc.getMarker(id)->colourArgb, 0xff445566u);
    EXPECT_EQ(doc.getRevision(), ++revision);
    EXPECT_EQ(listener.calls, ++calls);

    ASSERT_TRUE(doc.setMarkerColour(id, 0xff445566));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(listener.calls, calls);

    // An over-long label is refused and leaves the stored one alone — a rejected edit is not an
    // erase.
    EXPECT_FALSE(doc.setMarkerText(id, juce::String::repeatedString("y", TimelineDoc::kMaxMarkerTextLength + 1)));
    EXPECT_EQ(doc.getMarker(id)->text, "Verse");
    EXPECT_EQ(doc.getRevision(), revision);

    // Unknown ids are rejected outright on every setter — no mutation, no notification.
    EXPECT_FALSE(doc.setMarkerText(MarkerId{999}, "nope"));
    EXPECT_FALSE(doc.setMarkerColour(MarkerId{999}, 0xffffffff));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_EQ(listener.calls, calls);
}

TEST_F(TimelineDocTest, MarkerCapIsEnforced) {
    for (int i = 0; i < TimelineDoc::kMaxMarkers; ++i)
        ASSERT_TRUE(doc.addMarker((double)i, "m", 0xff000000).isValid()) << "at i = " << i;

    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(doc.addMarker(9999.0, "one too many", 0xff000000).isValid());
    EXPECT_EQ((int)doc.getMarkers().size(), TimelineDoc::kMaxMarkers);
    EXPECT_EQ(doc.getRevision(), revisionBefore) << "a refused add mutates nothing";
}

TEST_F(TimelineDocTest, ClearDropsMarkersAndIsANoOpWithNothingToDrop) {
    doc.addMarker(0.0, "Intro", 0xff000000);
    ASSERT_FALSE(doc.getMarkers().empty());
    EXPECT_TRUE(doc.isEmpty()) << "isEmpty() reports on TRACKS; a markers-only doc has no arrangement";

    const auto revisionBefore = doc.getRevision();
    doc.clear();
    EXPECT_TRUE(doc.getMarkers().empty());
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // Nothing left in EITHER container: clear() must not fire a second time.
    doc.clear();
    EXPECT_EQ(doc.getRevision(), revisionBefore + 1);

    // ...and a doc holding only markers is still something clear() has to notify about, which is
    // why the guard checks both containers rather than tracks alone.
    TimelineDoc markersOnly;
    CountingListener markerListener;
    markersOnly.addListener(&markerListener);
    markersOnly.addMarker(0.0, "Only", 0xff000000);
    markersOnly.clear();
    EXPECT_EQ(markerListener.calls, 2);
    EXPECT_TRUE(markersOnly.getMarkers().empty());
    markersOnly.removeListener(&markerListener);
}

TEST_F(TimelineDocTest, MarkersSurviveTheRoundTripWithTheirIdCounter) {
    const auto intro = doc.addMarker(0.0, "Intro", 0xff112233);
    doc.addMarker(8.5, "", 0xff445566);
    const auto before = dump(doc);

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    EXPECT_EQ(dump(loaded), before);

    ASSERT_EQ(loaded.getMarkers().size(), 2u);
    EXPECT_EQ(loaded.getMarkers()[0].id, intro);
    EXPECT_EQ(loaded.getMarkers()[0].text, "Intro");
    EXPECT_EQ(loaded.getMarkers()[0].colourArgb, 0xff112233u);
    EXPECT_DOUBLE_EQ(loaded.getMarkers()[1].beat, 8.5);
    EXPECT_EQ(loaded.getMarkers()[1].text, "") << "an empty label round-trips as an empty label";

    // The counter rode along, so a marker added after the load collides with nothing off the file.
    const auto added = loaded.addMarker(1.0, "After load", 0xff000000);
    ASSERT_TRUE(added.isValid());
    for (const auto& marker : loaded.getMarkers())
        if (!(marker.id == added))
            EXPECT_NE(marker.id, added);
    EXPECT_GT(added.value, doc.getMarkers().back().id.value);
}

TEST_F(TimelineDocTest, FromVarAcceptsADocumentWithNoMarkersKeyAtAll) {
    // Exactly what a file written before markers existed looks like: additive field, absent key.
    buildPopulatedDoc(doc);
    juce::var state = doc.toVar();
    ASSERT_NE(state.getDynamicObject(), nullptr);
    state.getDynamicObject()->removeProperty("markers");
    state.getDynamicObject()->removeProperty("nextMarkerId");

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(state)) << "a pre-markers file must still load";
    EXPECT_TRUE(loaded.getMarkers().empty());
    EXPECT_EQ(loaded.getTracks().size(), 2u) << "and everything else must be untouched";

    // The version did NOT move for an additive field.
    EXPECT_EQ(TimelineDoc::kFormatVersion, 1);
}

TEST_F(TimelineDocTest, FromVarRejectsMalformedMarkersWithoutTouchingTheDoc) {
    doc.addMarker(0.0, "Keep me", 0xff112233);
    const auto before = dump(doc);
    const auto revisionBefore = doc.getRevision();

    const auto markerState = [](const juce::var& markersValue) {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("version", TimelineDoc::kFormatVersion);
        root->setProperty("tracks", juce::Array<juce::var>());
        root->setProperty("markers", markersValue);
        return juce::var(root.get());
    };
    const auto markerVar = [](const std::function<void(juce::DynamicObject&)>& fill) {
        juce::DynamicObject::Ptr m = new juce::DynamicObject();
        m->setProperty("id", 1);
        fill(*m);
        juce::Array<juce::var> arr;
        arr.add(juce::var(m.get()));
        return juce::var(arr);
    };

    // "markers" is not an array.
    EXPECT_FALSE(doc.fromVar(markerState("not an array")));
    // A marker entry is not an object.
    EXPECT_FALSE(doc.fromVar(markerState(juce::var(juce::Array<juce::var>{juce::var(7)}))));
    // Missing / non-positive id.
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) { m.removeProperty("id"); }))));
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) { m.setProperty("id", 0); }))));
    // Bad beat / text / colour.
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) { m.setProperty("beat", -1.0); }))));
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) { m.setProperty("beat", "four"); }))));
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) { m.setProperty("text", 12); }))));
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) {
        m.setProperty("text", juce::String::repeatedString("z", TimelineDoc::kMaxMarkerTextLength + 1));
    }))));
    EXPECT_FALSE(doc.fromVar(markerState(markerVar([](juce::DynamicObject& m) { m.setProperty("colourArgb", -1); }))));
    EXPECT_FALSE(doc.fromVar(markerState(
        markerVar([](juce::DynamicObject& m) { m.setProperty("colourArgb", (juce::int64)0x1ffffffffLL); }))));

    // Duplicate marker ids.
    {
        juce::Array<juce::var> arr;
        for (int i = 0; i < 2; ++i) {
            juce::DynamicObject::Ptr m = new juce::DynamicObject();
            m->setProperty("id", 5);
            arr.add(juce::var(m.get()));
        }
        EXPECT_FALSE(doc.fromVar(markerState(juce::var(arr))));
    }

    // Over the cap.
    {
        juce::Array<juce::var> arr;
        for (int i = 0; i < TimelineDoc::kMaxMarkers + 1; ++i) {
            juce::DynamicObject::Ptr m = new juce::DynamicObject();
            m->setProperty("id", i + 1);
            arr.add(juce::var(m.get()));
        }
        EXPECT_FALSE(doc.fromVar(markerState(juce::var(arr))));
    }

    // All-or-nothing: every rejection above left the doc exactly as it was.
    EXPECT_EQ(dump(doc), before);
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

TEST_F(TimelineDocTest, FromVarRepairsMisOrderedMarkers) {
    juce::Array<juce::var> markers;
    for (const auto& entry : {std::make_pair(3, 12.0), std::make_pair(1, 4.0), std::make_pair(2, 0.0)}) {
        juce::DynamicObject::Ptr m = new juce::DynamicObject();
        m->setProperty("id", entry.first);
        m->setProperty("beat", entry.second);
        markers.add(juce::var(m.get()));
    }
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", TimelineDoc::kFormatVersion);
    root->setProperty("tracks", juce::Array<juce::var>());
    root->setProperty("markers", juce::var(markers));

    ASSERT_TRUE(doc.fromVar(juce::var(root.get())));
    ASSERT_EQ(doc.getMarkers().size(), 3u);
    EXPECT_DOUBLE_EQ(doc.getMarkers()[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(doc.getMarkers()[1].beat, 4.0);
    EXPECT_DOUBLE_EQ(doc.getMarkers()[2].beat, 12.0);

    // The counter is floored above the highest id in the file, however low the file claimed.
    const auto added = doc.addMarker(1.0, "next", 0xff000000);
    EXPECT_GT(added.value, 3);
}

TEST_F(TimelineDocTest, MarkerDefaultsAreTakenWhenTheFieldsAreAbsent) {
    juce::DynamicObject::Ptr m = new juce::DynamicObject();
    m->setProperty("id", 9);
    juce::Array<juce::var> markers;
    markers.add(juce::var(m.get()));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", TimelineDoc::kFormatVersion);
    root->setProperty("markers", juce::var(markers));

    ASSERT_TRUE(doc.fromVar(juce::var(root.get())));
    ASSERT_EQ(doc.getMarkers().size(), 1u);
    const auto& loaded = doc.getMarkers().front();
    EXPECT_DOUBLE_EQ(loaded.beat, Marker{}.beat);
    EXPECT_EQ(loaded.text, Marker{}.text);
    EXPECT_EQ(loaded.colourArgb, Marker{}.colourArgb);
}
