// Scanning basics and the eager/ensure-scanned lifecycle: shared-service listener fan-out and the
// warm-cache paths that keep a later ensureScanned() from relaunching what's already known (FRO44).

#include "PluginScanTestHelpers.h"

// ============================================================================
// 1. Scanning
// ============================================================================

TEST(PluginScanTest, ScanAddsDescribedPlugins) {
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());

    const auto result = scanToCompletion(service);

    EXPECT_EQ(result.total, 2);
    EXPECT_EQ(result.added, 2);
    EXPECT_EQ(result.failed, 0);
    EXPECT_FALSE(result.cancelled);
    EXPECT_EQ(service.getNumKnownPlugins(), 2);

    // Sorted by name, and each row carries the format the user has to be able to tell apart.
    const auto identities = service.getKnownPluginIdentities();
    ASSERT_EQ(identities.size(), 2u);
    EXPECT_EQ(identities[0].name, "Alpha");
    EXPECT_EQ(identities[1].name, "Beta");
    EXPECT_EQ(identities[0].format, "VST3");

    // --- Persistence: the owner saves toXml() and a fresh service comes back identical -----------
    auto xml = service.toXml();
    ASSERT_NE(xml, nullptr);

    PluginScanService restored;
    restored.loadFromXml(*xml);
    EXPECT_EQ(restored.getNumKnownPlugins(), 2);

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    const auto resolved = restored.resolve(alpha);
    ASSERT_TRUE(resolved.has_value()) << "a restored list must still resolve identities";
    EXPECT_EQ(resolved->name, "Alpha");
    // The path is what a restored list is FOR: it is the only place fileOrIdentifier ever lives.
    EXPECT_EQ(resolved->fileOrIdentifier, kAlpha);
}

TEST(PluginScanTest, RescanningAKnownPluginIsNotCountedAsNewAndDoesNotBlacklistIt) {
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());

    EXPECT_EQ(scanToCompletion(service).added, 1);

    const auto second = scanToCompletion(service);
    EXPECT_EQ(second.added, 0) << "already known, so nothing is new";
    EXPECT_EQ(second.failed, 0) << "'described nothing NEW' is not a failure — only 'described nothing' is";
    EXPECT_TRUE(service.getBlacklist().isEmpty());
    EXPECT_EQ(service.getNumKnownPlugins(), 1);
}

// ============================================================================
// 1b. Eager population / shared listeners (FRO44)
//
// One PluginScanService is meant to be shared by several consumers (the sidebar today, a future
// Instrument-track plugin picker) without each one owning — or re-triggering — its own scan.
// ============================================================================

namespace {

/** A consumer that just wants to know when a scan it may or may not have triggered finishes —
 *  stands in for the sidebar, a picker, or anything else registered on the shared service. */
struct RecordingListener : PluginScanService::Listener {
    std::vector<PluginScanService::Result> results;
    void pluginScanCompleted(const PluginScanService::Result& result) override { results.push_back(result); }
};

} // namespace

TEST(PluginScanTest, EnsureScannedTriggersExactlyOneScanNoMatterHowManyConsumersAsk) {
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);

    int candidateSourceCalls = 0;
    PluginScanService service;
    service.setCandidateSource([&](const juce::String&) {
        ++candidateSourceCalls;
        return juce::StringArray(kAlpha);
    });
    service.setChildLauncher(launcher.fn());

    // Three independent entry points — startup, the sidebar opening, a picker opening — all ask in
    // the same run, before anything has completed.
    RecordingListener startup, sidebar, picker;
    service.addListener(&startup);
    service.addListener(&sidebar);
    service.addListener(&picker);

    service.ensureScanned(juce::StringArray("VST3"));
    service.ensureScanned(juce::StringArray("VST3"));
    service.ensureScanned(juce::StringArray("VST3"));

    ASSERT_TRUE(pumpUntil([&] { return !startup.results.empty(); })) << "the one real scan never completed";

    EXPECT_EQ(candidateSourceCalls, 1) << "only the FIRST ensureScanned() call may start a real scan";
    EXPECT_EQ(launcher.launchCountFor(kAlpha), 1);
    EXPECT_EQ(service.getNumKnownPlugins(), 1);

    // Every registered consumer hears about it, not just whichever call happened to win the race.
    ASSERT_EQ(startup.results.size(), 1u);
    ASSERT_EQ(sidebar.results.size(), 1u);
    ASSERT_EQ(picker.results.size(), 1u);
    EXPECT_EQ(startup.results[0].added, 1);
    EXPECT_EQ(sidebar.results[0].added, 1);
    EXPECT_EQ(picker.results[0].added, 1);

    service.removeListener(&startup);
    service.removeListener(&sidebar);
    service.removeListener(&picker);
}

TEST(PluginScanTest, EnsureScannedNeverRescansOnceItHasAlreadyCompleted) {
    // "Persist/reuse the cached list so startup doesn't rescan everything every launch" — the flip
    // side of the exactly-one-scan test above: a LATER ensureScanned() (a picker opened well after
    // the eager startup scan already finished) must not launch a second one.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());

    service.ensureScanned(juce::StringArray("VST3"));
    ASSERT_TRUE(pumpUntil([&] { return service.getNumKnownPlugins() > 0; }));
    EXPECT_EQ(launcher.launchCountFor(kAlpha), 1);

    service.ensureScanned(juce::StringArray("VST3"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(launcher.launchCountFor(kAlpha), 1) << "a later ensureScanned() must not trigger a second scan";

    // The explicit rescan path (the sidebar's "Scan for plugins..." row) is unaffected — it always
    // calls scanAsync() directly and must still be able to find new plugins.
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    EXPECT_EQ(scanToCompletion(service).added, 1);
    EXPECT_EQ(service.getNumKnownPlugins(), 2);
}

TEST(PluginScanTest, EnsureScannedReportsProgressAndWhetherItActuallyStartedAScan) {
    // FRO105: ensureScanned() used to take no progress callback at all, so the eager startup scan
    // (its only production caller) ran silently with no way to tell the user a scan was in flight.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);
    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());

    std::vector<juce::String> progressed;
    const bool started =
        service.ensureScanned(juce::StringArray("VST3"), [&](const juce::String& fileOrIdentifier, int, int) {
            progressed.push_back(fileOrIdentifier);
        });
    EXPECT_TRUE(started) << "the first call must report that it actually started a scan";

    ASSERT_TRUE(pumpUntil([&] { return service.getNumKnownPlugins() == 2; }));
    EXPECT_EQ(progressed, (std::vector<juce::String>{kAlpha, kBeta}))
        << "every candidate must reach the caller's progress callback, in scan order";

    // A later call is the documented no-op: it reports it did NOT start a scan, and its own progress
    // callback is never invoked for a scan it never started.
    bool laterProgressInvoked = false;
    const bool startedAgain = service.ensureScanned(
        juce::StringArray("VST3"), [&](const juce::String&, int, int) { laterProgressInvoked = true; });
    EXPECT_FALSE(startedAgain);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_FALSE(laterProgressInvoked);
}

TEST(PluginScanTest, ConsumersSeeThePluginListWithoutTheSidebarEverOpening) {
    // No ModuleLibraryComponent is constructed anywhere in this test — a future picker reading
    // straight off the shared service must see the scanned plugin without anyone ever opening the
    // library sidebar's PLUGINS section.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());

    EXPECT_EQ(service.getNumKnownPlugins(), 0);
    service.ensureScanned(juce::StringArray("VST3"));
    ASSERT_TRUE(pumpUntil([&] { return service.getNumKnownPlugins() > 0; }));

    const auto identities = service.getKnownPluginIdentities();
    ASSERT_EQ(identities.size(), 1u);
    EXPECT_EQ(identities[0].name, "Alpha");
}

TEST(PluginScanTest, ScanAsyncNotifiesRegisteredListenersEvenWithNoCompletionCallback) {
    // scanAsync() itself (not just ensureScanned()) must reach every registered Listener — this is
    // what lets the sidebar's manual "Scan for plugins..." row and the eager path share one
    // completion handler (MainComponent::pluginScanCompleted) instead of duplicating it.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());

    RecordingListener listener;
    service.addListener(&listener);
    service.scanAsync(juce::StringArray("VST3"), {}, {});
    ASSERT_TRUE(pumpUntil([&] { return !listener.results.empty(); }));
    EXPECT_EQ(listener.results[0].added, 1);
    EXPECT_FALSE(listener.results[0].cancelled);

    service.removeListener(&listener);
}

TEST(PluginScanTest, RemovedListenerHearsNothingFurther) {
    PluginScanService service;
    service.setCandidateSource(candidates({}));

    RecordingListener listener;
    service.addListener(&listener);
    service.removeListener(&listener);

    scanToCompletion(service);
    EXPECT_TRUE(listener.results.empty()) << "a removed listener must not be notified";
}

TEST(PluginScanTest, EnsureScannedNeverRelaunchesAnAlreadyKnownPluginOnAWarmCache) {
    // "Persist/reuse the cached list so startup doesn't rescan everything every launch" (FRO44 spec)
    // — the persisted-list equivalent of EnsureScannedNeverRescansOnceItHasAlreadyCompleted above,
    // but for the case that actually happens on every real relaunch of the app: a FRESH
    // PluginScanService instance (ensureScanRequested_ latch reset) that loaded yesterday's saved
    // list via loadFromXml() before anyone calls ensureScanned(). Alpha is already known; only Beta
    // is newly on disk. A per-launch child-process probe of every installed plugin — not just the
    // new one — is exactly the regression this test guards against.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    std::unique_ptr<juce::XmlElement> savedList;
    {
        PluginScanService priorLaunch;
        priorLaunch.setCandidateSource(candidates({kAlpha}));
        priorLaunch.setChildLauncher(launcher.fn());
        ASSERT_EQ(scanToCompletion(priorLaunch).added, 1);
        savedList = priorLaunch.toXml();
        ASSERT_NE(savedList, nullptr);
    }

    PluginScanService service;
    service.loadFromXml(*savedList);
    ASSERT_EQ(service.getNumKnownPlugins(), 1) << "the persisted list must be visible before any scan runs";
    launcher.launched.clear();

    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());
    service.ensureScanned(juce::StringArray("VST3"));
    ASSERT_TRUE(pumpUntil([&] { return service.getNumKnownPlugins() > 1; }));

    EXPECT_EQ(launcher.launchCountFor(kAlpha), 0)
        << "an already-known plugin must not be relaunched by the automatic/eager path";
    EXPECT_EQ(launcher.launchCountFor(kBeta), 1) << "a newly installed plugin must still be probed";
    EXPECT_EQ(service.getNumKnownPlugins(), 2);

    // The manual "Scan for plugins..." row is a different call (scanAsync's default
    // skipAlreadyKnown=false) and must keep re-probing everything, e.g. to notice an in-place update.
    EXPECT_EQ(scanToCompletion(service).added, 0) << "no NEW plugin, but the launcher must still run";
    EXPECT_EQ(launcher.launchCountFor(kAlpha), 1) << "the manual rescan path must still re-probe Alpha";
}
