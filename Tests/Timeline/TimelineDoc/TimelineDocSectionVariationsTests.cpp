// Section variations in the timeline model: which tracks a variation owns, how edits reach (or skip) placements,
// switching, removing, the file format and the audio files an unplayed variation keeps alive.

#include "AppUndoManager.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include "Timeline/TimelineDoc/SectionVariationLetter.h"
#include "Timeline/TimelineValidator.h"
#include "TimelineDocSectionTestHelpers.h"
#include <juce_audio_processors/juce_audio_processors.h>

using synth::TimelineValidationError;

namespace {

juce::String leadName(const TimelineDoc& doc, TrackId lead, double start) {
    const auto clips = clipsOn(doc, lead, start, start + 1.0);
    return clips.empty() ? juce::String("<none>") : clips[0]->name;
}

// Three verses (lead track "A", drums-like track "B"); the third plays a new variation B that owns only the lead.
class VariationTest : public TimelineDocTest {
protected:
    ThreeVerses t;
    int letter = -1;

    void SetUp() override {
        TimelineDocTest::SetUp();
        t = buildThreeVerses(doc);
        letter = doc.addVariationForTrack(clipsOn(doc, t.song.a, 16.0, 17.0)[0]->id);
    }
    void editLead(double start, const juce::String& name) {
        ASSERT_TRUE(doc.setClipName(clipsOn(doc, t.song.a, start, start + 1.0)[0]->id, name));
    }
    void editBass(double start, const juce::String& name) {
        ASSERT_TRUE(doc.setClipName(clipsOn(doc, t.song.b, start, start + 1.0)[0]->id, name));
    }
};

// Two plain MIDI tracks, a four-beat section played twice, and a variation B owning track M that no placement plays.
struct UnplayedB {
    TimelineDoc doc;
    TrackId m;
    SectionId section;
    PlacementId first, second;

    UnplayedB() {
        m = doc.addTrack(TrackKind::Midi, "M");
        doc.addTrack(TrackKind::Midi, "N");
        const auto clip = doc.addClip(m, 0.0, 4.0, "c");
        doc.addNote(clip, makeNote(1.0, 60));
        section = doc.makeSection(0.0, 4.0, "S", 0, kSpb);
        second = doc.placeSection(section, 4.0, SectionDropMode::MakeRoom, kSpb);
        first = doc.placementsOf(section).front();
        doc.addVariationForTrack(doc.getTrack(m)->clips[1].id);
        doc.setClipName(doc.getTrack(m)->clips[1].id, "bee");
        doc.setPlacementVariation(second, 0);
    }
};

} // namespace

TEST_F(VariationTest, ANewVariationOfAPartOwnsOnlyThatTrackAndEditsFollowTheOwnership) {
    ASSERT_EQ(letter, 1);
    EXPECT_EQ(synth::variationLetter(letter), "B");
    EXPECT_EQ(doc.getPlacement(t.p3)->variation, 1);
    const auto* b = doc.getVariation(t.verse, 1);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->ownTracks, (std::set<std::int64_t>{t.song.a.value}));

    editLead(16.0, "B lead");
    EXPECT_EQ(leadName(doc, t.song.a, 0.0), "c0");
    EXPECT_EQ(leadName(doc, t.song.a, 8.0), "c0") << "the A placements keep their lead";
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "B lead");

    editBass(16.0, "shared bass");
    EXPECT_EQ(clipsOn(doc, t.song.b, 0.0, 1.0)[0]->name, "shared bass");
    EXPECT_EQ(clipsOn(doc, t.song.b, 8.0, 9.0)[0]->name, "shared bass")
        << "a shared track still reaches every placement";
}

TEST_F(VariationTest, EditingTheLeadInAnAPlacementDoesNotTouchB) {
    editLead(16.0, "B lead");
    editLead(0.0, "A lead");
    EXPECT_EQ(leadName(doc, t.song.a, 8.0), "A lead");
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "B lead");
}

TEST_F(VariationTest, SwitchingAPlacementRewritesOnlyThatPlacementAndTheStoredMusicSurvives) {
    editLead(16.0, "B lead");
    const auto beforeA = clipsOn(doc, t.song.a, 0.0, 8.0).size();
    const auto revision = doc.getRevision();

    ASSERT_TRUE(doc.setPlacementVariation(t.p3, 0));
    EXPECT_EQ(doc.getRevision(), revision + 1);
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "c0");
    EXPECT_EQ(leadName(doc, t.song.a, 0.0), "c0");
    ASSERT_EQ(doc.getVariation(t.verse, 1)->stored.clips.at(t.song.a.value)[0].name, "B lead")
        << "B is played by nobody and keeps its music";

    ASSERT_TRUE(doc.setPlacementVariation(t.p3, 1));
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "B lead");
    EXPECT_EQ(clipsOn(doc, t.song.a, 0.0, 8.0).size(), beforeA);
    EXPECT_EQ(leadName(doc, t.song.a, 8.0), "c0");
    EXPECT_FALSE(doc.setPlacementVariation(t.p3, 1)) << "already playing it";
    EXPECT_FALSE(doc.setPlacementVariation(t.p3, 9)) << "no such variation";
}

TEST_F(VariationTest, PlayVariationEverywhereMakesEveryPlacementIdentical) {
    editLead(16.0, "B lead");
    ASSERT_TRUE(doc.playVariationEverywhere(t.verse, 1));
    for (const auto& p : doc.getPlacements())
        EXPECT_EQ(p.variation, 1);
    EXPECT_EQ(leadName(doc, t.song.a, 0.0), "B lead");
    EXPECT_EQ(leadName(doc, t.song.a, 8.0), "B lead");
    EXPECT_TRUE(placementsAgree(doc, t.verse));
    EXPECT_FALSE(doc.playVariationEverywhere(t.verse, 1)) << "nothing left to switch";

    ASSERT_TRUE(doc.playVariationEverywhere(t.verse, 0));
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "c0") << "A's music was kept while nobody played it";
    EXPECT_TRUE(placementsAgree(doc, t.verse));
}

TEST_F(VariationTest, ACopyVariationOwnsEveryTrackAndEditsStayInIt) {
    const auto letterC = doc.addVariation(t.p2);
    ASSERT_EQ(letterC, 2);
    const auto* c = doc.getVariation(t.verse, 2);
    EXPECT_EQ(c->ownTracks, (std::set<std::int64_t>{t.song.a.value, t.song.b.value}));
    EXPECT_TRUE(placementsAgree(doc, t.verse)) << "no clip was written";

    editBass(8.0, "C bass");
    EXPECT_EQ(clipsOn(doc, t.song.b, 0.0, 1.0)[0]->name, "bass");
    EXPECT_EQ(clipsOn(doc, t.song.b, 16.0, 17.0)[0]->name, "bass");
    editBass(0.0, "A bass");
    EXPECT_EQ(clipsOn(doc, t.song.b, 8.0, 9.0)[0]->name, "C bass");
}

TEST_F(VariationTest, RemovingAVariationSendsItsPlacementsBackToAAndRejectsA) {
    editLead(16.0, "B lead");
    EXPECT_FALSE(doc.removeVariation(t.verse, 0));
    EXPECT_FALSE(doc.removeVariation(t.verse, 5));
    ASSERT_TRUE(doc.removeVariation(t.verse, 1));
    EXPECT_EQ(doc.getVariation(t.verse, 1), nullptr);
    EXPECT_EQ(doc.getPlacement(t.p3)->variation, 0);
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "c0");
    EXPECT_EQ(doc.addVariation(t.p1), 1) << "the lowest free letter is used again";
}

TEST_F(VariationTest, PlaceSectionWithALetterPlaysThatVariation) {
    editLead(16.0, "B lead");
    const auto placed = doc.placeSection(t.verse, 24.0, SectionDropMode::MakeRoom, kSpb, 1);
    ASSERT_TRUE(placed.isValid());
    EXPECT_EQ(doc.getPlacement(placed)->variation, 1);
    EXPECT_EQ(leadName(doc, t.song.a, 24.0), "B lead");
    EXPECT_EQ(clipsOn(doc, t.song.b, 24.0, 25.0)[0]->name, "bass");
    EXPECT_FALSE(doc.placeSection(t.verse, 40.0, SectionDropMode::MakeRoom, kSpb, 7).isValid());

    // The new placement joins B's group: an edit there reaches the other B placement's lead.
    editLead(24.0, "both B");
    EXPECT_EQ(leadName(doc, t.song.a, 16.0), "both B");
}

TEST_F(VariationTest, RemovingATrackDropsItFromOwnTracksAndStoredMusic) {
    editLead(16.0, "B lead");
    ASSERT_TRUE(doc.setPlacementVariation(t.p3, 0));
    ASSERT_TRUE(doc.getVariation(t.verse, 1)->stored.clips.count(t.song.a.value));
    ASSERT_TRUE(doc.removeTrack(t.song.a));
    const auto* b = doc.getVariation(t.verse, 1);
    EXPECT_TRUE(b->ownTracks.empty());
    EXPECT_TRUE(b->stored.clips.empty());
    EXPECT_TRUE(b->stored.points.empty());
    EXPECT_TRUE(doc.getVariation(t.verse, 0)->stored.clips.count(t.song.a.value) == 0);
}

TEST_F(VariationTest, RemovingALaneDropsItsStoredPoints) {
    ASSERT_TRUE(doc.addVariation(t.p3) == 2);
    ASSERT_TRUE(doc.setPlacementVariation(t.p3, 0));
    ASSERT_TRUE(doc.getVariation(t.verse, 2)->stored.points.count(t.song.lane.value));
    ASSERT_TRUE(doc.removeLane(t.song.lane));
    EXPECT_TRUE(doc.getVariation(t.verse, 2)->stored.points.empty());
}

TEST_F(VariationTest, TheSwitchAndItsRedoAreOneUndoStepEach) {
    AppUndoManager undo;
    editLead(16.0, "B lead");
    const auto before = dump(doc);
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { doc.setPlacementVariation(t.p3, 0); }));
    const auto after = dump(doc);
    EXPECT_NE(before, after);
    undo.undo();
    EXPECT_EQ(dump(doc), before);
    undo.redo();
    EXPECT_EQ(dump(doc), after);
}

TEST_F(VariationTest, TheCapIs26VariationsAndRejectsLeaveTheDocAlone) {
    for (int i = 2; i < TimelineDoc::kMaxVariations; ++i)
        ASSERT_EQ(doc.addVariation(t.p1), i);
    EXPECT_EQ(doc.getSection(t.verse)->variations.size(), 26u);
    const auto before = dump(doc);
    const auto revision = doc.getRevision();
    EXPECT_EQ(doc.addVariation(t.p1), -1);
    EXPECT_EQ(doc.addVariationForTrack(clipsOn(doc, t.song.a, 0.0, 1.0)[0]->id), -1);
    EXPECT_EQ(doc.addVariation(PlacementId{9999}), -1);
    EXPECT_EQ(doc.addVariationForTrack(clipsOn(doc, t.song.a, 24.0, 30.0).empty()
                                           ? ClipId{9999}
                                           : clipsOn(doc, t.song.a, 24.0, 30.0)[0]->id),
              -1)
        << "a clip outside every placement";
    EXPECT_EQ(dump(doc), before);
    EXPECT_EQ(doc.getRevision(), revision);
}

TEST_F(VariationTest, MakePlacementUniqueTakesTheCurrentMusicAsItsA) {
    editLead(16.0, "B lead");
    const auto unique = doc.makePlacementUnique(t.p3, "Solo", 0);
    ASSERT_TRUE(unique.isValid());
    EXPECT_EQ(doc.getSection(unique)->variations.size(), 1u);
    EXPECT_EQ(doc.getPlacement(t.p3)->variation, 0);
    EXPECT_EQ(doc.getVariation(unique, 0)->stored.clips.at(t.song.a.value)[0].name, "B lead");
    EXPECT_EQ(doc.getSection(t.verse)->variations.size(), 2u) << "the old section keeps its variations";
}

TEST(TimelineDocSectionVariationFile, AnUnplayedVariationRoundTripsWithItsMusic) {
    UnplayedB s;
    const auto saved = s.doc.toVar();
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(saved));
    EXPECT_EQ(dump(s.doc), dump(loaded));
    const auto* b = loaded.getVariation(s.section, 1);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->stored.clips.at(s.m.value)[0].name, "bee");
    EXPECT_EQ(b->ownTracks, (std::set<std::int64_t>{s.m.value}));
    EXPECT_FALSE(loaded.toVar()["sections"][0]["placements"][0].getDynamicObject()->hasProperty("variation"));

    // Playing it again brings the saved music back.
    ASSERT_TRUE(loaded.setPlacementVariation(s.second, 1));
    EXPECT_EQ(loaded.getTrack(s.m)->clips[1].name, "bee");
}

TEST(TimelineDocSectionVariationFile, APlayedVariationWritesNoStoredMusicAndAUnplayedAWritesItsOwn) {
    UnplayedB s;
    ASSERT_TRUE(s.doc.playVariationEverywhere(s.section, 1));
    const auto saved = s.doc.toVar();
    const auto sectionVar = saved["sections"][0];
    EXPECT_FALSE(sectionVar["variations"][0].getDynamicObject()->hasProperty("stored"));
    EXPECT_TRUE(sectionVar.getDynamicObject()->hasProperty("stored")) << "A is unplayed now";
    EXPECT_EQ(static_cast<int>(sectionVar["placements"][1]["variation"]), 1);

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(saved));
    EXPECT_EQ(dump(s.doc), dump(loaded));
    ASSERT_TRUE(loaded.playVariationEverywhere(s.section, 0));
    EXPECT_EQ(loaded.getTrack(s.m)->clips[1].name, "c") << "A's music survived the save";
}

TEST(TimelineDocSectionVariationFile, AFileWithoutVariationsLoadsAsAOnly) {
    TimelineDoc plain;
    const auto track = plain.addTrack(TrackKind::Midi, "M");
    plain.addClip(track, 0.0, 4.0, "c");
    const auto section = plain.makeSection(0.0, 4.0, "S", 0, kSpb);
    const auto saved = plain.toVar();
    EXPECT_FALSE(saved["sections"][0].getDynamicObject()->hasProperty("variations"));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(saved));
    ASSERT_EQ(loaded.getSection(section)->variations.size(), 1u);
    EXPECT_EQ(loaded.getSection(section)->variations[0].letter, 0);
}

namespace {

struct BadCase {
    const char* what;
    std::function<void(juce::var&)> breakIt;
};

juce::DynamicObject& obj(const juce::var& v) { return *v.getDynamicObject(); }
juce::var variationVar(juce::var& root) { return root["sections"][0]["variations"][0]; }

// The stored clip list of the first (only) stored track of variation B.
juce::Array<juce::var>& storedClipList(juce::var& root) {
    return *variationVar(root)["stored"]["clips"].getDynamicObject()->getProperties().getValueAt(0).getArray();
}
juce::DynamicObject& storedClip(juce::var& root) { return obj(storedClipList(root)[0]); }

std::vector<BadCase> badVariationCases(std::int64_t otherTrack) {
    return {
        {"letter 0", [](juce::var& r) { obj(variationVar(r)).setProperty("letter", 0); }},
        {"letter 26", [](juce::var& r) { obj(variationVar(r)).setProperty("letter", 26); }},
        {"letter not a number", [](juce::var& r) { obj(variationVar(r)).setProperty("letter", "B"); }},
        {"duplicate letter", [](juce::var& r) { r["sections"][0]["variations"].getArray()->add(variationVar(r)); }},
        {"variations not an array", [](juce::var& r) { obj(r["sections"][0]).setProperty("variations", 3); }},
        {"26 variations",
         [](juce::var& r) {
             auto* list = r["sections"][0]["variations"].getArray();
             while (list->size() < 26)
                 list->add(variationVar(r));
         }},
        {"placement names an absent letter",
         [](juce::var& r) { obj(r["sections"][0]["placements"][0]).setProperty("variation", 7); }},
        {"placement letter not a number",
         [](juce::var& r) { obj(r["sections"][0]["placements"][0]).setProperty("variation", "x"); }},
        {"unknown variation key", [](juce::var& r) { obj(variationVar(r)).setProperty("extra", 1); }},
        {"unknown stored key", [](juce::var& r) { obj(variationVar(r)["stored"]).setProperty("extra", 1); }},
        {"ownTracks missing", [](juce::var& r) { obj(variationVar(r)).removeProperty("ownTracks"); }},
        {"ownTracks names an absent track",
         [](juce::var& r) { variationVar(r)["ownTracks"].getArray()->add(juce::int64(9999)); }},
        {"ownTracks repeats a track",
         [](juce::var& r) { variationVar(r)["ownTracks"].getArray()->add(variationVar(r)["ownTracks"][0]); }},
        {"stored clips for an absent track",
         [](juce::var& r) {
             obj(variationVar(r)["stored"]["clips"]).setProperty(juce::Identifier("9999"), juce::Array<juce::var>());
         }},
        {"stored clips for a track the variation does not own",
         [otherTrack](juce::var& r) {
             obj(variationVar(r)["stored"]["clips"])
                 .setProperty(juce::Identifier(juce::String(otherTrack)), juce::Array<juce::var>());
         }},
        {"stored points for an absent lane",
         [](juce::var& r) {
             obj(variationVar(r)["stored"]["points"]).setProperty(juce::Identifier("77"), juce::Array<juce::var>());
         }},
        {"stored clip with no length", [](juce::var& r) { storedClip(r).setProperty("lengthBeats", 0.0); }},
        {"stored clip outside the section", [](juce::var& r) { storedClip(r).setProperty("startBeat", 100.0); }},
        {"stored clip not an object", [](juce::var& r) { storedClipList(r).set(0, juce::var(1)); }},
    };
}

} // namespace

TEST(TimelineDocSectionVariationFile, FromVarAndTheValidatorRefuseEachBadCaseAndAcceptTheWritersOutput) {
    UnplayedB s;
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::validateTimeline(s.doc.toVar(), graph).ok)
        << synth::validateTimeline(s.doc.toVar(), graph).message;
    const auto otherTrack = s.doc.getTracks()[1].id.value;
    for (const auto& c : badVariationCases(otherTrack)) {
        auto saved = s.doc.toVar();
        c.breakIt(saved);
        TimelineDoc loaded;
        EXPECT_FALSE(loaded.fromVar(saved)) << c.what;
        EXPECT_FALSE(synth::validateTimeline(saved, graph).ok) << c.what;
    }

    // An audio file is never accepted from an untrusted file, stored or not.
    auto withAsset = s.doc.toVar();
    storedClip(withAsset).setProperty("assetRef", "Audio/x.wav");
    EXPECT_EQ(synth::validateTimeline(withAsset, graph).error, TimelineValidationError::AssetNotAllowed);
}

TEST(TimelineDocSectionVariationFile, StoredNotesCountTowardsTheDocumentNoteBudget) {
    UnplayedB s;
    juce::AudioProcessorGraph graph;
    auto saved = s.doc.toVar();
    ASSERT_TRUE(synth::validateTimeline(saved, graph).ok);

    // Each clip is within its own cap; five of them are past the whole-document budget.
    juce::Array<juce::var> notes;
    for (int i = 0; i < TimelineDoc::kMaxNotesPerClip; ++i) {
        juce::DynamicObject::Ptr n = new juce::DynamicObject();
        n->setProperty("id", i + 1);
        n->setProperty("startBeat", 0.0);
        n->setProperty("lengthBeats", 1.0);
        n->setProperty("pitch", 60);
        n->setProperty("velocity", 100);
        n->setProperty("channel", 1);
        notes.add(juce::var(n.get()));
    }
    storedClip(saved).setProperty("notes", notes);
    const auto heavy = storedClipList(saved)[0];
    for (int i = 0; i < 4; ++i)
        storedClipList(saved).add(heavy);
    EXPECT_EQ(synth::validateTimeline(saved, graph).error, TimelineValidationError::TooManyNotes);
}

TEST(TimelineDocSectionVariationAssets, OnlyUnplayedVariationsClipsAreVisited) {
    TimelineDoc doc;
    const auto audio = doc.addTrack(TrackKind::Audio, "Au");
    const auto clip = doc.addClip(audio, 0.0, 4.0, "au");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/kept.wav", 0.0));
    const auto section = doc.makeSection(0.0, 4.0, "S", 0, kSpb);
    const auto second = doc.placeSection(section, 4.0, SectionDropMode::MakeRoom, kSpb);

    int visited = 0;
    doc.forEachStoredVariationClip([&](const Clip&) { ++visited; });
    EXPECT_EQ(visited, 0) << "every variation is played";

    ASSERT_EQ(doc.addVariation(second), 1);
    doc.forEachStoredVariationClip([&](const Clip&) { ++visited; });
    EXPECT_EQ(visited, 0) << "B is played by the second placement";

    ASSERT_TRUE(doc.setPlacementVariation(second, 0));
    std::vector<juce::String> refs;
    doc.forEachStoredVariationClip([&](const Clip& c) { refs.push_back(c.assetRef); });
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(refs[0], "Audio/kept.wav");
}

namespace {

// A bundle folder holding Audio/<name> files.
struct TempBundle {
    juce::File root = juce::File::createTempFile("variation-bundle");
    TempBundle() {
        root.createDirectory();
        root.getChildFile(synth::ProjectBundle::kAudioSubdirName).createDirectory();
    }
    ~TempBundle() { root.deleteRecursively(); }
    void add(const juce::String& name) const {
        root.getChildFile(synth::ProjectBundle::kAudioSubdirName).getChildFile(name).replaceWithText("x");
    }
};

} // namespace

TEST(TimelineDocSectionVariationAssets, ACleanKeepsTheFileOnlyAnUnplayedVariationUses) {
    TempBundle bundle;
    bundle.add("kept.wav");
    bundle.add("other.wav");
    bundle.add("orphan.wav");

    TimelineDoc doc;
    const auto audio = doc.addTrack(TrackKind::Audio, "Au");
    const auto clip = doc.addClip(audio, 0.0, 4.0, "au");
    ASSERT_TRUE(doc.setClipAsset(clip, "Audio/kept.wav", 0.0));
    const auto section = doc.makeSection(0.0, 4.0, "S", 0, kSpb);
    const auto second = doc.placeSection(section, 4.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_EQ(doc.addVariation(second), 1);
    ASSERT_TRUE(doc.playVariationEverywhere(section, 1));
    // Both placements now play B; A (unplayed) alone remembers kept.wav once B's clips point elsewhere.
    ASSERT_TRUE(doc.setClipAsset(doc.getTrack(audio)->clips[0].id, "Audio/other.wav", 0.0)); // reaches both
    ASSERT_EQ(doc.getVariation(section, 0)->stored.clips.at(audio.value)[0].assetRef, "Audio/kept.wav");

    const auto unused = synth::AssetManager::collectUnusedAssets(doc, bundle.root);
    EXPECT_EQ(unused.size(), 1);
    EXPECT_TRUE(unused.contains("Audio/orphan.wav"));
    EXPECT_FALSE(unused.contains("Audio/kept.wav")) << "held by the unplayed A";
}
