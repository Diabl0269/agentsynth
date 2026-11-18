// expandSong / parseSongNotes: the C++ port of the hosted song-layout expansion, on the same inputs the
// hosted service's tests use, plus the never-throws behaviour (skip what cannot be read, say why).
#include "AI/ProjectGenerate/SongLayout.h"
#include <gtest/gtest.h>

using synth::projectgenerate::expandSong;
using synth::projectgenerate::parseSongNotes;
using synth::projectgenerate::SongNote;

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

const char* const kBaseSong = R"({"bpm": 174,
  "patterns": [
    {"id": "kick", "track": "Kick", "bars": 1, "notes": "0:1:36:100 2:1:36:100"},
    {"id": "bass", "track": "Bass", "bars": 2, "notes": "0:2:40:80 4:8:43:80"}],
  "sections": [
    {"name": "Intro", "bars": 2, "play": [{"pattern": "kick"}]},
    {"name": "Drop", "bars": 4, "play": [{"pattern": "kick"}, {"pattern": "bass"}]}]})";

juce::var envelope(const juce::String& song, const juce::String& extra = {}) {
    return parse(R"({"mode": "merge", "nodes": [], "connections": )" + juce::String("[]") + extra + R"(, "song": )" +
                 song + "}");
}

juce::var songWith(const juce::String& patterns, const juce::String& sections, int bpm = 120) {
    return envelope("{\"bpm\": " + juce::String(bpm) + ", \"patterns\": " + patterns + ", \"sections\": " + sections +
                    "}");
}

juce::Array<juce::var> opsOf(const juce::var& env) {
    const juce::Array<juce::var>* ops = env["timelineOps"].getArray();
    return ops != nullptr ? *ops : juce::Array<juce::var>();
}

juce::StringArray opNames(const juce::var& env) {
    juce::StringArray names;
    for (const juce::var& op : opsOf(env))
        names.add(op["op"].toString());
    return names;
}

// The clips of the placeClips op on `track` (a name, as the JSON text of the selector).
juce::Array<juce::var> clipsOn(const juce::var& env, const juce::String& trackJson) {
    for (const juce::var& op : opsOf(env))
        if (op["op"].toString() == "placeClips" && juce::JSON::toString(op["track"], true) == trackJson)
            return *op["clips"].getArray();
    return {};
}

juce::Array<double> field(const juce::var& clip, const char* name) {
    juce::Array<double> out;
    for (const juce::var& note : *clip["notes"].getArray())
        out.add(static_cast<double>(note[name]));
    return out;
}

void expectDoubles(const juce::Array<double>& actual, std::initializer_list<double> expected) {
    ASSERT_EQ(actual.size(), static_cast<int>(expected.size()));
    int i = 0;
    for (double e : expected)
        EXPECT_DOUBLE_EQ(actual[i++], e);
}

} // namespace

TEST(SongLayout, TilesPatternsOffsetsSectionsAndNamesClips) {
    const juce::var out = expandSong(envelope(kBaseSong));
    EXPECT_FALSE(out.hasProperty("song"));
    const auto kick = clipsOn(out, "\"Kick\"");
    ASSERT_EQ(kick.size(), 2);
    EXPECT_EQ(kick[0]["name"].toString(), "Intro");
    EXPECT_DOUBLE_EQ(static_cast<double>(kick[0]["startBeat"]), 0);
    EXPECT_DOUBLE_EQ(static_cast<double>(kick[0]["lengthBeats"]), 8);
    EXPECT_DOUBLE_EQ(static_cast<double>(kick[1]["startBeat"]), 8);
    EXPECT_DOUBLE_EQ(static_cast<double>(kick[1]["lengthBeats"]), 16);
    expectDoubles(field(kick[0], "startBeat"), {0, 2, 4, 6});
    EXPECT_EQ(kick[1]["notes"].getArray()->size(), 8);
    // The 2-bar bass repeats twice in the 4-bar Drop, so its notes land at 0, 4, 8 and 12.
    expectDoubles(field(clipsOn(out, "\"Bass\"")[0], "startBeat"), {0, 4, 8, 12});
}

TEST(SongLayout, EmitsTempoMarkersThenPlaceClipsAfterTheModelsOwnOps) {
    const juce::var out =
        expandSong(envelope(kBaseSong, R"(, "timelineOps": [{"op": "addTrack", "name": "Bass", "kind": "midi"}])"));
    EXPECT_EQ(opNames(out).joinIntoString(","), "addTrack,setTempo,addMarker,addMarker,placeClips,placeClips");
    const auto ops = opsOf(out);
    EXPECT_EQ(static_cast<int>(ops[1]["bpm"]), 174);
    EXPECT_EQ(ops[2]["name"].toString(), "Intro");
    EXPECT_DOUBLE_EQ(static_cast<double>(ops[2]["beat"]), 0);
    EXPECT_EQ(ops[3]["name"].toString(), "Drop");
    EXPECT_DOUBLE_EQ(static_cast<double>(ops[3]["beat"]), 8);
}

TEST(SongLayout, ShortensNotesPastTheSectionEndAndDropsThoseStartingAfterIt) {
    const juce::String bass = R"([{"id": "bass", "track": "Bass", "bars": 2, "notes": "0:2:40:80 4:8:43:80"}])";
    // 1 bar: the note at 4 starts at the section end and is dropped.
    const juce::var shortOut =
        expandSong(songWith(bass, R"([{"name": "Short", "bars": 1, "play": [{"pattern": "bass"}]}])"));
    expectDoubles(field(clipsOn(shortOut, "\"Bass\"")[0], "startBeat"), {0});
    expectDoubles(field(clipsOn(shortOut, "\"Bass\"")[0], "lengthBeats"), {2});
    // 3 bars: [0,2], [4,8], and the tile at 8 gets its first note (length 2); its second is dropped.
    const juce::var tail =
        expandSong(songWith(bass, R"([{"name": "Tail", "bars": 3, "play": [{"pattern": "bass"}]}])"));
    const auto clip = clipsOn(tail, "\"Bass\"")[0];
    expectDoubles(field(clip, "startBeat"), {0, 4, 8});
    expectDoubles(field(clip, "lengthBeats"), {2, 8, 2});
}

TEST(SongLayout, ClampsTransposeAndScalesAndRoundsVelocity) {
    const juce::var out =
        expandSong(songWith(R"([{"id": "p", "track": "T", "bars": 1, "notes": "0:1:126:100 1:1:2:1"}])",
                            R"([{"name": "Up", "bars": 1, "play": [{"pattern": "p", "transpose": 24, "velocity": 1.5}]},
            {"name": "Down", "bars": 1, "play": [{"pattern": "p", "transpose": -24, "velocity": 0.25}]}])"));
    const auto clips = clipsOn(out, "\"T\"");
    ASSERT_EQ(clips.size(), 2);
    expectDoubles(field(clips[0], "pitch"), {127, 26});
    expectDoubles(field(clips[0], "velocity"), {127, 2});
    expectDoubles(field(clips[1], "pitch"), {102, 0});
    expectDoubles(field(clips[1], "velocity"), {25, 1});
}

TEST(SongLayout, PlaysTheFillInsteadOfTheMainPatternInTheLastBars) {
    const juce::String patterns = R"([{"id": "main", "track": "D", "bars": 1, "notes": "0:1:36:100"},
        {"id": "fill", "track": "D", "bars": 1, "notes": "0:1:38:100 2:1:38:100"}])";
    const juce::var out =
        expandSong(songWith(patterns, R"([{"name": "A", "bars": 4, "play": [{"pattern": "main", "fill": "fill"}]}])"));
    const auto clip = clipsOn(out, "\"D\"")[0];
    expectDoubles(field(clip, "startBeat"), {0, 4, 8, 12, 14});
    expectDoubles(field(clip, "pitch"), {36, 36, 36, 38, 38});
    // A section shorter than the fill is covered by the fill entirely.
    const juce::String twoBarFill = R"([{"id": "main", "track": "D", "bars": 1, "notes": "0:1:36:100"},
        {"id": "fill", "track": "D", "bars": 2, "notes": "0:1:38:100 4:1:39:100"}])";
    const juce::var out2 = expandSong(
        songWith(twoBarFill, R"([{"name": "B", "bars": 1, "play": [{"pattern": "main", "fill": "fill"}]}])"));
    const auto clip2 = clipsOn(out2, "\"D\"")[0];
    expectDoubles(field(clip2, "startBeat"), {0});
    expectDoubles(field(clip2, "pitch"), {38});
}

TEST(SongLayout, GroupsPlaysOnTheSameTrackIntoOneClipAndSkipsEmptyClips) {
    const juce::var out = expandSong(songWith(
        R"([{"id": "a", "track": {"index": 2}, "bars": 1, "notes": "1:1:60:100"},
            {"id": "b", "track": {"index": 2}, "bars": 1, "notes": "0:1:55:100"},
            {"id": "empty", "track": "Z", "bars": 1, "notes": ""}])",
        R"([{"name": "S", "bars": 1, "play": [{"pattern": "a"}, {"pattern": "b"}, {"pattern": "empty"}]}])"));
    EXPECT_EQ(opNames(out).joinIntoString(","), "setTempo,addMarker,placeClips");
    const auto clips = clipsOn(out, juce::JSON::toString(parse(R"({"index":2})"), true));
    ASSERT_EQ(clips.size(), 1);
    expectDoubles(field(clips[0], "pitch"), {55, 60});
}

TEST(SongLayout, EmitsTempoAndMarkersEvenWhenNoClipsArePlaced) {
    const juce::var out = expandSong(songWith("[]", R"([{"name": "S", "bars": 1, "play": []}])", 90));
    EXPECT_EQ(opNames(out).joinIntoString(","), "setTempo,addMarker");
    EXPECT_EQ(static_cast<int>(opsOf(out)[0]["bpm"]), 90);
}

TEST(SongLayout, AnEnvelopeWithoutASongComesBackUnchanged) {
    const juce::var env = parse(
        R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [{"op": "addTrack", "name": "T", "kind": "midi"}]})");
    juce::StringArray warnings;
    EXPECT_EQ(juce::JSON::toString(expandSong(env, &warnings)), juce::JSON::toString(env));
    EXPECT_TRUE(warnings.isEmpty());
}

TEST(SongLayout, ExpandsA64NoteHatsPatternString) {
    juce::StringArray tokens;
    for (int i = 0; i < 64; ++i)
        tokens.add(juce::String(i / 4.0) + ":0.125:42:" + juce::String(i % 2 == 0 ? 90 : 60));
    const juce::var out = expandSong(
        songWith(R"([{"id": "hats", "track": "Hats", "bars": 4, "notes": ")" + tokens.joinIntoString(" ") + R"("}])",
                 R"([{"name": "S", "bars": 8, "play": [{"pattern": "hats"}]}])"));
    const auto clip = clipsOn(out, "\"Hats\"")[0];
    ASSERT_EQ(clip["notes"].getArray()->size(), 128);
    EXPECT_DOUBLE_EQ(static_cast<double>((*clip["notes"].getArray())[1]["startBeat"]), 0.25);
    EXPECT_EQ(static_cast<int>((*clip["notes"].getArray())[1]["velocity"]), 60);
    EXPECT_DOUBLE_EQ(static_cast<double>((*clip["notes"].getArray())[127]["startBeat"]), 31.75);
}

TEST(SongLayout, ABadNotesStringIsSkippedWithAWarningAndTheRestStillExpands) {
    juce::StringArray warnings;
    const juce::var out =
        expandSong(songWith(
                       R"([{"id": "good", "track": "Kick", "bars": 1, "notes": "0:1:36"},
            {"id": "hats", "track": "H", "bars": 1, "notes": "0:0.25:42 1:0.25:42 1.5:0.25:x"}])",
                       R"([{"name": "A", "bars": 1, "play": [{"pattern": "good"}, {"pattern": "hats"}]}])"),
                   &warnings);
    EXPECT_EQ(clipsOn(out, "\"Kick\"").size(), 1);
    EXPECT_EQ(clipsOn(out, "\"H\"").size(), 0);
    EXPECT_TRUE(warnings.joinIntoString("\n").contains(
        "Pattern \"hats\" note 3 \"1.5:0.25:x\" is not start:length:pitch[:velocity]"));
}

TEST(SongLayout, UnknownPatternsAndMismatchedFillsAreSkippedWithWarnings) {
    juce::StringArray warnings;
    const juce::String patterns = R"([{"id": "kick", "track": "Kick", "bars": 1, "notes": "0:1:36"},
        {"id": "bass", "track": "Bass", "bars": 1, "notes": "0:1:40"}])";
    const juce::var out = expandSong(songWith(patterns, R"([{"name": "A", "bars": 1, "play": [
        {"pattern": "nope"}, {"pattern": "kick", "fill": "missing"}, {"pattern": "kick", "fill": "bass"}]}])"),
                                     &warnings);
    EXPECT_EQ(clipsOn(out, "\"Kick\"")[0]["notes"].getArray()->size(), 2);
    const juce::String all = warnings.joinIntoString("\n");
    EXPECT_TRUE(all.contains("pattern \"nope\", which is not defined"));
    EXPECT_TRUE(all.contains("fill \"missing\", which is not defined"));
    EXPECT_TRUE(all.contains("different track"));
}

TEST(SongLayout, DuplicatePatternIdsAndOverBudgetSongsAreTrimmed) {
    juce::StringArray warnings;
    const juce::String dup = R"({"id": "kick", "track": "Kick", "bars": 1, "notes": "0:1:36"})";
    expandSong(songWith("[" + dup + "," + dup + "]", R"([{"name": "A", "bars": 1, "play": []}])"), &warnings);
    EXPECT_TRUE(warnings.joinIntoString("\n").contains("used more than once"));

    warnings.clear();
    juce::StringArray sections;
    for (int i = 0; i < 5; ++i)
        sections.add("{\"name\": \"S" + juce::String(i) + "\", \"bars\": 60, \"play\": []}");
    const juce::var out = expandSong(songWith("[]", "[" + sections.joinIntoString(",") + "]"), &warnings);
    EXPECT_TRUE(warnings.joinIntoString("\n").contains("256 bars"));
    EXPECT_EQ(opNames(out).joinIntoString(","), "setTempo,addMarker,addMarker,addMarker,addMarker");
}

TEST(SongLayout, TheOpLimitDropsTheSongsLastOpsAndKeepsTheModelsOwn) {
    juce::StringArray own;
    for (int i = 0; i < 63; ++i)
        own.add("{\"op\": \"addTrack\", \"name\": \"T" + juce::String(i) + "\", \"kind\": \"midi\"}");
    juce::StringArray warnings;
    const juce::var out =
        expandSong(envelope(kBaseSong, ", \"timelineOps\": [" + own.joinIntoString(",") + "]"), &warnings);
    EXPECT_EQ(opsOf(out).size(), 64);
    EXPECT_EQ(opsOf(out)[63]["op"].toString(), "setTempo");
    EXPECT_TRUE(warnings.joinIntoString("\n").contains("limit of 64"));
}

TEST(SongLayout, ABadBpmSkipsTheTempoOp) {
    juce::StringArray warnings;
    const juce::var out = expandSong(songWith("[]", R"([{"name": "S", "bars": 1, "play": []}])", 500), &warnings);
    EXPECT_EQ(opNames(out).joinIntoString(","), "addMarker");
    EXPECT_FALSE(warnings.isEmpty());
}

TEST(SongLayout, ParseSongNotesDefaultsVelocityAndNamesTheProblem) {
    std::vector<SongNote> notes;
    juce::String error;
    ASSERT_TRUE(parseSongNotes("0:1:60 1:1:62:80 \n 2:0.5:64", 8, "p", notes, error)) << error;
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0].velocity, 100);
    EXPECT_EQ(notes[1].velocity, 80);
    EXPECT_DOUBLE_EQ(notes[2].lengthBeats, 0.5);

    const auto problem = [](const char* text) {
        std::vector<SongNote> parsed;
        juce::String message;
        parseSongNotes(text, 1, "p", parsed, message);
        return message;
    };
    EXPECT_TRUE(problem("0:1:128").contains("pitch 128"));
    EXPECT_TRUE(problem("0:1:60.5").contains("pitch 60.5"));
    EXPECT_TRUE(problem("0:1:60:0").contains("velocity 0"));
    EXPECT_TRUE(problem("0:1:60:128").contains("velocity 128"));
    EXPECT_TRUE(problem("0:0:60").contains("length 0"));
    EXPECT_TRUE(problem("-1:1:60").contains("starts at beat -1"));
    EXPECT_TRUE(problem("4:1:60").contains("1 bars (4 beats)"));
    EXPECT_TRUE(problem("{\"startBeat\":0}").contains("Pattern \"p\" note 1"));
    EXPECT_TRUE(problem("0:1").contains("is not start:length:pitch"));
    EXPECT_TRUE(problem("0::60").contains("is not start:length:pitch"));
    EXPECT_TRUE(problem("3.5:1:60").isEmpty());
}
