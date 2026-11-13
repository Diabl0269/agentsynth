// The per-hit drum settings a placeClips note may carry (chance, repeat, flam, nudge, condition): accepted when in
// range, rejected (never clamped) when not, and every error lists every accepted key.

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::TimelineDoc;
using synth::TimelineOps;
using synth::TimelineOpsResult;
using synth::TrackKind;

namespace {

juce::var envelopeWithNote(const juce::String& noteJson) {
    return juce::JSON::parse(
        R"({"timelineOps": [{"op": "placeClips", "track": "Drums", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [)" +
        noteJson + "]}]}]}");
}

struct OpsFixture {
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    TimelineDoc doc;
    OpsFixture() { doc.addTrack(TrackKind::Midi, "Drums"); }
    TimelineOpsResult validate(const juce::String& note) {
        return TimelineOps::validate(envelopeWithNote(note), doc, graph);
    }
    TimelineOpsResult apply(const juce::String& note) {
        return TimelineOps::apply(envelopeWithNote(note), doc, graph, undoManager);
    }
};

} // namespace

TEST(TimelineOpsDrumNotes, EveryKeyIsAcceptedAndLandsOnTheNote) {
    OpsFixture f;
    const auto result = f.apply(R"({"startBeat": 1, "lengthBeats": 0.25, "pitch": 42, "chance": 50, "repeat": 3,
        "flam": true, "nudge": -0.25, "condition": "2/4"})");
    ASSERT_TRUE(result.ok) << result.message;
    const auto& notes = f.doc.getTracks()[0].clips[0].notes;
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].chance, 50);
    EXPECT_EQ(notes[0].repeat, 3);
    EXPECT_TRUE(notes[0].flam);
    EXPECT_DOUBLE_EQ(notes[0].nudge, -0.25);
    EXPECT_EQ(notes[0].condHit, 2);
    EXPECT_EQ(notes[0].condOf, 4);
}

TEST(TimelineOpsDrumNotes, TheBoundsAreAccepted) {
    OpsFixture f;
    for (const char* note : {R"({"chance": 0})", R"({"chance": 100})", R"({"repeat": 1})", R"({"repeat": 8})",
                             R"({"nudge": -0.5})", R"({"nudge": 0.5})", R"({"condition": "1/1"})",
                             R"({"condition": "8/8"})", R"({"flam": false})", R"({"nudge": 0})"})
        EXPECT_TRUE(f.validate(note).ok) << note;
}

TEST(TimelineOpsDrumNotes, OutOfRangeAndMalformedValuesAreRejectedNotClamped) {
    OpsFixture f;
    for (const char* note :
         {R"({"chance": 101})", R"({"chance": -1})", R"({"chance": 50.5})", R"({"chance": "50"})", R"({"repeat": 0})",
          R"({"repeat": 9})", R"({"flam": 1})", R"({"flam": "true"})", R"({"nudge": 0.51})", R"({"nudge": -0.6})",
          R"({"nudge": "0.1"})", R"({"condition": "0/2"})", R"({"condition": "3/2"})", R"({"condition": "1/9"})",
          R"({"condition": "2"})", R"({"condition": "a/b"})", R"({"condition": "1/"})", R"({"condition": 2})",
          R"({"condition": "-1/2"})"}) {
        const auto result = f.validate(note);
        EXPECT_FALSE(result.ok) << note;
        EXPECT_TRUE(f.doc.getTracks()[0].clips.empty()) << "a rejected op changes nothing: " << note;
    }
}

TEST(TimelineOpsDrumNotes, AnUnknownKeyErrorListsEveryAcceptedKey) {
    OpsFixture f;
    const auto result = f.validate(R"({"pitch": 42, "swing": 0.5})");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("\"swing\"")) << result.message;
    for (const char* key :
         {"startBeat", "lengthBeats", "pitch", "velocity", "channel", "chance", "repeat", "flam", "nudge", "condition"})
        EXPECT_TRUE(result.message.contains(juce::String("\"") + key + "\"")) << key << " in: " << result.message;
}

TEST(TimelineOpsDrumNotes, ANoteWithoutTheKeysKeepsTheDefaults) {
    OpsFixture f;
    ASSERT_TRUE(f.apply(R"({"pitch": 36})").ok);
    const auto& note = f.doc.getTracks()[0].clips[0].notes[0];
    EXPECT_EQ(note.chance, 100);
    EXPECT_EQ(note.repeat, 1);
    EXPECT_FALSE(note.flam);
    EXPECT_DOUBLE_EQ(note.nudge, 0.0);
    EXPECT_EQ(note.condOf, 1);
}
