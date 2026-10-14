// The chat export document: ChatTranscript::toJson is a pure function from the recorded turns to the JSON a user saves.
// Each turn kind, a failed turn, an empty conversation, ISO timestamps, and that the text parses back.
#include "AI/ChatTranscript.h"
#include <gtest/gtest.h>

using synth::ChatExportInfo;
using synth::ChatTranscript;
using synth::ChatTurn;

namespace {

ChatExportInfo info() {
    ChatExportInfo i;
    i.exportedAt = juce::Time(2026, 9, 14, 10, 30, 15, 0, false);
    i.appVersion = "0.202.0";
    i.providerKind = "remote";
    i.model = "gpt-oss";
    return i;
}

juce::var parsed(const juce::var& json) {
    const auto text = juce::JSON::toString(json);
    auto back = juce::JSON::parse(text);
    EXPECT_TRUE(back.isObject()) << text;
    return back;
}

juce::var turnAt(const juce::var& root, int index) { return root["turns"][index]; }

ChatTurn songTurn() {
    ChatTurn turn;
    turn.at = juce::Time(2026, 9, 14, 10, 0, 0, 0, false);
    turn.request = "a dark drum and bass song";
    turn.kind = ChatTurn::Kind::song;
    turn.plan = juce::JSON::parse(R"({"title":"Dark","bpm":174})");
    turn.steps = {{"track:Kit", synth::songsteps::StepKind::track, "Kit sound"}};
    turn.events = {juce::JSON::parse(R"({"type":"steps","steps":[]})"),
                   juce::JSON::parse(R"({"type":"done","envelope":{"nodes":[]},"notes":["skipped a fix"]})")};
    turn.envelope = juce::JSON::parse(R"({"nodes":[]})");
    turn.appliedOk = true;
    turn.notes.add("skipped a fix");
    return turn;
}

} // namespace

TEST(ChatTranscriptTest, EmptyConversationHasHeaderAndNoTurns) {
    const auto root = parsed(ChatTranscript().toJson(info()));
    EXPECT_EQ(root["format"].toString(), "agentsynth-chat");
    EXPECT_EQ((int)root["version"], 1);
    EXPECT_EQ(root["app"]["version"].toString(), "0.202.0");
    EXPECT_EQ(root["provider"]["kind"].toString(), "remote");
    EXPECT_EQ(root["provider"]["model"].toString(), "gpt-oss");
    ASSERT_TRUE(root["turns"].isArray());
    EXPECT_EQ(root["turns"].size(), 0);
}

TEST(ChatTranscriptTest, ModelIsLeftOutWhenUnknown) {
    auto i = info();
    i.model = {};
    i.providerKind = "ollama";
    const auto root = parsed(ChatTranscript().toJson(i));
    EXPECT_EQ(root["provider"]["kind"].toString(), "ollama");
    EXPECT_FALSE(root["provider"].getDynamicObject()->hasProperty("model"));
}

TEST(ChatTranscriptTest, TimestampsAreISO8601AndReadBack) {
    ChatTranscript transcript;
    transcript.addTurn(songTurn());
    const auto root = parsed(transcript.toJson(info()));
    for (const auto& text : {root["exportedAt"].toString(), turnAt(root, 0)["at"].toString()}) {
        EXPECT_TRUE(text.contains("T")) << text;
        EXPECT_NE(juce::Time::fromISO8601(text), juce::Time()) << text;
    }
    EXPECT_EQ(juce::Time::fromISO8601(turnAt(root, 0)["at"].toString()).toMilliseconds(),
              songTurn().at.toMilliseconds());
}

TEST(ChatTranscriptTest, SongTurnCarriesPlanStepsEventsAndEnvelope) {
    ChatTranscript transcript;
    transcript.addTurn(songTurn());
    const auto turn = turnAt(parsed(transcript.toJson(info())), 0);
    EXPECT_EQ(turn["request"].toString(), "a dark drum and bass song");
    EXPECT_EQ(turn["kind"].toString(), "song");
    EXPECT_EQ((int)turn["plan"]["bpm"], 174);
    ASSERT_EQ(turn["steps"].size(), 1);
    EXPECT_EQ(turn["steps"][0]["id"].toString(), "track:Kit");
    EXPECT_EQ(turn["steps"][0]["kind"].toString(), "track");
    EXPECT_EQ(turn["steps"][0]["label"].toString(), "Kit sound");
    ASSERT_EQ(turn["events"].size(), 2);
    EXPECT_EQ(turn["events"][0]["type"].toString(), "steps");
    EXPECT_EQ(turn["events"][1]["type"].toString(), "done");
    EXPECT_TRUE(turn["envelope"].isObject());
    EXPECT_TRUE((bool)turn["applied"]["ok"]);
    EXPECT_EQ(turn["applied"]["notes"][0].toString(), "skipped a fix");
    EXPECT_FALSE(turn["applied"].getDynamicObject()->hasProperty("error"));
    EXPECT_FALSE(turn.getDynamicObject()->hasProperty("response"));
}

TEST(ChatTranscriptTest, EditTurnHasEnvelopeContextAndAppliedNotes) {
    ChatTurn edit;
    edit.request = "add a chorus";
    edit.kind = ChatTurn::Kind::edit;
    edit.envelope = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Chorus"}],"timelineOps":[]})");
    edit.context = juce::JSON::parse(R"({"currentPatch":{"nodes":[]},"availableTracks":[]})");
    edit.appliedOk = true;
    edit.notes.add("moved a value into range");
    ChatTranscript transcript;
    transcript.addTurn(edit);
    const auto turn = turnAt(parsed(transcript.toJson(info())), 0);
    EXPECT_EQ(turn["kind"].toString(), "edit");
    EXPECT_EQ(turn["envelope"]["nodes"][0]["type"].toString(), "Chorus");
    EXPECT_TRUE(turn["context"].hasProperty("currentPatch"));
    EXPECT_TRUE((bool)turn["applied"]["ok"]);
    EXPECT_EQ(turn["applied"]["notes"].size(), 1);
    EXPECT_FALSE(turn.getDynamicObject()->hasProperty("plan"));
    EXPECT_FALSE(turn.getDynamicObject()->hasProperty("steps"));
    EXPECT_FALSE(turn.getDynamicObject()->hasProperty("events"));
}

TEST(ChatTranscriptTest, FailedTurnRecordsTheError) {
    ChatTurn failed;
    failed.request = "make a song";
    failed.kind = ChatTurn::Kind::song;
    failed.error = "The build could not finish.";
    ChatTranscript transcript;
    transcript.addTurn(failed);
    const auto turn = turnAt(parsed(transcript.toJson(info())), 0);
    EXPECT_FALSE((bool)turn["applied"]["ok"]);
    EXPECT_EQ(turn["applied"]["error"].toString(), "The build could not finish.");
    EXPECT_FALSE(turn.getDynamicObject()->hasProperty("envelope"));
}

TEST(ChatTranscriptTest, TextAnswerKeepsTheRawResponseAndNoEnvelope) {
    ChatTurn chat;
    chat.request = "what does the reverb decay do?";
    chat.kind = ChatTurn::Kind::chat;
    chat.response = "It sets how long the tail rings.";
    chat.appliedOk = true;
    ChatTranscript transcript;
    transcript.addTurn(chat);
    const auto turn = turnAt(parsed(transcript.toJson(info())), 0);
    EXPECT_EQ(turn["kind"].toString(), "chat");
    EXPECT_EQ(turn["response"].toString(), "It sets how long the tail rings.");
    EXPECT_FALSE(turn.getDynamicObject()->hasProperty("envelope"));
}

TEST(ChatTranscriptTest, TurnWithUnknownTimeLeavesAtOut) {
    ChatTurn restored;
    restored.request = "hello";
    ChatTranscript transcript;
    transcript.addTurn(restored);
    EXPECT_FALSE(turnAt(parsed(transcript.toJson(info())), 0).getDynamicObject()->hasProperty("at"));
}

TEST(ChatTranscriptTest, KindNamesAreTheDocumentedFour) {
    EXPECT_STREQ(synth::chatTurnKindName(ChatTurn::Kind::song), "song");
    EXPECT_STREQ(synth::chatTurnKindName(ChatTurn::Kind::edit), "edit");
    EXPECT_STREQ(synth::chatTurnKindName(ChatTurn::Kind::patch), "patch");
    EXPECT_STREQ(synth::chatTurnKindName(ChatTurn::Kind::chat), "chat");
}

TEST(ChatTranscriptTest, UpdateTurnChangesTheStoredTurnAndRejectsAnUnknownIndex) {
    ChatTranscript transcript;
    const auto index = transcript.addTurn(songTurn());
    EXPECT_TRUE(transcript.updateTurn(index, [](ChatTurn& turn) { turn.events.push_back(juce::var("ping")); }));
    EXPECT_EQ(transcript.turn(index)->events.size(), 3u);
    EXPECT_FALSE(transcript.updateTurn(index + 1, [](ChatTurn&) { FAIL(); }));
    transcript.clear();
    EXPECT_TRUE(transcript.empty());
    EXPECT_EQ(transcript.turn(0), nullptr);
}
