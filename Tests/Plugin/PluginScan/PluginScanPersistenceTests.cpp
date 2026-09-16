// Persistence via the owner (MainComponent stores the list under "pluginScanList", a hosted build
// resolves but never scans) and the eager startup scan (FRO44) that populates the sidebar unasked.

#include "PluginScanTestHelpers.h"

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"

// ============================================================================
// 8. Persistence via the owner
// ============================================================================

namespace {

/** A synchronous, network-free AI provider. Every MainComponent in this file is built with one, for
 *  the same reason MainComponentTests does: the default constructor installs the real Ollama
 *  provider, whose model fetch resolves off-thread and long after the component that asked. */
class SilentProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Silent"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"silent-model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Silent response.";
        callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "silent-model";
    int requestTimeoutMs = 240000;
};

/** The scan list lives in the same shared "Agent Synth" settings file MainComponent uses, so the key
 *  is reset around every test here — the MainComponentTests harness pattern. */
class PluginScanPersistenceTest : public ::testing::Test {
protected:
    static void clearScanList() {
        juce::PropertiesFile::Options options;
        options.applicationName = "Agent Synth";
        options.folderName = "Agent Synth";
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties properties;
        properties.setStorageParameters(options);
        if (auto* settings = properties.getUserSettings()) {
            settings->removeValue(MainComponent::kPluginScanListKey);
            settings->saveIfNeeded();
        }
    }

    void SetUp() override { clearScanList(); }
    void TearDown() override { clearScanList(); }
};

} // namespace

TEST_F(PluginScanPersistenceTest, PersistenceViaOwner) {
    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;

    // Produce a scanned list first, out of line: what this test is about is the OWNER's half of the
    // persistence contract, not the scan, and running the async scan inside a live MainComponent
    // would pump the message loop underneath a component whose AI panel has its own pending
    // callbacks — an unrelated hazard that would make this test flaky for reasons of its own.
    std::unique_ptr<juce::XmlElement> scannedList;
    {
        FakeLauncher launcher;
        launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
        PluginScanService scanner;
        scanner.setCandidateSource(candidates({kAlpha, kCrasher}));
        scanner.setChildLauncher(launcher.fn());
        scanToCompletion(scanner);
        ASSERT_EQ(scanner.getNumKnownPlugins(), 1);
        ASSERT_TRUE(scanner.getBlacklist().contains(kCrasher));
        scannedList = scanner.toXml();
        ASSERT_NE(scannedList, nullptr);
    }

    {
        MainComponent main(std::make_unique<SilentProvider>());
        auto& service = main.getPluginScanService();
        ASSERT_EQ(service.getNumKnownPlugins(), 0) << "nothing saved yet";

        service.loadFromXml(*scannedList);
        main.savePluginScanList();
        main.refreshPluginLibrary();

        EXPECT_TRUE(main.getAppPropertiesForTest().getUserSettings()->containsKey(MainComponent::kPluginScanListKey))
            << "the scan list must be stored under the documented key";
        // The library section is fed from the same list, so a scan is visible without a relaunch.
        EXPECT_EQ(main.getModuleLibrary().getPluginCount(), 1);
    }

    // --- A fresh MainComponent restores it ------------------------------------------------------
    {
        MainComponent main(std::make_unique<SilentProvider>());
        auto& service = main.getPluginScanService();
        EXPECT_EQ(service.getNumKnownPlugins(), 1) << "the saved scan list must come back on the next launch";
        EXPECT_TRUE(service.getBlacklist().contains(kCrasher)) << "...and so must the blacklist";

        const auto resolved = service.resolve(alpha);
        ASSERT_TRUE(resolved.has_value());
        EXPECT_EQ(resolved->fileOrIdentifier, kAlpha);

        // The sidebar shows it straight away, without a scan.
        EXPECT_EQ(main.getModuleLibrary().getPluginCount(), 1);
        EXPECT_GE(entryIndexForText(main.getModuleLibrary(), "Alpha"), 0);

        // ...and the process-wide backend resolves through it, which is what makes a patch that
        // names this plugin load on the next launch without a rescan.
        if (auto* backend =
                dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault())) {
            EXPECT_EQ(backend->getScanService(), &service) << "the owner must install the service on the backend";
            juce::PluginDescription out;
            EXPECT_TRUE(backend->resolveIdentity(alpha, out));
            EXPECT_EQ(out.name, "Alpha");
        }
    }

    // Destroying the owner must take the service off the backend with it.
    if (auto* backend = dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault()))
        EXPECT_EQ(backend->getScanService(), nullptr) << "a destroyed owner must not leave a dangling scan service";
}

TEST_F(PluginScanPersistenceTest, AHostedBuildResolvesButNeverScans) {
    // The plugin editor reuses MainComponent, so this guard is the thing standing between a user
    // clicking "Scan for plugins..." inside Ableton and one extra copy of Ableton per candidate:
    // the scan re-launches currentExecutableFile, which inside a VST3 is the HOST's binary.
    AudioEngine hostedEngine(AudioEngine::HostMode::Hosted);
    ASSERT_TRUE(hostedEngine.isHosted());

    synth::theme::ThemeManager themeManager;
    synth::theme::AppLookAndFeel lookAndFeel;
    MainComponent main(themeManager, lookAndFeel, hostedEngine, std::make_unique<SilentProvider>());

    main.startPluginScan();
    EXPECT_FALSE(main.getPluginScanService().isScanning()) << "a hosted build must never launch a scan";

    // ...but the resolver is still installed, or a DAW session hosting a plugin could never
    // resolve its identity to a binary.
    if (auto* backend = dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault()))
        EXPECT_EQ(backend->getScanService(), &main.getPluginScanService());
}

// ============================================================================
// 9. Eager startup scan (FRO44)
// ============================================================================

TEST_F(PluginScanPersistenceTest, HostedBuildNeverStartsTheEagerScanEither) {
    // FRO44's own guard on the same door AHostedBuildResolvesButNeverScans (above) locks: the eager
    // entry point must defer to the very same "never inside a host" rule as the manual button, not
    // reintroduce a bypass.
    AudioEngine hostedEngine(AudioEngine::HostMode::Hosted);
    synth::theme::ThemeManager themeManager;
    synth::theme::AppLookAndFeel lookAndFeel;
    MainComponent main(themeManager, lookAndFeel, hostedEngine, std::make_unique<SilentProvider>());

    // A counting seam, not just isScanning(): on a CI machine with zero real plugins installed,
    // isScanning() alone would read false whether or not the guard actually fired (a real scan with
    // nothing to find also finishes instantly), so it cannot tell "never started" from "started and
    // already done". Asserting the candidate source is never even CALLED is the one check that
    // actually pins the isHosted() guard.
    int candidateSourceCalls = 0;
    main.getPluginScanService().setCandidateSource([&](const juce::String&) {
        ++candidateSourceCalls;
        return juce::StringArray();
    });

    main.maybeStartEagerPluginScan();
    EXPECT_FALSE(main.getPluginScanService().isScanning())
        << "hosted mode must stay lazy-on-resolve-only — see docs/architecture.md's Plugin scanning section";
    EXPECT_EQ(candidateSourceCalls, 0) << "a hosted build must never even enumerate candidates";
    EXPECT_EQ(main.getModuleLibrary().getPluginCount(), 0);
}

TEST_F(PluginScanPersistenceTest, EagerScanPopulatesTheSidebarWithoutItEverBeingOpened) {
    // The founder complaint this ticket fixes, end to end: nothing here calls
    // moduleLibrary.onScanPluginsRequested or opens the PLUGINS section — only the eager entry point
    // Main.cpp calls after building the real window.
    //
    // `launcher` is declared BEFORE `main` (the pattern every other test in this file follows,
    // e.g. PersistenceViaOwner's inner scope): MainComponent's destructor joins the scan thread via
    // cancelScan(), and that thread can still be calling into a captured `&launcher` while it winds
    // down, so the launcher must outlive `main`, not the other way around.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);

    // The delegating ctor owns its own (standalone, never Hosted) AudioEngine — see MainComponent.h's
    // ctor comment — so the eager scan's isHosted() guard is a no-op here, exactly as it is for the
    // real app's own MainWindow-created MainComponent.
    MainComponent main(std::make_unique<SilentProvider>());
    ASSERT_EQ(main.getModuleLibrary().getPluginCount(), 0) << "nothing saved from a previous test run";

    // maybeStartEagerPluginScan() drives the REAL synth::hostedPluginFormatNames() (VST3 **and**
    // AudioUnit on macOS), unlike the single-format helpers most other tests in this file use — so
    // the fake source must be format-aware, or a plugin "found" for one format is scanned again for
    // every other hosted format and double-counted below.
    main.getPluginScanService().setCandidateSource(
        [](const juce::String& format) { return format == "VST3" ? juce::StringArray(kAlpha) : juce::StringArray(); });
    main.getPluginScanService().setChildLauncher(launcher.fn());

    main.maybeStartEagerPluginScan();
    ASSERT_TRUE(pumpUntil([&] { return main.getModuleLibrary().getPluginCount() > 0; }))
        << "the eager scan never reached the sidebar";
    EXPECT_GE(entryIndexForText(main.getModuleLibrary(), "Alpha"), 0);

    // pluginScanCompleted() persists the list exactly like a manual scan's old inline completion did.
    EXPECT_TRUE(main.getAppPropertiesForTest().getUserSettings()->containsKey(MainComponent::kPluginScanListKey));

    // A later call (e.g. FRO42's picker opening after startup already scanned) must not rescan.
    main.maybeStartEagerPluginScan();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(launcher.launchCountFor(kAlpha), 1);
}

TEST_F(PluginScanPersistenceTest, MaybeStartEagerPluginScanShowsAScanningBannerOnTheStatusBar) {
    // FRO105: maybeStartEagerPluginScan() — the actual production entry point Main.cpp calls, not
    // ensureScanned() directly — used to start the scan with no progress callback at all, so nothing
    // told the user a scan was even running until the one "Found N plugins" completion message.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);

    MainComponent main(std::make_unique<SilentProvider>());
    main.getPluginScanService().setCandidateSource(
        [](const juce::String& format) { return format == "VST3" ? juce::StringArray(kAlpha) : juce::StringArray(); });
    main.getPluginScanService().setChildLauncher(launcher.fn());

    // Deterministic, no polling needed: the "Scanning..." banner is posted synchronously, inline
    // inside maybeStartEagerPluginScan() itself, BEFORE it returns — only the scan's own progress and
    // completion callbacks are posted asynchronously via MessageManager::callAsync (see
    // PluginScanService::postToMessageThread), so nothing has had a chance to overwrite it yet.
    main.maybeStartEagerPluginScan();
    EXPECT_EQ(main.getStatusBar().getTransientMessageForTest(), juce::String("Scanning for plugins..."))
        << "the eager scan must announce itself immediately, not stay silent until it finishes";

    ASSERT_TRUE(pumpUntil([&] { return main.getModuleLibrary().getPluginCount() > 0; }))
        << "the eager scan never reached completion";
    EXPECT_TRUE(main.getStatusBar().getTransientMessageForTest().startsWith("Found"))
        << "completion must still show the existing 'Found N plugins' message";

    // A later call is the documented no-op and must not re-show the banner for a scan that already
    // ran — it would stomp right back over the completion message a caller might be showing.
    main.getStatusBar().showMessage("sentinel");
    main.maybeStartEagerPluginScan();
    EXPECT_EQ(main.getStatusBar().getTransientMessageForTest(), juce::String("sentinel"));
}

// ============================================================================
// 10. Surviving a quit mid-scan (FRO105)
// ============================================================================

TEST_F(PluginScanPersistenceTest, DestroyingMainComponentMidScanPersistsWhatWasFoundSoFar) {
    // Quitting while a large or slow plugin folder is still being probed must not throw away
    // everything that scan already found -- pluginScanCompleted() (the only place that used to call
    // savePluginScanList()) never fires here, because ~MainComponent() unregisters the Listener
    // before cancelling the scan. Three candidates, a generous per-candidate delay: destroy `main`
    // while Alpha is already known but the scan is still running. Cancellation is only checked
    // between candidates (see PluginScanService::runScan), so whichever candidate is in flight at
    // that moment still finishes and gets saved -- the assertions below intentionally don't pin down
    // WHICH of Beta/Gamma that was (a poll landing a few ms later than expected would otherwise make
    // this flaky), only that at least Alpha survived and the scan really was cut short somewhere.
    constexpr const char* kGamma = "/plugins/Gamma.vst3";
    FakeLauncher launcher;
    launcher.delayMs = 400;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB2FB, kBeta);
    // kGamma has no entry on purpose: it must never even be launched (see the assertion below), so
    // what it would have resolved to is irrelevant.

    // `launcher` outlives `main` (declared first) -- ~MainComponent()'s cancelScan() joins the scan
    // thread, and that thread can still be calling into `&launcher` while it winds down.
    auto main = std::make_unique<MainComponent>(std::make_unique<SilentProvider>());
    main->getPluginScanService().setCandidateSource(candidates({kAlpha, kBeta, kGamma}));
    main->getPluginScanService().setChildLauncher(launcher.fn());

    main->maybeStartEagerPluginScan();
    ASSERT_TRUE(pumpUntil([&] {
        return main->getPluginScanService().getNumKnownPlugins() > 0 && main->getPluginScanService().isScanning();
    })) << "never landed in the mid-scan window (Alpha known, scan still running)";

    main.reset(); // ~MainComponent(): cancels the scan, then must still save what it already had.

    juce::PropertiesFile::Options options;
    options.applicationName = "Agent Synth";
    options.folderName = "Agent Synth";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties properties;
    properties.setStorageParameters(options);
    auto savedList = juce::parseXML(properties.getUserSettings()->getValue(MainComponent::kPluginScanListKey));
    ASSERT_NE(savedList, nullptr) << "an interrupted scan must still persist whatever it found before the quit";

    PluginScanService restored;
    restored.loadFromXml(*savedList);
    // Not pinned to exactly 2: whichever candidate was "currently in flight" at the moment of the
    // quit still finishes and gets saved (cancellation is only checked BETWEEN candidates), so the
    // saved count is Alpha alone or Alpha+Beta depending on exactly when the poll below landed —
    // either is a correct outcome. Gamma (no xmlByFile entry) can only ever be blacklisted, never
    // added, so it can never push this above 2 regardless.
    EXPECT_GE(restored.getNumKnownPlugins(), 1) << "at least Alpha (found before the quit) must survive";
    EXPECT_LE(restored.getNumKnownPlugins(), 2);

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    EXPECT_TRUE(restored.resolve(alpha).has_value());
    EXPECT_EQ(launcher.launchCountFor(kGamma), 0)
        << "Gamma was never reached -- this really was an interrupted scan, not a slow-but-complete one";
}
