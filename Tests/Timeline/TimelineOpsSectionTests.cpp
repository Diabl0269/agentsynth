// TimelineOps `makeSection` and `placeSection`: the field checks, the all-or-nothing rule, a song-shaped batch
// ending in real repeats that follow each other, the arrangement context naming the sections, one undo step, and a
// writeLane over the whole song after the sections.

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Timeline/ArrangementContext.h"
#include "Timeline/TimelineDoc/SectionPalette.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include "Timeline/TimelineValidator.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::TimelineDoc;
using synth::TimelineOps;
using synth::TimelineOpsResult;

namespace {

juce::var envelopeOf(const juce::String& opsArrayJson) {
    const juce::var value = juce::JSON::parse("{\"timelineOps\": " + opsArrayJson + "}");
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << opsArrayJson;
    return value;
}

// A Lead track with two bars-worth of notes in each of two 16-beat parts.
const char* const kPlaceLead = R"({"op": "addTrack", "kind": "midi", "name": "Lead"},
    {"op": "placeClips", "track": "Lead", "clips": [
      {"startBeat": 0, "lengthBeats": 16, "name": "A", "notes": [
        {"startBeat": 0, "lengthBeats": 1, "pitch": 60, "velocity": 100},
        {"startBeat": 8, "lengthBeats": 1, "pitch": 64, "velocity": 90}]},
      {"startBeat": 16, "lengthBeats": 16, "name": "B", "notes": [
        {"startBeat": 0, "lengthBeats": 2, "pitch": 67, "velocity": 100}]}]})";

class TimelineOpsSectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        filter->properties.set("uuid", "filter-uuid");
    }

    TimelineOpsResult validate(const juce::String& ops) { return TimelineOps::validate(envelopeOf(ops), doc, graph); }
    TimelineOpsResult apply(const juce::String& ops) {
        return TimelineOps::apply(envelopeOf(ops), doc, graph, undoManager);
    }
    // The song-shaped batch: a Lead, then Chorus made from bars 1-4 and placed again at beat 32.
    juce::String songOps(const juce::String& tail = {}) const {
        return juce::String("[") + kPlaceLead +
               R"(, {"op": "makeSection", "name": "Chorus", "startBeat": 0, "endBeat": 16},
                  {"op": "placeSection", "name": "Chorus", "beat": 32})" +
               tail + "]";
    }
    const synth::Track* lead() const { return doc.getTracks().empty() ? nullptr : &doc.getTracks()[0]; }

    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    TimelineDoc doc;
};

} // namespace

TEST_F(TimelineOpsSectionTest, MakeSectionAndPlaceSectionAcceptAWellFormedPair) {
    const auto result = validate(songOps());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.previewText.contains("makes section Chorus (bars 1-4)")) << result.previewText;
    EXPECT_TRUE(result.previewText.contains("places Chorus at bar 9")) << result.previewText;
    EXPECT_TRUE(doc.getSections().empty()) << "validate never touches the live document";
}

TEST_F(TimelineOpsSectionTest, MakeSectionRejectsABadFieldAndTheWholeBatchWithIt) {
    const juce::String longest(std::string(41, 'x'));
    const std::vector<juce::String> bad{
        R"({"op": "makeSection", "name": "", "startBeat": 0, "endBeat": 16})",
        R"({"op": "makeSection", "name": "   ", "startBeat": 0, "endBeat": 16})",
        R"({"op": "makeSection", "name": ")" + longest + R"(", "startBeat": 0, "endBeat": 16})",
        R"({"op": "makeSection", "name": "X", "startBeat": 16, "endBeat": 16})",
        R"({"op": "makeSection", "name": "X", "startBeat": 16, "endBeat": 8})",
        R"({"op": "makeSection", "name": "X", "startBeat": -1, "endBeat": 8})",
        R"({"op": "makeSection", "name": "X", "startBeat": 0, "endBeat": 100001})",
        R"({"op": "makeSection", "name": "X", "startBeat": 0})",
        R"({"op": "makeSection", "name": 5, "startBeat": 0, "endBeat": 4})",
        R"({"op": "makeSection", "name": "X", "startBeat": 0, "endBeat": 4, "colour": 1})"};
    for (const auto& op : bad) {
        SCOPED_TRACE(op.toStdString());
        const auto result = validate(juce::String("[") + kPlaceLead + ", " + op + "]");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains("(makeSection): ")) << result.message;
        const auto applied = apply(juce::String("[") + kPlaceLead + ", " + op + "]");
        EXPECT_FALSE(applied.ok);
        EXPECT_TRUE(doc.getTracks().empty()) << "all-or-nothing: the earlier ops did not land";
    }
}

TEST_F(TimelineOpsSectionTest, PlaceSectionRejectsABadFieldAnUnknownNameAndAnOccupiedSpot) {
    const juce::String made = juce::String("[") + kPlaceLead +
                              R"(, {"op": "makeSection", "name": "Chorus", "startBeat": 0, "endBeat": 16}, )";
    for (const char* op :
         {R"({"op": "placeSection", "name": "Chorus"})", R"({"op": "placeSection", "name": "Chorus", "beat": -4})",
          R"({"op": "placeSection", "name": "Chorus", "beat": 99999})",
          R"({"op": "placeSection", "name": "Chorus", "beat": 32, "mode": "cover"})",
          R"({"op": "placeSection", "name": "", "beat": 32})", R"({"op": "placeSection", "name": "Verse", "beat": 32})",
          R"({"op": "placeSection", "name": "Chorus", "beat": 8})"}) {
        SCOPED_TRACE(op);
        const auto result = validate(made + op + "]");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains("(placeSection): ")) << result.message;
    }
    EXPECT_FALSE(
        validate(juce::String("[") + kPlaceLead + R"(, {"op": "placeSection", "name": "Chorus", "beat": 32}])").ok)
        << "a placeSection before its makeSection names nothing";
}

TEST_F(TimelineOpsSectionTest, ASectionNameCannotBeMadeTwiceOrOverlapAnother) {
    const juce::String head = juce::String("[") + kPlaceLead;
    EXPECT_FALSE(validate(head + R"(, {"op": "makeSection", "name": "A", "startBeat": 0, "endBeat": 16},
                                   {"op": "makeSection", "name": "A", "startBeat": 16, "endBeat": 32}])")
                     .ok);
    EXPECT_FALSE(validate(head + R"(, {"op": "makeSection", "name": "A", "startBeat": 0, "endBeat": 16},
                                   {"op": "makeSection", "name": "B", "startBeat": 8, "endBeat": 24}])")
                     .ok);
    EXPECT_TRUE(validate(head + R"(, {"op": "makeSection", "name": "A", "startBeat": 0, "endBeat": 16},
                                  {"op": "makeSection", "name": "B", "startBeat": 16, "endBeat": 32}])")
                    .ok)
        << "touching is fine";
}

TEST_F(TimelineOpsSectionTest, AnExistingSectionCanBePlacedByNameWhenTheBatchMadeNone) {
    ASSERT_TRUE(apply(songOps()).ok);
    EXPECT_TRUE(validate(R"([{"op": "placeSection", "name": "Chorus", "beat": 48}])").ok);
}

TEST_F(TimelineOpsSectionTest, ASectionNamedLikeOneInTheDocumentGetsAFreeNameAndTheBatchPlacesItsOwn) {
    TimelineDoc& d = doc;
    const auto lead = d.addTrack(synth::TrackKind::Midi, "Old");
    d.addClip(lead, 64.0, 16.0, "old");
    const auto old = d.makeSection(64.0, 80.0, "Chorus", 0, 0.5);
    ASSERT_TRUE(old.isValid());
    const auto result = apply(songOps());
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(doc.getSections().size(), 2u);
    EXPECT_EQ(doc.getSections()[0].name, "Chorus");
    EXPECT_EQ(doc.getSections()[1].name, "Chorus 2");
    EXPECT_EQ(doc.placementsOf(old).size(), 1u) << "the old Chorus is untouched";
    EXPECT_EQ(doc.placementsOf(doc.getSections()[1].id).size(), 2u) << "the batch placed the section it made";
}

TEST_F(TimelineOpsSectionTest, AnAmbiguousDocumentNameCannotBePlacedButABatchNameIsNeverAmbiguous) {
    doc.addTrack(synth::TrackKind::Midi, "Old");
    ASSERT_TRUE(doc.makeSection(64.0, 80.0, "Chorus", 0, 0.5).isValid());
    ASSERT_TRUE(doc.makeSection(80.0, 96.0, "Chorus", 0, 0.5).isValid());
    EXPECT_FALSE(validate(R"([{"op": "placeSection", "name": "Chorus", "beat": 200}])").ok);
    EXPECT_TRUE(validate(juce::String("[") + kPlaceLead +
                         R"(, {"op": "makeSection", "name": "Chorus", "startBeat": 0, "endBeat": 16},
                            {"op": "placeSection", "name": "Chorus", "beat": 200}])")
                    .ok);
}

TEST_F(TimelineOpsSectionTest, TheSectionPaletteIsTheTimelineModelsAndWraps) {
    EXPECT_EQ(synth::sectionColourFor(0), synth::kSectionColours[0]);
    EXPECT_EQ(synth::sectionColourFor(8), synth::kSectionColours[0]);
}

TEST_F(TimelineOpsSectionTest, TheSectionLimitsRejectTheBatch) {
    TimelineDoc big;
    big.addTrack(synth::TrackKind::Midi, "Lead");
    for (int i = 0; i < TimelineDoc::kMaxSections; ++i)
        ASSERT_TRUE(big.makeSection(i * 4.0, i * 4.0 + 4.0, "S" + juce::String(i), 0, 0.5).isValid());
    const auto result = TimelineOps::validate(
        envelopeOf(R"([{"op": "makeSection", "name": "More", "startBeat": 2000, "endBeat": 2004}])"), big, graph);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("limit")) << result.message;
}

TEST_F(TimelineOpsSectionTest, ABatchEndsWithARepeatHoldingTheOriginalClipsAndEditsFollow) {
    const auto result = apply(songOps());
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(doc.getSections().size(), 1u);
    ASSERT_EQ(doc.getPlacements().size(), 2u);
    EXPECT_EQ(doc.getSections()[0].name, "Chorus");
    EXPECT_DOUBLE_EQ(doc.getSections()[0].lengthBeats, 16.0);
    EXPECT_NE(doc.getSections()[0].colourArgb, 0u);

    const auto* track = lead();
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 3u);
    const synth::Clip* original = &track->clips[0];
    const synth::Clip* repeat = &track->clips[2];
    EXPECT_DOUBLE_EQ(original->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(repeat->startBeat, 32.0);
    EXPECT_TRUE(synth::sameClipContent(*original, [&] {
        synth::Clip shifted = *repeat;
        shifted.startBeat = original->startBeat;
        return shifted;
    }()));
    ASSERT_EQ(repeat->notes.size(), 2u);

    // Editing a note in the repeat reaches the original.
    ASSERT_TRUE(doc.moveNote(repeat->notes[0].id, 1.0, 72));
    EXPECT_EQ(lead()->clips[0].notes[0].pitch, 72);
    EXPECT_DOUBLE_EQ(lead()->clips[0].notes[0].startBeat, 1.0);
    EXPECT_TRUE(synth::validateTimeline(doc.toVar(), graph).ok);
}

TEST_F(TimelineOpsSectionTest, TheArrangementContextListsTheSectionsAfterTheBatch) {
    ASSERT_TRUE(apply(songOps()).ok);
    synth::TransportService::PositionSnapshot snapshot;
    snapshot.bpm = 120.0;
    snapshot.timeSigNumerator = 4;
    snapshot.timeSigDenominator = 4;
    const auto summary = synth::ArrangementContext::summarize(doc, graph, snapshot);
    EXPECT_TRUE(summary.contains("Sections (bars): \"Chorus\" 1-4 (1 of 2); \"Chorus\" 9-12 (2 of 2)"))
        << summary.toStdString();
}

TEST_F(TimelineOpsSectionTest, OneUndoRestoresTheDocumentBeforeTheBatch) {
    const auto before = juce::JSON::toString(doc.toVar());
    const auto result = apply(songOps());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.message.contains("one undo step")) << result.message;
    ASSERT_FALSE(doc.getSections().empty());
    undoManager.undo();
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), before);
}

// What the stamp does to automation: the batch's writeLane lands over every bar, so the repeat's points are
// replaced by the first Chorus' (shifted). The document stays valid and the lane holds the same shape in both.
TEST_F(TimelineOpsSectionTest, AWriteLaneOverTheWholeSongAfterTheSectionsLeavesTheDocumentValid) {
    const auto result = apply(songOps(R"(, {"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
        "points": [{"beat": 0, "value": 400}, {"beat": 8, "value": 6000}, {"beat": 16, "value": 900},
                   {"beat": 40, "value": 3000}, {"beat": 47, "value": 200}]})"));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(synth::validateTimeline(doc.toVar(), graph).ok);
    const auto* lane = doc.getLaneForParam("filter-uuid", "cutoff");
    ASSERT_NE(lane, nullptr);
    std::vector<double> inFirst, inRepeat;
    for (const auto& p : lane->points) {
        if (p.beat >= 0.0 && p.beat < 16.0)
            inFirst.push_back(p.value);
        else if (p.beat >= 32.0 && p.beat < 48.0)
            inRepeat.push_back(p.value);
    }
    EXPECT_EQ(inFirst, (std::vector<double>{400.0, 6000.0})) << "the first Chorus keeps what was written there";
    EXPECT_EQ(inRepeat, inFirst) << "the repeat plays the same automation as the first Chorus";
}
