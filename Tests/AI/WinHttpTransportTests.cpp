#include <juce_core/juce_core.h>

#if JUCE_WINDOWS

#include "AI/WinHttpTransport.h"
#include <atomic>
#include <gtest/gtest.h>

// No test here opens a socket: each one fails before any connection is attempted.

TEST(WinHttpTransportTest, AlreadyCancelledRequestAbortsWithoutConnecting) {
    const std::atomic<bool> cancelled{true};
    const auto r = synth::performWinHttpRequest("GET", "https://example.invalid/", {}, {}, 5000, cancelled);
    EXPECT_TRUE(r.transportFailed);
    EXPECT_FALSE(r.timedOut);
    EXPECT_EQ(r.errorMessage, juce::String("Request aborted (cancelled)."));
}

TEST(WinHttpTransportTest, MalformedUrlFailsAsTransportError) {
    const std::atomic<bool> cancelled{false};
    const auto r = synth::performWinHttpRequest("GET", "not a url", {}, {}, 5000, cancelled);
    EXPECT_TRUE(r.transportFailed);
    EXPECT_EQ(r.httpStatus, 0);
    EXPECT_TRUE(r.errorMessage.isNotEmpty());
}

TEST(WinHttpTransportTest, UnsupportedSchemeIsRejected) {
    const std::atomic<bool> cancelled{false};
    const auto r = synth::performWinHttpRequest("GET", "ftp://example.invalid/file", {}, {}, 5000, cancelled);
    EXPECT_TRUE(r.transportFailed);
}

#endif // JUCE_WINDOWS
