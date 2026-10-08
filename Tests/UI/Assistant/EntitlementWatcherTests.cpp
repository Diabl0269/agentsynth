#include "UI/Assistant/EntitlementWatcher.h"
#include <gtest/gtest.h>

using synth::EntitlementWatcher;

TEST(EntitlementWatcherTest, PollsUntilThePlanIsPro) {
    int refreshes = 0;
    bool pro = false;
    EntitlementWatcher watcher([&] { ++refreshes; }, [&] { return pro; });
    watcher.pollIntervalMs = 1000000;

    watcher.startWatchingForUpgrade();
    ASSERT_TRUE(watcher.isPolling());

    watcher.pollOnce();
    watcher.pollOnce();
    EXPECT_EQ(refreshes, 2);
    EXPECT_TRUE(watcher.isPolling());

    pro = true;
    watcher.pollOnce();
    EXPECT_EQ(refreshes, 2) << "no refresh once the plan is already Pro";
    EXPECT_FALSE(watcher.isPolling());
}

TEST(EntitlementWatcherTest, GivesUpWhenThePollWindowRunsOut) {
    int refreshes = 0;
    EntitlementWatcher watcher([&] { ++refreshes; }, [] { return false; });
    watcher.pollIntervalMs = 1000000;
    watcher.pollWindowMs = 0;

    watcher.startWatchingForUpgrade();
    juce::Thread::sleep(5);
    watcher.pollOnce();

    EXPECT_EQ(refreshes, 0);
    EXPECT_FALSE(watcher.isPolling());
}

TEST(EntitlementWatcherTest, DoesNotPollUntilAskedTo) {
    EntitlementWatcher watcher([] {}, [] { return false; });
    EXPECT_FALSE(watcher.isPolling());
}
