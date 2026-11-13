// The song steps wire format: NDJSON lines cut from arbitrary reads, each event kind, a plan reply, and the build
// model the build card reads (running -> fixing -> done or skipped, and the progress count).
#include "AI/SongSteps/SongSteps.h"
#include <gtest/gtest.h>

using namespace synth::songsteps;

namespace {

std::vector<juce::String> splitChunks(const std::vector<std::string>& chunks, bool finish = true) {
    LineSplitter splitter;
    std::vector<juce::String> lines;
    auto sink = [&](const juce::String& line) { lines.push_back(line); };
    for (const auto& chunk : chunks)
        splitter.push(chunk.data(), chunk.size(), sink);
    if (finish)
        splitter.finish(sink);
    return lines;
}

StreamEvent line(const char* json) {
    const auto event = parseStreamLine(json);
    EXPECT_TRUE(event.has_value()) << json;
    return event.value_or(StreamEvent{});
}

BuildModel threeStepBuild() {
    BuildModel model;
    model.apply(line(R"({"type":"steps","steps":[{"id":"track:Bass","kind":"track","label":"Bass sound"},
        {"id":"pattern:Bass","kind":"pattern","label":"Writing the bass line"},{"id":"sections","kind":"sections","label":"Laying out"}]})"));
    return model;
}

const char* kPlanData = R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"A rolling break.",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"},{"name":"Drop","bars":32,"energy":5,"chords":"Am F","description":"full"}],
    "tracks":[{"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"punchy","sections":["Intro","Drop"]}],
    "lanes":[{"track":"Kit","what":"filter opens"}]},
    "price":{"units":5,"estimatedUsd":0.02},"steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}],"token":"abc.def"})";

} // namespace

TEST(SongStepsLineSplitter, ALineSplitAcrossReadsArrivesWholeOnItsNewline) {
    const auto lines = splitChunks({R"({"type":"pi)", "ng\"}\n{\"type\":\"", "ping\"}\n"});
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], juce::String(R"({"type":"ping"})"));
    EXPECT_EQ(lines[1], juce::String(R"({"type":"ping"})"));
}

TEST(SongStepsLineSplitter, SeveralLinesInOneReadAndCrlfAreCutCleanly) {
    const auto lines = splitChunks({"a\r\nb\n\nc\n"});
    ASSERT_EQ(lines.size(), 3u); // the blank line is dropped
    EXPECT_EQ(lines[0], juce::String("a"));
    EXPECT_EQ(lines[2], juce::String("c"));
}

TEST(SongStepsLineSplitter, ALastLineWithoutNewlineComesOutOnFinishOnly) {
    EXPECT_TRUE(splitChunks({"tail"}, /*finish=*/false).empty());
    const auto lines = splitChunks({"tail"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], juce::String("tail"));
}

TEST(SongStepsLineSplitter, AMultiByteCharacterCutByAReadBoundaryStaysWhole) {
    std::string whole = "{\"a\":\"";
    whole += static_cast<char>(0xC3); // an e with an acute accent is two bytes
    whole += static_cast<char>(0xA9);
    whole += "\"}\n";
    const auto cut = whole.find(static_cast<char>(0xC3)) + 1;
    const auto lines = splitChunks({whole.substr(0, cut), whole.substr(cut)});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], juce::String::fromUTF8(whole.substr(0, whole.size() - 1).c_str()));
}

TEST(SongStepsEvents, EveryEventKindParses) {
    const auto steps = line(R"({"type":"steps","steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}]})");
    ASSERT_EQ(steps.type, EventType::steps);
    ASSERT_EQ(steps.steps.size(), 1u);
    EXPECT_EQ(steps.steps[0].kind, StepKind::track);

    const auto step = line(R"({"type":"step","id":"track:Kit","status":"done","ms":2100,"summary":"3 patterns"})");
    EXPECT_EQ(step.type, EventType::step);
    EXPECT_EQ(step.status, StepStatus::done);
    EXPECT_EQ(step.ms, 2100);
    EXPECT_EQ(step.stepSummary, juce::String("3 patterns"));

    EXPECT_EQ(line(R"({"type":"ping"})").type, EventType::ping);

    const auto done = line(R"({"type":"done","envelope":{"mode":"merge"},"notes":["automation skipped"],
        "summary":{"tracks":4,"sections":7,"bars":120,"seconds":200}})");
    EXPECT_EQ(done.type, EventType::done);
    EXPECT_TRUE(done.envelope.isObject());
    EXPECT_EQ(done.notes.size(), 1);
    EXPECT_EQ(done.sections, 7);
    EXPECT_DOUBLE_EQ(done.seconds, 200.0);

    const auto error = line(R"({"type":"error","message":"The build failed","refunded":true})");
    EXPECT_EQ(error.type, EventType::error);
    EXPECT_TRUE(error.refunded);
}

TEST(SongStepsEvents, MalformedBlankAndUnknownLinesAreSkippedNotFatal) {
    EXPECT_FALSE(parseStreamLine("").has_value());
    EXPECT_FALSE(parseStreamLine("   ").has_value());
    EXPECT_FALSE(parseStreamLine("{not json").has_value());
    EXPECT_FALSE(parseStreamLine("[1,2]").has_value());
    EXPECT_FALSE(parseStreamLine(R"({"type":"future-event"})").has_value());
    EXPECT_FALSE(parseStreamLine(R"({"type":"step","id":"x","status":"exploded"})").has_value());
    EXPECT_FALSE(parseStreamLine(R"({"type":"done"})").has_value()) << "a done line without an envelope is unusable";
}

TEST(SongStepsEvents, AnUnknownStepKindStillGetsAnIconKind) {
    EXPECT_EQ(stepKindFromString("something-new"), StepKind::plan);
    for (int i = 0; i <= (int)StepKind::mix; ++i)
        EXPECT_EQ(stepKindFromString(stepKindToString((StepKind)i)), (StepKind)i);
}

TEST(SongStepsPlan, ReplyReadsThePlanPriceAndToken) {
    PlanReply reply;
    juce::String error;
    ASSERT_TRUE(parsePlanReply(kPlanData, reply, error)) << error;
    EXPECT_EQ(reply.plan.title, juce::String("Dark drum and bass"));
    EXPECT_EQ(reply.units, 5);
    EXPECT_EQ(reply.token, juce::String("abc.def"));
    EXPECT_EQ(reply.plan.laneCount, 1);
    EXPECT_EQ(reply.plan.totalBars(), 48);
    EXPECT_EQ(reply.plan.durationText(), juce::String("1:06")); // 48 bars x 4 beats at 174 BPM
    EXPECT_TRUE(reply.plan.raw.isObject()) << "the plan object is kept verbatim for /build";
    EXPECT_EQ(planMetaText(reply.plan), juce::String::fromUTF8("1:06 \xC2\xB7 174 BPM \xC2\xB7 2 sections"));
}

TEST(SongStepsPlan, AReplyWithoutSectionsOrTokenIsRefused) {
    PlanReply reply;
    juce::String error;
    EXPECT_FALSE(parsePlanReply("not json", reply, error));
    EXPECT_FALSE(parsePlanReply(R"({"plan":{"title":"x","sections":[],"tracks":[]},"token":"t"})", reply, error));
    EXPECT_FALSE(
        parsePlanReply(juce::String(kPlanData).replace(R"("token":"abc.def")", R"("token":"")"), reply, error));
}

TEST(SongStepsBuildModel, ARunningStepMovesThroughFixingToDoneAndCountsProgress) {
    auto model = threeStepBuild();
    EXPECT_EQ(model.total(), 3);
    EXPECT_EQ(model.currentIndex(), -1);
    EXPECT_FLOAT_EQ(model.progress(), 0.0f);

    model.apply(line(R"({"type":"step","id":"track:Bass","status":"running"})"));
    EXPECT_EQ(model.currentIndex(), 0);
    EXPECT_EQ(model.currentNumber(), 1);

    model.apply(line(R"({"type":"step","id":"track:Bass","status":"fixing"})"));
    EXPECT_TRUE(model.isFixing());

    model.apply(line(R"({"type":"step","id":"track:Bass","status":"done","ms":900,"summary":"warm saw"})"));
    EXPECT_FALSE(model.isFixing());
    EXPECT_EQ(model.finishedCount(), 1);
    EXPECT_EQ(model.steps()[0].ms, 900);
    EXPECT_EQ(model.steps()[0].summary, juce::String("warm saw"));
    EXPECT_NEAR(model.progress(), 1.0f / 3.0f, 1e-5f);

    model.apply(line(R"({"type":"step","id":"pattern:Bass","status":"running"})"));
    EXPECT_EQ(model.currentIndex(), 1);
    EXPECT_EQ(model.currentNumber(), 2) << "step 2 of 3: one is finished, this is the next";
}

TEST(SongStepsBuildModel, ASkippedStepCountsAsFinishedAndIsNamedInTheResult) {
    auto model = threeStepBuild();
    model.apply(line(R"({"type":"step","id":"track:Bass","status":"done"})"));
    model.apply(line(R"({"type":"step","id":"pattern:Bass","status":"skipped"})"));
    model.apply(line(R"({"type":"step","id":"sections","status":"done"})"));
    EXPECT_TRUE(model.hasSkipped());
    EXPECT_EQ(model.finishedCount(), 3);
    model.apply(line(R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":7,"seconds":200}})"));
    EXPECT_TRUE(model.isDone());
    EXPECT_FLOAT_EQ(model.progress(), 1.0f);
    EXPECT_EQ(model.resultLine(), juce::String("3 steps, 7 sections, 3:20 (1 detail left out)"));
}

TEST(SongStepsBuildModel, ALateRunningLineDoesNotReopenAFinishedStep) {
    auto model = threeStepBuild();
    model.apply(line(R"({"type":"step","id":"track:Bass","status":"done"})"));
    EXPECT_FALSE(model.apply(line(R"({"type":"step","id":"track:Bass","status":"running"})")));
    EXPECT_EQ(model.steps()[0].status, StepStatus::done);
    EXPECT_FALSE(model.apply(line(R"({"type":"step","id":"nope","status":"running"})")))
        << "unknown step ids are ignored";
    EXPECT_FALSE(model.apply(line(R"({"type":"ping"})"))) << "a ping changes nothing on screen";
}

TEST(SongStepsBuildModel, AnErrorLineEndsTheBuildAndRemembersTheRefund) {
    auto model = threeStepBuild();
    model.apply(line(R"({"type":"step","id":"track:Bass","status":"running"})"));
    model.apply(line(R"({"type":"error","message":"The build failed","refunded":true})"));
    EXPECT_TRUE(model.isFinished());
    EXPECT_TRUE(model.isFailed());
    EXPECT_TRUE(model.wasRefunded());
    EXPECT_EQ(model.failureMessage(), juce::String("The build failed"));
    EXPECT_EQ(model.currentIndex(), -1) << "nothing is still running after a failure";
}
