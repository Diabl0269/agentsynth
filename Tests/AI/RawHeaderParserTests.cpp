#include "AI/RawHeaderParser.h"
#include <gtest/gtest.h>

TEST(RawHeaderParserTest, ParsesCrlfSeparatedHeadersAndSkipsTheStatusLine) {
    const auto headers = synth::parseRawHttpHeaders("HTTP/1.1 200 OK\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "X-Request-Id:  abc-123  \r\n"
                                                    "\r\n");
    EXPECT_EQ(headers.size(), 2);
    EXPECT_EQ(headers["Content-Type"], juce::String("application/json"));
    EXPECT_EQ(headers["X-Request-Id"], juce::String("abc-123"));
}

TEST(RawHeaderParserTest, KeepsTheServersKeyCasing) {
    const auto headers = synth::parseRawHttpHeaders("retry-after: 30\r\n");
    ASSERT_EQ(headers.getAllKeys().size(), 1);
    EXPECT_EQ(headers.getAllKeys()[0], juce::String("retry-after"));
    EXPECT_EQ(headers["Retry-After"], juce::String("30")); // lookup is case-insensitive
}

TEST(RawHeaderParserTest, ValueMayContainColons) {
    const auto headers = synth::parseRawHttpHeaders("Location: https://example.com:8443/path\n");
    EXPECT_EQ(headers["Location"], juce::String("https://example.com:8443/path"));
}

TEST(RawHeaderParserTest, RepeatedNameKeepsTheLastValue) {
    const auto headers = synth::parseRawHttpHeaders("X-A: one\r\nX-A: two\r\n");
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["X-A"], juce::String("two"));
}

TEST(RawHeaderParserTest, SkipsLinesWithoutANameOrColon) {
    const auto headers = synth::parseRawHttpHeaders(": no-name\r\nnot a header line\r\n   : blank\r\nOk: yes\r\n");
    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers["Ok"], juce::String("yes"));
}

TEST(RawHeaderParserTest, EmptyInputGivesNoHeaders) { EXPECT_EQ(synth::parseRawHttpHeaders({}).size(), 0); }
