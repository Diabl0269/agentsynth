// Crash isolation (blacklisting a failing candidate, surviving a save/load round trip) and timeouts,
// which are deliberately indistinguishable from a crash to the parent.

#include "PluginScanTestHelpers.h"

// ============================================================================
// 2. Crash isolation
// ============================================================================

TEST(PluginScanTest, CrashBlacklistsAndContinues) {
    FakeLauncher launcher;
    // Crasher deliberately absent from the map: the launcher returns false for it.
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    PluginScanService service;
    service.setCandidateSource(candidates({kCrasher, kBeta}));
    service.setChildLauncher(launcher.fn());

    const auto first = scanToCompletion(service);
    EXPECT_EQ(first.failed, 1);
    EXPECT_EQ(first.added, 1) << "a crash must not abort the scan — the plugin after it still lands";
    EXPECT_EQ(service.getNumKnownPlugins(), 1);
    EXPECT_TRUE(service.getBlacklist().contains(kCrasher));
    EXPECT_FALSE(service.getBlacklist().contains(kBeta));
    EXPECT_EQ(launcher.launchCountFor(kCrasher), 1);

    // --- A rescan must not walk back into it ---------------------------------------------------
    const auto second = scanToCompletion(service);
    EXPECT_EQ(second.skipped, 1);
    EXPECT_EQ(launcher.launchCountFor(kCrasher), 1) << "a blacklisted candidate must never be launched again";
    EXPECT_EQ(launcher.launchCountFor(kBeta), 2) << "...while everything else is rescanned normally";

    // --- clearBlacklist is the only way back in -------------------------------------------------
    service.clearBlacklist();
    EXPECT_TRUE(service.getBlacklist().isEmpty());

    const auto third = scanToCompletion(service);
    EXPECT_EQ(launcher.launchCountFor(kCrasher), 2) << "clearing the blacklist must let the plugin be retried";
    EXPECT_EQ(third.failed, 1) << "...and it crashes again, so it goes straight back on the list";
    EXPECT_TRUE(service.getBlacklist().contains(kCrasher));
}

TEST(PluginScanTest, GarbageOutputIsTreatedAsAFailure) {
    FakeLauncher launcher;
    // "Succeeded" but printed something that is not a description document at all.
    launcher.xmlByFile[kAlpha] = "this is not xml";

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());

    const auto result = scanToCompletion(service);
    EXPECT_EQ(result.added, 0);
    EXPECT_EQ(result.failed, 1);
    EXPECT_EQ(service.getNumKnownPlugins(), 0);
    EXPECT_TRUE(service.getBlacklist().contains(kAlpha));
}

TEST(PluginScanTest, BlacklistSurvivesTheSaveLoadRoundTrip) {
    // Otherwise a crashing plugin is rediscovered and re-probed on every launch, which is the crash
    // loop the blacklist exists to break.
    FakeLauncher launcher;
    PluginScanService service;
    service.setCandidateSource(candidates({kCrasher}));
    service.setChildLauncher(launcher.fn());
    scanToCompletion(service);
    ASSERT_TRUE(service.getBlacklist().contains(kCrasher));

    auto xml = service.toXml();
    ASSERT_NE(xml, nullptr);

    PluginScanService restored;
    restored.loadFromXml(*xml);
    EXPECT_TRUE(restored.getBlacklist().contains(kCrasher));
}

// ============================================================================
// 3. Timeouts
// ============================================================================

TEST(PluginScanTest, TimeoutTreatedAsCrash) {
    // The launcher seam stands in for the real watchdog: it takes longer than the (tiny, injected)
    // timeout and then reports failure, which is exactly what launchScanChildProcess does after it
    // kills a hung child. What is pinned here is the SCAN's response, which must be identical to a
    // crash — hangs and crashes are indistinguishable from the parent and get the same treatment.
    FakeLauncher launcher;
    launcher.delayMs = 30;
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    PluginScanService service;
    service.setScanTimeoutMs(5);
    EXPECT_EQ(service.getScanTimeoutMs(), 5) << "the timeout must be injectable, or this test takes 15 s";
    service.setCandidateSource(candidates({kCrasher, kBeta}));
    service.setChildLauncher(launcher.fn());

    const auto result = scanToCompletion(service);
    EXPECT_EQ(result.failed, 1);
    EXPECT_EQ(result.added, 1) << "a hung plugin must not stop the ones after it";
    EXPECT_TRUE(service.getBlacklist().contains(kCrasher));
}

TEST(PluginScanTest, TheTimeoutIsHandedToTheLauncher) {
    int seenTimeout = 0;
    PluginScanService service;
    service.setScanTimeoutMs(1234);
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher([&seenTimeout](const juce::String&, const juce::String&, int timeoutMs, juce::String&) {
        seenTimeout = timeoutMs;
        return false;
    });

    scanToCompletion(service);
    EXPECT_EQ(seenTimeout, 1234);
    EXPECT_EQ(PluginScanService::kDefaultScanTimeoutMs, 15000) << "the shipped default, pinned so it cannot drift "
                                                                  "down to something that kills slow-but-honest "
                                                                  "plugins";
}
