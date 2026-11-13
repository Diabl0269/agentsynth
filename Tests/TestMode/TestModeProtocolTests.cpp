// The test mode's wire format: one JSON object per line each way. Parsing never throws, and a bad line still gets an
// answer the client can match to what it sent.

#include "TestMode/TestModeProtocol.h"
#include <gtest/gtest.h>

using namespace synth::testmode;

TEST(TestModeProtocol, ParsesACommandWithArgsAndTimeout) {
    const auto parsed = parseRequestLine(R"({"id":7,"cmd":"click","args":{"id":"x","clicks":2},"timeoutMs":900})");
    ASSERT_TRUE(parsed.request.has_value());
    EXPECT_EQ(static_cast<int>(parsed.request->id), 7);
    EXPECT_EQ(parsed.request->cmd, "click");
    EXPECT_EQ(parsed.request->args["id"].toString(), "x");
    EXPECT_EQ(static_cast<int>(parsed.request->args["clicks"]), 2);
    EXPECT_EQ(parsed.request->timeoutMs, 900);
}

TEST(TestModeProtocol, ArgsAreOptional) {
    const auto parsed = parseRequestLine(R"({"id":"a","cmd":"ping"})");
    ASSERT_TRUE(parsed.request.has_value());
    EXPECT_NE(parsed.request->args.getDynamicObject(), nullptr);
    EXPECT_EQ(parsed.request->timeoutMs, 0);
}

TEST(TestModeProtocol, MalformedJsonIsAnErrorNotAThrow) {
    for (const char* line : {"not json", "{", "", "[1,2]", "42", "\"cmd\""}) {
        SCOPED_TRACE(line);
        const auto parsed = parseRequestLine(line);
        EXPECT_FALSE(parsed.request.has_value());
        EXPECT_TRUE(parsed.error.isNotEmpty());
    }
}

TEST(TestModeProtocol, AMissingOrNonStringCommandKeepsTheIdForTheReply) {
    const auto missing = parseRequestLine(R"({"id":3})");
    EXPECT_FALSE(missing.request.has_value());
    EXPECT_EQ(static_cast<int>(missing.id), 3);
    const auto wrongType = parseRequestLine(R"({"id":4,"cmd":5})");
    EXPECT_FALSE(wrongType.request.has_value());
    EXPECT_EQ(static_cast<int>(wrongType.id), 4);
}

TEST(TestModeProtocol, ArgsThatAreNotAnObjectAreRefused) {
    EXPECT_FALSE(parseRequestLine(R"({"cmd":"list","args":[1]})").request.has_value());
}

TEST(TestModeProtocol, RepliesAreSingleLineJsonThatRoundTrips) {
    const auto ok = formatOk(5, juce::var("multi\nline"));
    EXPECT_FALSE(ok.containsChar('\n'));
    const auto okJson = juce::JSON::parse(ok);
    EXPECT_TRUE(static_cast<bool>(okJson["ok"]));
    EXPECT_EQ(static_cast<int>(okJson["id"]), 5);
    EXPECT_EQ(okJson["result"].toString(), "multi\nline");

    const auto bad = juce::JSON::parse(formatError("q", "went wrong"));
    EXPECT_FALSE(static_cast<bool>(bad["ok"]));
    EXPECT_EQ(bad["id"].toString(), "q");
    EXPECT_EQ(bad["error"].toString(), "went wrong");
}
