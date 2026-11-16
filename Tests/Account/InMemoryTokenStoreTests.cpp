#include "Auth/InMemoryTokenStore.h"
#include <atomic>
#include <gtest/gtest.h>
#include <thread>

// The account service's worker thread saves and clears the token while the message thread reads it,
// so the store has to be safe for that. Plain asserts catch a torn read; the ThreadSanitizer CI job
// (the run-tsan label) catches the unsynchronised access itself.
TEST(InMemoryTokenStore, SaveLoadAndClearRoundTrip) {
    synth::InMemoryTokenStore store;
    EXPECT_TRUE(store.load().isEmpty());
    EXPECT_TRUE(store.save("refresh-token"));
    EXPECT_EQ(store.load(), juce::String("refresh-token"));
    store.clear();
    EXPECT_TRUE(store.load().isEmpty());
}

TEST(InMemoryTokenStore, ReadingWhileAnotherThreadSavesAndClearsNeverSeesATornToken) {
    synth::InMemoryTokenStore store;
    const juce::String token("a-long-refresh-token-that-is-not-short-enough-to-sit-inline");
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load()) {
            store.save(token);
            store.clear();
        }
    });
    int torn = 0;
    for (int i = 0; i < 20000; ++i) {
        const auto seen = store.load();
        if (seen.isNotEmpty() && seen != token)
            ++torn;
    }
    stop.store(true);
    writer.join();
    EXPECT_EQ(torn, 0);
}
