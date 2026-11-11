// Sections in the timeline model: making a section, placing it again, and every edit inside one placement
// reaching all the others in the same mutation.

#include "TimelineDocSectionTestHelpers.h"

using synth::Section;

TEST_F(TimelineDocTest, FreshDocHasNoSections) {
    EXPECT_TRUE(doc.getSections().empty());
    EXPECT_TRUE(doc.getPlacements().empty());
    EXPECT_EQ(doc.getSection(SectionId{}), nullptr);
    EXPECT_EQ(doc.placementAt(0.0), nullptr);
}

TEST_F(TimelineDocTest, MakeSectionSplitsCrossingMidiAndAudioClips) {
    const auto midi = doc.addTrack(TrackKind::Midi, "M");
    const auto audio = doc.addTrack(TrackKind::Audio, "Au");
    const auto m = doc.addClip(midi, 2.0, 8.0, "m");
    doc.addNote(m, makeNote(1.0, 60));
    doc.addNote(m, makeNote(6.0, 62));
    const auto au = doc.addClip(audio, 2.0, 8.0, "au");
    ASSERT_TRUE(doc.setClipAsset(au, "a.wav", 1.0));

    const auto section = doc.makeSection(4.0, 8.0, "  Chorus ", 0xff00ff00, kSpb);
    ASSERT_TRUE(section.isValid());
    EXPECT_EQ(doc.getSection(section)->name, "Chorus") << "the name is trimmed";
    EXPECT_DOUBLE_EQ(doc.getSection(section)->lengthBeats, 4.0);
    ASSERT_EQ(doc.getPlacements().size(), 1u);
    EXPECT_DOUBLE_EQ(doc.getPlacements()[0].startBeat, 4.0);

    EXPECT_EQ(startsOn(doc, midi), (std::vector<double>{2.0, 4.0, 8.0}));
    EXPECT_EQ(startsOn(doc, audio), (std::vector<double>{2.0, 4.0, 8.0}));
    const auto inside = clipsOn(doc, audio, 4.0, 8.0);
    ASSERT_EQ(inside.size(), 1u);
    EXPECT_DOUBLE_EQ(inside[0]->sourceStartSeconds, 1.0 + 2.0 * kSpb) << "the right half reads on from the cut";
    EXPECT_DOUBLE_EQ(clipsOn(doc, audio, 8.0, 9.0)[0]->sourceStartSeconds, 1.0 + 6.0 * kSpb);
}

TEST_F(TimelineDocTest, MakeSectionRejectsBadInputWithoutMutating) {
    doc.addTrack(TrackKind::Midi, "M");
    ASSERT_TRUE(doc.makeSection(0.0, 8.0, "A", 0, kSpb).isValid());
    const auto revision = doc.getRevision();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(doc.makeSection(4.0, 12.0, "Overlap", 0, kSpb).isValid());
    EXPECT_FALSE(doc.makeSection(-1.0, 4.0, "Neg", 0, kSpb).isValid());
    EXPECT_FALSE(doc.makeSection(8.0, 8.0, "Empty", 0, kSpb).isValid());
    EXPECT_FALSE(doc.makeSection(8.0, nan, "NaN", 0, kSpb).isValid());
    EXPECT_FALSE(doc.makeSection(8.0, 12.0, "   ", 0, kSpb).isValid());
    EXPECT_FALSE(doc.makeSection(8.0, 12.0, juce::String::repeatedString("x", 129), 0, kSpb).isValid());
    EXPECT_FALSE(doc.makeSection(8.0, 12.0, "X", 0, 0.0).isValid());
    EXPECT_TRUE(doc.makeSection(8.0, 12.0, "Touching", 0, kSpb).isValid()) << "touching is fine";
    EXPECT_EQ(doc.getRevision(), revision + 1);
}

TEST_F(TimelineDocTest, PlaceSectionMakeRoomMovesEverythingLaterAndCopiesTheMusic) {
    const auto song = buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0xff112233, kSpb);
    const auto p2 = doc.placeSection(verse, 8.0, SectionDropMode::MakeRoom, kSpb);
    const auto p3 = doc.placeSection(verse, 16.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_TRUE(p2.isValid());
    ASSERT_TRUE(p3.isValid());

    ASSERT_EQ(doc.getPlacements().size(), 3u);
    EXPECT_EQ(startsOn(doc, song.a), (std::vector<double>{0, 4, 8, 12, 16, 20, 24}));
    EXPECT_EQ(startsOn(doc, song.b), (std::vector<double>{0, 8, 16}));
    EXPECT_DOUBLE_EQ(doc.getMarker(song.tailMarker)->beat, 24.0);
    EXPECT_EQ(pointBeats(doc, song.lane), (std::vector<double>{1, 5, 9, 13, 17, 21, 25}));
    EXPECT_EQ(doc.getClip(song.tail)->startBeat, 24.0);
    EXPECT_TRUE(placementsAgree(doc, verse));
    EXPECT_EQ(doc.placementOrdinal(p3), 3);
    EXPECT_EQ(doc.placementAt(9.0)->id, p2);
    EXPECT_EQ(doc.placementAt(24.0), nullptr);
    const auto* copy = clipsOn(doc, song.a, 8.0, 9.0).front();
    ASSERT_EQ(copy->notes.size(), 2u);
    EXPECT_EQ(copy->name, "c0");
    EXPECT_EQ(doc.placementForClip(copy->id)->id, p2);
}

TEST_F(TimelineDocTest, PlaceSectionInsideAnotherPlacementCutsItOutOfTheTimeline) {
    const auto song = buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb);
    const auto chorus = doc.makeSection(8.0, 12.0, "Chorus", 0, kSpb);
    ASSERT_TRUE(doc.placeSection(chorus, 4.0, SectionDropMode::MakeRoom, kSpb).isValid());
    EXPECT_TRUE(doc.getSection(verse) == nullptr) << "its only placement was cut, so the section went";
    (void)song;
}

TEST_F(TimelineDocTest, PlaceSectionCoverReplacesWhatWasThereAndDropsAnOverlappedPlacement) {
    const auto song = buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb);
    const auto chorus = doc.makeSection(8.0, 12.0, "Chorus", 0, kSpb);
    const auto other = doc.addClip(song.b, 20.0, 4.0, "far");
    // Cover [6, 14): clips of the verse's tail and the chorus are replaced; the chorus placement overlaps and goes.
    const auto placed = doc.placeSection(verse, 6.0, SectionDropMode::Cover, kSpb);
    ASSERT_TRUE(placed.isValid());
    EXPECT_TRUE(doc.getSection(chorus) == nullptr);
    EXPECT_EQ(doc.getClip(other)->startBeat, 20.0) << "nothing after the cover moves";
    ASSERT_EQ(doc.placementsOf(verse).size(), 1u) << "the verse's own first placement overlaps the drop too";
    EXPECT_DOUBLE_EQ(doc.getPlacement(placed)->startBeat, 6.0);
    EXPECT_EQ(doc.getClip(song.tail), nullptr) << "the clip under the cover is gone";
    EXPECT_EQ(startsOn(doc, song.a), (std::vector<double>{0, 4, 6, 10}));
    EXPECT_EQ(pointBeats(doc, song.lane), (std::vector<double>{1, 5, 7, 11}))
        << "the point under the cover (beat 9) is replaced by the copied ones";
}

TEST_F(TimelineDocTest, EveryEditInsideOnePlacementReachesTheOthersInOneRevision) {
    auto t = buildThreeVerses(doc);
    const auto& song = t.song;
    const auto inP2 = [&](TrackId track, size_t i) { return clipsOn(doc, track, 8.0, 16.0).at(i); };

    const auto expectOneBump = [&](const char* what, const std::function<void()>& edit) {
        const auto before = doc.getRevision();
        const auto calls = listener.calls;
        const auto stamps = doc.getSectionStampRevision();
        edit();
        EXPECT_EQ(doc.getRevision(), before + 1) << what;
        EXPECT_EQ(listener.calls, calls + 1) << what;
        EXPECT_EQ(doc.getSectionStampRevision(), stamps + 1) << what;
        EXPECT_EQ(doc.getLastStampedPlacements().size(), 2u) << what;
        EXPECT_TRUE(placementsAgree(doc, t.verse)) << what;
    };

    expectOneBump("moveNote", [&] { doc.moveNote(inP2(song.a, 0)->notes[1].id, 3.0, 65); });
    expectOneBump("addNote", [&] { doc.addNote(inP2(song.a, 0)->id, makeNote(1.0, 70)); });
    expectOneBump("removeNote", [&] { doc.removeNote(inP2(song.a, 0)->notes[0].id); });
    expectOneBump("resizeClip", [&] { doc.resizeClip(inP2(song.a, 1)->id, 3.0); });
    expectOneBump("addClip", [&] { doc.addClip(song.b, 9.0, 1.0, "extra"); });
    expectOneBump("moveClip", [&] { doc.moveClip(inP2(song.a, 1)->id, 5.0 + 8.0); });
    expectOneBump("removeClip", [&] { doc.removeClip(inP2(song.b, 0)->id); });
    expectOneBump("editBreakpoints", [&] { doc.editBreakpoints(song.lane, {9.0}, {{10.0, 555.0, 0.0f, 1}}); });

    // Edit the LAST placement: still copied to the first two.
    expectOneBump("edit in placement 3", [&] { doc.setClipName(clipsOn(doc, song.a, 16.0, 17.0)[0]->id, "renamed"); });
    EXPECT_EQ(clipsOn(doc, song.a, 0.0, 1.0)[0]->name, "renamed");
    // Edits outside every placement stamp nothing.
    const auto stamps = doc.getSectionStampRevision();
    doc.setClipName(song.tail, "tail2");
    EXPECT_EQ(doc.getSectionStampRevision(), stamps);
}

TEST_F(TimelineDocTest, WhenSeveralPlacementsChangeTheLowestStartWins) {
    auto t = buildThreeVerses(doc);
    {
        TimelineDoc::SectionEditScope scope(doc);
        doc.setClipName(clipsOn(doc, t.song.a, 16.0, 17.0)[0]->id, "late");
        doc.setClipName(clipsOn(doc, t.song.a, 8.0, 9.0)[0]->id, "early");
    }
    for (const double start : {0.0, 8.0, 16.0})
        EXPECT_EQ(clipsOn(doc, t.song.a, start, start + 1.0)[0]->name, "early");
}

TEST_F(TimelineDocTest, ScopeRunsNoSyncAndNoRevisionWhenNothingChanged) {
    auto t = buildThreeVerses(doc);
    const auto revision = doc.getRevision();
    {
        TimelineDoc::SectionEditScope scope(doc);
    }
    EXPECT_EQ(doc.getRevision(), revision);
}

TEST_F(TimelineDocTest, MovePlacementMakeRoomKeepsTheIdAndTheMusic) {
    auto t = buildThreeVerses(doc);
    const auto& song = t.song;
    ASSERT_TRUE(doc.movePlacement(t.p1, 16.0, SectionDropMode::MakeRoom, kSpb));
    // p1 left [0,8) and the gap closed; 16 is the start it gets, the end of the third placement once the gap closed.
    ASSERT_EQ(doc.getPlacements().size(), 3u);
    EXPECT_EQ(doc.getPlacements()[2].id, t.p1);
    EXPECT_DOUBLE_EQ(doc.getPlacements()[2].startBeat, 16.0);
    EXPECT_DOUBLE_EQ(doc.getPlacements()[0].startBeat, 0.0);
    EXPECT_TRUE(placementsAgree(doc, t.verse));
    EXPECT_EQ(startsOn(doc, song.a), (std::vector<double>{0, 4, 8, 12, 16, 20, 24}));
    EXPECT_EQ(doc.getClip(song.tail)->startBeat, 24.0);
    EXPECT_TRUE(doc.movePlacement(t.p1, 16.0, SectionDropMode::MakeRoom, kSpb)) << "same spot is a no-op";
}

TEST_F(TimelineDocTest, MovePlacementCoverLeavesAGapAndOverwritesTheTarget) {
    TimelineDoc& d = doc;
    const auto song = buildSectionSong(d);
    const auto verse = d.makeSection(0.0, 8.0, "Verse", 0, kSpb);
    const auto chorus = d.makeSection(8.0, 12.0, "Chorus", 0, kSpb);
    const auto p = d.placementsOf(verse).front();
    ASSERT_TRUE(d.movePlacement(p, 10.0, SectionDropMode::Cover, kSpb));
    EXPECT_EQ(d.getPlacement(p)->startBeat, 10.0);
    EXPECT_EQ(d.getSection(chorus), nullptr) << "the chorus placement was under the drop";
    EXPECT_EQ(startsOn(d, song.a), (std::vector<double>{8.0, 10.0, 14.0}))
        << "old span cleared, gap kept; the tail clip is trimmed back to the drop";
    EXPECT_EQ(pointBeats(d, song.lane), (std::vector<double>{9.0, 11.0, 15.0}));
}

TEST_F(TimelineDocTest, MovingTheOnlyPlacementKeepsTheSection) {
    buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb);
    const auto p = doc.placementsOf(verse).front();
    ASSERT_TRUE(doc.movePlacement(p, 20.0, SectionDropMode::MakeRoom, kSpb));
    ASSERT_NE(doc.getSection(verse), nullptr);
    EXPECT_EQ(doc.getPlacement(p)->id, p);
    ASSERT_TRUE(doc.movePlacement(p, 2.0, SectionDropMode::Cover, kSpb));
    ASSERT_NE(doc.getSection(verse), nullptr);
}

TEST_F(TimelineDocTest, DeletePlacementAndBarsClosesTheGap) {
    auto t = buildThreeVerses(doc);
    ASSERT_TRUE(doc.deletePlacementAndBars(t.p2, kSpb));
    EXPECT_EQ(doc.getPlacements().size(), 2u);
    EXPECT_DOUBLE_EQ(doc.getPlacements()[1].startBeat, 8.0);
    EXPECT_EQ(startsOn(doc, t.song.a), (std::vector<double>{0, 4, 8, 12, 16}));
    EXPECT_DOUBLE_EQ(doc.getMarker(t.song.tailMarker)->beat, 16.0);
    EXPECT_EQ(pointBeats(doc, t.song.lane), (std::vector<double>{1, 5, 9, 13, 17}));
    EXPECT_TRUE(placementsAgree(doc, t.verse));
}

TEST_F(TimelineDocTest, RemovePlacementKeepMusicKeepsTheClipsAndTheLastOneTakesTheSection) {
    auto t = buildThreeVerses(doc);
    const auto clips = startsOn(doc, t.song.a);
    ASSERT_TRUE(doc.removePlacementKeepMusic(t.p2));
    EXPECT_EQ(startsOn(doc, t.song.a), clips);
    ASSERT_TRUE(doc.removePlacementKeepMusic(t.p1));
    ASSERT_TRUE(doc.removePlacementKeepMusic(t.p3));
    EXPECT_TRUE(doc.getSections().empty()) << "a section with no placement is removed";
    EXPECT_EQ(startsOn(doc, t.song.a), clips);
    EXPECT_FALSE(doc.removePlacementKeepMusic(t.p3));
}

TEST_F(TimelineDocTest, MakePlacementUniqueCutsTheLinkBothWays) {
    auto t = buildThreeVerses(doc);
    EXPECT_FALSE(doc.makePlacementUnique(t.p2, "", 0).isValid());
    const auto unique = doc.makePlacementUnique(t.p2, "Verse B", 0xff445566);
    ASSERT_TRUE(unique.isValid());
    EXPECT_EQ(doc.getPlacement(t.p2)->section, unique);
    EXPECT_EQ(doc.getSection(unique)->name, "Verse B");
    EXPECT_DOUBLE_EQ(doc.getSection(unique)->lengthBeats, 8.0);

    doc.setClipName(clipsOn(doc, t.song.a, 8.0, 9.0)[0]->id, "B only");
    EXPECT_EQ(clipsOn(doc, t.song.a, 0.0, 1.0)[0]->name, "c0");
    EXPECT_EQ(clipsOn(doc, t.song.a, 16.0, 17.0)[0]->name, "c0");
    doc.setClipName(clipsOn(doc, t.song.a, 0.0, 1.0)[0]->id, "A only");
    EXPECT_EQ(clipsOn(doc, t.song.a, 16.0, 17.0)[0]->name, "A only");
    EXPECT_EQ(clipsOn(doc, t.song.a, 8.0, 9.0)[0]->name, "B only");

    // The last placement of a section cannot be made unique.
    ASSERT_TRUE(doc.removePlacementKeepMusic(t.p3));
    EXPECT_FALSE(doc.makePlacementUnique(t.p1, "Solo", 0).isValid());
}

TEST_F(TimelineDocTest, RenameAndRecolourAreNoOpsWhenEqual) {
    buildSectionSong(doc);
    const auto verse = doc.makeSection(0.0, 8.0, "Verse", 0xff112233, kSpb);
    const auto revision = doc.getRevision();
    EXPECT_TRUE(doc.renameSection(verse, "Verse"));
    EXPECT_TRUE(doc.setSectionColour(verse, 0xff112233));
    EXPECT_EQ(doc.getRevision(), revision);
    EXPECT_TRUE(doc.renameSection(verse, " Hook "));
    EXPECT_EQ(doc.getSection(verse)->name, "Hook");
    EXPECT_FALSE(doc.renameSection(verse, "  "));
    EXPECT_TRUE(doc.setSectionColour(verse, 0xff000001));
    EXPECT_FALSE(doc.renameSection(SectionId{99}, "x"));
}

TEST_F(TimelineDocTest, MakeSectionsFromMarkersJoinsIdenticalRepeatsAndSplitsDifferingOnes) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    for (const double start : {0.0, 8.0, 16.0}) {
        const auto clip = doc.addClip(a, start, 4.0, "c");
        doc.addNote(clip, makeNote(0.0, 60));
    }
    // Third "Chorus" differs: its note is another pitch. A fourth span has an empty label.
    const auto third = clipsOn(doc, a, 16.0, 17.0)[0]->id;
    doc.moveNote(doc.getClip(third)->notes[0].id, 0.0, 61);
    doc.addMarker(0.0, "Chorus", 0);
    doc.addMarker(8.0, "Chorus", 0);
    doc.addMarker(16.0, "Chorus", 0);
    doc.addMarker(24.0, "", 0);
    doc.addClip(a, 24.0, 4.0, "x");

    int calls = 0;
    const int made = doc.makeSectionsFromMarkers(
        doc.getArrangementEndBeat(), [&](int) { return static_cast<juce::uint32>(0xff000000 + ++calls); }, kSpb);
    EXPECT_EQ(made, 4);
    ASSERT_EQ(doc.getSections().size(), 3u);
    EXPECT_EQ(doc.getSections()[0].name, "Chorus");
    EXPECT_EQ(doc.getSections()[1].name, "Chorus 2");
    EXPECT_EQ(doc.getSections()[2].name, "Section 3");
    EXPECT_EQ(doc.placementsOf(doc.getSections()[0].id).size(), 2u);
    EXPECT_EQ(doc.getMarkers().size(), 4u) << "markers are kept";
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(doc.makeSectionsFromMarkers(doc.getArrangementEndBeat(), {}, kSpb), 0)
        << "spans already placed are skipped";
}

TEST_F(TimelineDocTest, EditingALinkedClipFromOutsideAPlacementReachesEveryPlacement) {
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto c0 = doc.addClip(a, 0.0, 4.0, "c0");
    doc.addNote(c0, makeNote(0.0, 60));
    const auto verse = doc.makeSection(0.0, 4.0, "Verse", 0, kSpb);
    const auto p2 = doc.placeSection(verse, 4.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_TRUE(p2.isValid());
    // A linked partner far outside any section.
    const auto outside = doc.duplicateClipLinked(clipsOn(doc, a, 4.0, 5.0)[0]->id);
    ASSERT_TRUE(outside.isValid());
    doc.moveClip(outside, 40.0);
    ASSERT_GT(doc.countLinkedClips(outside), 0);

    doc.addNote(outside, makeNote(2.0, 72));
    EXPECT_EQ(doc.getClip(clipsOn(doc, a, 4.0, 5.0)[0]->id)->notes.size(), 2u);
    EXPECT_EQ(clipsOn(doc, a, 0.0, 1.0)[0]->notes.size(), 2u) << "placement 1 follows placement 2's linked change";
    EXPECT_TRUE(placementsAgree(doc, verse));
}

TEST_F(TimelineDocTest, SameClipContentSeesEveryField) {
    const Clip base;
    EXPECT_TRUE(sameClipContent(base, base));
    const auto differs = [&](const char* field, const std::function<void(Clip&)>& change) {
        Clip other;
        change(other);
        EXPECT_FALSE(sameClipContent(base, other)) << field << " is not compared";
    };
    differs("name", [](Clip& c) { c.name = "x"; });
    differs("startBeat", [](Clip& c) { c.startBeat = 1.0; });
    differs("lengthBeats", [](Clip& c) { c.lengthBeats = 5.0; });
    differs("notes", [](Clip& c) { c.notes.push_back(MidiNote{}); });
    differs("assetRef", [](Clip& c) { c.assetRef = "a.wav"; });
    differs("gainDb", [](Clip& c) { c.gainDb = 1.0; });
    differs("fadeInBeats", [](Clip& c) { c.fadeInBeats = 1.0; });
    differs("fadeOutBeats", [](Clip& c) { c.fadeOutBeats = 1.0; });
    differs("sourceStartSeconds", [](Clip& c) { c.sourceStartSeconds = 1.0; });
    differs("muted", [](Clip& c) { c.muted = true; });
    differs("stepsPerBar", [](Clip& c) { c.stepsPerBar = 32; });
    differs("swing", [](Clip& c) { c.swing = 0.6; });
    differs("humanise", [](Clip& c) { c.humanise = 0.5; });
    differs("drumRows", [](Clip& c) { c.drumRows.push_back({36, 8, -1.0}); });
    differs("mutedPitches", [](Clip& c) { c.mutedPitches = {36}; });
    differs("soloPitches", [](Clip& c) { c.soloPitches = {36}; });
    differs("scale", [](Clip& c) { c.scale = synth::MusicalScale{}; });
    differs("scaleVisible", [](Clip& c) { c.scaleVisible = true; });
    differs("noteRange", [](Clip& c) { c.noteRange = std::make_pair(10, 20); });
    differs("stretchOrigin", [](Clip& c) { c.stretchOrigin = Clip::StretchOrigin{"o.wav", 0.0, 2.0}; });

    // Ids and link groups are not music.
    Clip other;
    other.id = ClipId{7};
    other.linkGroup = 3;
    EXPECT_TRUE(sameClipContent(base, other));

    // Every note field.
    const auto noteDiffers = [&](const char* field, const std::function<void(MidiNote&)>& change) {
        Clip a, b;
        a.notes.push_back(MidiNote{});
        b.notes.push_back(MidiNote{});
        change(b.notes[0]);
        EXPECT_FALSE(sameClipContent(a, b)) << "note." << field << " is not compared";
    };
    noteDiffers("startBeat", [](MidiNote& n) { n.startBeat = 1.0; });
    noteDiffers("lengthBeats", [](MidiNote& n) { n.lengthBeats = 2.0; });
    noteDiffers("pitch", [](MidiNote& n) { n.pitch = 61; });
    noteDiffers("velocity", [](MidiNote& n) { n.velocity = 90; });
    noteDiffers("channel", [](MidiNote& n) { n.channel = 2; });
    noteDiffers("muted", [](MidiNote& n) { n.muted = true; });
    noteDiffers("chance", [](MidiNote& n) { n.chance = 50; });
    noteDiffers("repeat", [](MidiNote& n) { n.repeat = 2; });
    noteDiffers("flam", [](MidiNote& n) { n.flam = true; });
    noteDiffers("nudge", [](MidiNote& n) { n.nudge = 0.25; });
    noteDiffers("condHit", [](MidiNote& n) {
        n.condOf = 2;
        n.condHit = 2;
    });
    noteDiffers("condOf", [](MidiNote& n) { n.condOf = 2; });
}

TEST_F(TimelineDocTest, InsertAndDeleteTimeMoveTheWholeSong) {
    const auto song = buildSectionSong(doc);
    ASSERT_TRUE(doc.insertTime(6.0, 2.0, kSpb));
    EXPECT_EQ(startsOn(doc, song.a), (std::vector<double>{0, 4, 8, 10}));
    EXPECT_EQ(pointBeats(doc, song.lane), (std::vector<double>{1, 5, 11}));
    EXPECT_DOUBLE_EQ(doc.getMarker(song.tailMarker)->beat, 10.0);
    ASSERT_TRUE(doc.deleteTime(6.0, 8.0, kSpb));
    EXPECT_EQ(startsOn(doc, song.a), (std::vector<double>{0, 4, 6, 8}));
    EXPECT_FALSE(doc.insertTime(-1.0, 2.0, kSpb));
    EXPECT_FALSE(doc.deleteTime(4.0, 4.0, kSpb));
    EXPECT_FALSE(doc.insertTime(1.0, 2.0, 0.0));
}
