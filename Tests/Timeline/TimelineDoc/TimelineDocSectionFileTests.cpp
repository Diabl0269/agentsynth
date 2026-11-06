// Sections in the file: round trip, malformed files, a load that never stamps, the untrusted validator and undo.

#include "AppUndoManager.h"
#include "Timeline/TimelineValidator.h"
#include "TimelineDocSectionTestHelpers.h"
#include <juce_audio_processors/juce_audio_processors.h>

using synth::TimelineValidationError;

namespace {

juce::DynamicObject& obj(const juce::var& v) { return *v.getDynamicObject(); }
juce::DynamicObject& sectionObj(juce::var& root, int i = 0) { return obj(root["sections"][i]); }
juce::DynamicObject& placementObj(juce::var& root, int section = 0, int i = 0) {
    return obj(root["sections"][section]["placements"][i]);
}

struct BadCase {
    const char* what;
    std::function<void(juce::var&)> breakIt;
};

std::vector<BadCase> badCases() {
    return {
        {"sections is not an array", [](juce::var& r) { obj(r).setProperty("sections", 5); }},
        {"a section is not an object", [](juce::var& r) { r["sections"].getArray()->set(0, juce::var(3)); }},
        {"duplicate section id",
         [](juce::var& r) { sectionObj(r, 1).setProperty("id", sectionObj(r, 0).getProperty("id")); }},
        {"placement id equals a section id",
         [](juce::var& r) { placementObj(r).setProperty("id", sectionObj(r, 1).getProperty("id")); }},
        {"zero id", [](juce::var& r) { sectionObj(r).setProperty("id", 0); }},
        {"no placements", [](juce::var& r) { sectionObj(r).setProperty("placements", juce::Array<juce::var>()); }},
        {"placements missing", [](juce::var& r) { sectionObj(r).removeProperty("placements"); }},
        {"overlap", [](juce::var& r) { placementObj(r, 1, 0).setProperty("startBeat", 1.0); }},
        {"empty name", [](juce::var& r) { sectionObj(r).setProperty("name", ""); }},
        {"untrimmed name", [](juce::var& r) { sectionObj(r).setProperty("name", " x"); }},
        {"name not a string", [](juce::var& r) { sectionObj(r).setProperty("name", 4); }},
        {"name too long",
         [](juce::var& r) { sectionObj(r).setProperty("name", juce::String::repeatedString("x", 129)); }},
        {"zero length", [](juce::var& r) { sectionObj(r).setProperty("lengthBeats", 0.0); }},
        {"negative start", [](juce::var& r) { placementObj(r).setProperty("startBeat", -1.0); }},
        {"start not a number", [](juce::var& r) { placementObj(r).setProperty("startBeat", "x"); }},
        {"colour out of range", [](juce::var& r) { sectionObj(r).setProperty("colourArgb", juce::int64(1) << 33); }},
        {"unknown section key", [](juce::var& r) { sectionObj(r).setProperty("extra", 1); }},
        {"unknown placement key", [](juce::var& r) { placementObj(r).setProperty("extra", 1); }},
        {"bad counter", [](juce::var& r) { obj(r).setProperty("nextSectionId", 0); }},
        {"too many sections",
         [](juce::var& r) {
             auto* a = r["sections"].getArray();
             while (a->size() <= TimelineDoc::kMaxSections)
                 a->add(a->getFirst());
         }},
    };
}

class SectionFileTest : public TimelineDocTest {
protected:
    ThreeVerses t;
    SectionId chorus;

    void SetUp() override {
        TimelineDocTest::SetUp();
        t = buildThreeVerses(doc);
        chorus = doc.makeSection(32.0, 36.0, "Chorus", 0xff00ff00, kSpb);
        ASSERT_TRUE(chorus.isValid());
    }
};

} // namespace

TEST_F(SectionFileTest, SectionsRoundTripAndAreAbsentWhenThereAreNone) {
    const auto saved = doc.toVar();
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(saved));
    EXPECT_EQ(dump(doc), dump(loaded));
    ASSERT_EQ(loaded.getSections().size(), 2u);
    EXPECT_EQ(loaded.getPlacements().size(), 4u);

    TimelineDoc plain;
    plain.addTrack(TrackKind::Midi, "M");
    const auto var = plain.toVar();
    EXPECT_FALSE(var.getDynamicObject()->hasProperty("sections"));
    EXPECT_FALSE(var.getDynamicObject()->hasProperty("nextSectionId"));

    // A fresh id after a load never collides with a loaded one.
    const auto fresh = loaded.makeSection(40.0, 44.0, "New", 0, kSpb);
    for (const auto& s : loaded.getSections())
        if (s.id != fresh)
            EXPECT_NE(s.id, fresh);
    for (const auto& p : loaded.getPlacements())
        EXPECT_NE(p.id.value, fresh.value);
}

TEST_F(SectionFileTest, FromVarRejectsEachMalformedCaseAndLeavesTheDocUntouched) {
    const auto before = dump(doc);
    for (const auto& c : badCases()) {
        auto saved = doc.toVar();
        c.breakIt(saved);
        EXPECT_FALSE(doc.fromVar(saved)) << c.what;
        EXPECT_EQ(dump(doc), before) << c.what;
    }
}

TEST_F(SectionFileTest, ALoadedFileWithDifferingRepeatsDoesNotStampUntilOneIsEdited) {
    auto saved = doc.toVar();
    // Rename the clip in the second placement directly in the saved file.
    auto& clipA = *saved["tracks"][0]["clips"].getArray();
    int renamed = 0;
    for (auto& clip : clipA)
        if (static_cast<double>(clip["startBeat"]) == 8.0 && ++renamed == 1)
            clip.getDynamicObject()->setProperty("name", "odd");
    ASSERT_EQ(renamed, 1);

    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(saved));
    EXPECT_EQ(loaded.getSectionStampRevision(), 0);
    EXPECT_EQ(clipsOn(loaded, t.song.a, 8.0, 9.0)[0]->name, "odd");
    EXPECT_EQ(clipsOn(loaded, t.song.a, 0.0, 1.0)[0]->name, "c0");

    // The first edit stamps from the edited placement (the third one, here).
    loaded.setClipName(clipsOn(loaded, t.song.a, 16.0, 17.0)[0]->id, "third");
    for (const double start : {0.0, 8.0, 16.0})
        EXPECT_EQ(clipsOn(loaded, t.song.a, start, start + 1.0)[0]->name, "third");
}

TEST_F(SectionFileTest, ValidateTimelineAcceptsAValidFileAndRefusesEachBadCase) {
    juce::AudioProcessorGraph graph;
    // The fixture's lane is bound to a node this empty graph lacks, so validate a lane-free copy of the song.
    TimelineDoc plain;
    const auto track = plain.addTrack(TrackKind::Midi, "M");
    plain.addClip(track, 0.0, 4.0, "c");
    const auto first = plain.makeSection(0.0, 4.0, "A", 0, kSpb);
    plain.placeSection(first, 4.0, SectionDropMode::MakeRoom, kSpb);
    plain.makeSection(8.0, 12.0, "B", 0, kSpb);
    EXPECT_TRUE(synth::validateTimeline(plain.toVar(), graph).ok)
        << synth::validateTimeline(plain.toVar(), graph).message;
    for (const auto& c : badCases()) {
        auto saved = plain.toVar();
        c.breakIt(saved);
        const auto result = synth::validateTimeline(saved, graph);
        EXPECT_FALSE(result.ok) << c.what;
    }

    auto overlap = plain.toVar();
    placementObj(overlap, 1, 0).setProperty("startBeat", 1.0);
    EXPECT_EQ(synth::validateTimeline(overlap, graph).error, TimelineValidationError::SectionOverlap);
    auto many = plain.toVar();
    auto* a = many["sections"].getArray();
    while (a->size() <= TimelineDoc::kMaxSections)
        a->add(a->getFirst());
    EXPECT_EQ(synth::validateTimeline(many, graph).error, TimelineValidationError::TooManySections);
    EXPECT_EQ(synth::timelineValidationErrorName(TimelineValidationError::SectionOverlap), "SectionOverlap");
    EXPECT_EQ(synth::timelineValidationErrorName(TimelineValidationError::TooManySections), "TooManySections");
}

TEST(TimelineDocSectionUndo, MakeSectionUndoesAndAStampingEditUndoesInOneStep) {
    AppUndoManager undo;
    TimelineDoc doc;
    const auto song = buildSectionSong(doc);
    const auto empty = dump(doc);

    SectionId verse;
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { verse = doc.makeSection(0.0, 8.0, "Verse", 0, kSpb); }));
    ASSERT_TRUE(verse.isValid());
    ASSERT_TRUE(undo.recordTimelineChange(doc, [&] { doc.placeSection(verse, 8.0, SectionDropMode::MakeRoom, kSpb); }));
    const auto twoPlacements = dump(doc);

    ASSERT_TRUE(
        undo.recordTimelineChange(doc, [&] { doc.setClipName(clipsOn(doc, song.a, 8.0, 9.0)[0]->id, "edit"); }));
    EXPECT_EQ(clipsOn(doc, song.a, 0.0, 1.0)[0]->name, "edit");

    undo.undo();
    EXPECT_EQ(dump(doc), twoPlacements) << "one undo reverts the edit and its copies together";
    undo.undo();
    undo.undo();
    EXPECT_EQ(dump(doc), empty);
    EXPECT_TRUE(doc.getSections().empty());
    EXPECT_TRUE(doc.getPlacements().empty());
}
