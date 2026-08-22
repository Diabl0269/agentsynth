// PluginScanTests.cpp
//
// synth::PluginScanService — the out-of-process plugin scan, the scan list, and the load UX
// that hangs off it.
//
// There is no third-party plugin binary in this repo and CI machines have none installed, so every
// test here drives the scan through its injected seams: a fake ChildLauncher that returns canned
// description XML for a "good" plugin and `false` for a "crashing" one (which is exactly what a
// crash, a hang, or a non-zero exit looks like from the parent's side), and a fake CandidateSource
// so the candidate list is not whatever happens to be installed on the machine running the suite.
// The real launcher and the real candidate source are exercised by actually running the app; what is
// pinned here is everything above them.
//
// Groups:
//   1. Scanning — descriptions land in the list, and the list survives a save/load round trip.
//   2. Crash isolation — a failing candidate is blacklisted, the scan continues, and a rescan does
//      not step on the same mine until the blacklist is cleared.
//   3. Timeouts — indistinguishable from a crash, deliberately.
//   4. Resolution — the documented uid-then-name precedence.
//   5. Backend integration — a HostedPluginModule placeholder becomes real once the service learns
//      about the plugin, with no re-plumbing (the placeholder test, continued).
//   6. Child mode — argv parsing and exit-code semantics of `--scan-plugin`.
//   7. Library UX — the Plugins section: rows, empty state, the scan row, and the drag payload.
//   8. Persistence via the owner — MainComponent stores the list under "pluginScanList".

#include "../Source/AI/AIProvider.h"
#include "../Source/MainComponent.h"
#include "../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../Source/Plugin/Hosting/PluginScanService.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "StubPluginInstance.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::PluginIdentity;
using synth::PluginScanService;

namespace {

constexpr const char* kAlpha = "/plugins/Alpha.vst3";
constexpr const char* kBeta = "/plugins/Beta.vst3";
constexpr const char* kCrasher = "/plugins/Crasher.vst3";

/** Pumps the message loop until `predicate` holds — the bounded-poll idiom from HostedPluginTests.
 *  Needed because scanAsync posts its progress and completion callbacks rather than calling them. */
template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

/** The document a healthy child process prints: KnownPluginList's own XML shape. */
juce::String descriptionXml(const juce::String& name, int uid, const juce::String& fileOrIdentifier,
                            const juce::String& format = "VST3") {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = format;
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = fileOrIdentifier;
    description.manufacturerName = "Test Labs";
    description.version = "1.0.0";

    juce::KnownPluginList list;
    list.addType(description);
    auto xml = list.createXml();
    return xml != nullptr ? xml->toString() : juce::String();
}

/** A ChildLauncher over a canned map, counting the launches so "was this candidate probed at all?"
 *  is directly observable — which is the whole assertion behind blacklisting. */
struct FakeLauncher {
    std::map<juce::String, juce::String> xmlByFile; // absent = "this one crashes"
    std::vector<juce::String> launched;
    int delayMs = 0;

    PluginScanService::ChildLauncher fn() {
        return [this](const juce::String&, const juce::String& fileOrIdentifier, int, juce::String& xmlOut) {
            launched.push_back(fileOrIdentifier);
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            const auto it = xmlByFile.find(fileOrIdentifier);
            if (it == xmlByFile.end())
                return false;
            xmlOut = it->second;
            return true;
        };
    }

    int launchCountFor(const juce::String& fileOrIdentifier) const {
        return (int)std::count(launched.begin(), launched.end(), fileOrIdentifier);
    }
};

PluginScanService::CandidateSource candidates(juce::StringArray files) {
    return [files](const juce::String&) { return files; };
}

/** Runs one scan to completion and hands back its Result. */
PluginScanService::Result scanToCompletion(PluginScanService& service,
                                           const juce::StringArray& formats = juce::StringArray("VST3")) {
    PluginScanService::Result result;
    bool done = false;
    service.scanAsync(formats, {}, [&](const PluginScanService::Result& r) {
        result = r;
        done = true;
    });
    EXPECT_TRUE(pumpUntil([&] { return done; })) << "the scan never completed";
    return result;
}

/** The real backend's resolveIdentity (so the scan service is genuinely in the loop) with the stub's
 *  instance creation (so no third-party binary is ever loaded). */
class ScanningStubBackend : public synth::DefaultHostedPluginBackend {
public:
    using synth::DefaultHostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription& description, double, int,
                             InstanceCallback callback) override {
        if (callback == nullptr)
            return;
        lastDescription = description;
        auto sharedCallback = std::make_shared<InstanceCallback>(std::move(callback));
        juce::MessageManager::callAsync([sharedCallback] {
            (*sharedCallback)(std::make_unique<synth::test::StubPluginInstance>(2, 2), juce::String());
        });
    }

    juce::PluginDescription lastDescription;
};

} // namespace

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

// ============================================================================
// 4. Resolution precedence
// ============================================================================

TEST(PluginScanTest, ResolvePrecedence) {
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());
    scanToCompletion(service);

    // 1. uid wins outright — even when the name has drifted (the user renamed the file, or the
    //    vendor renamed the product between versions).
    PluginIdentity renamed;
    renamed.format = "VST3";
    renamed.name = "Some Other Name Entirely";
    renamed.uid = 0xA1FA;
    auto resolved = service.resolve(renamed);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->name, "Alpha") << "the uid is the strongest key and must beat the name";

    // 2. name + format, for an identity with no uid at all.
    PluginIdentity byName;
    byName.format = "VST3";
    byName.name = "Beta";
    byName.uid = 0;
    resolved = service.resolve(byName);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->uniqueId, 0xB37A);

    // 2b. ...and as the fallback when the uid matches nothing.
    PluginIdentity unknownUid;
    unknownUid.format = "VST3";
    unknownUid.name = "Beta";
    unknownUid.uid = 0xDEAD;
    resolved = service.resolve(unknownUid);
    ASSERT_TRUE(resolved.has_value()) << "an unknown uid must fall back to the name, not give up";
    EXPECT_EQ(resolved->name, "Beta");

    // 3. Format is part of the identity: the same plugin as AU is a different entry.
    PluginIdentity wrongFormat;
    wrongFormat.format = "AudioUnit";
    wrongFormat.name = "Alpha";
    wrongFormat.uid = 0xA1FA;
    EXPECT_FALSE(service.resolve(wrongFormat).has_value());

    // 3b. ...and an unknown plugin resolves to nothing, which is what leaves a placeholder.
    PluginIdentity absent;
    absent.format = "VST3";
    absent.name = "Never Installed";
    absent.uid = 99;
    EXPECT_FALSE(service.resolve(absent).has_value());

    EXPECT_FALSE(service.resolve(PluginIdentity{}).has_value()) << "an empty identity is not a wildcard";
}

TEST(PluginScanTest, AmbiguousUidFallsBackToTheName) {
    // VST3 shells and some vendors' families genuinely collide on uid. Picking an arbitrary one of
    // two plugins the user can tell apart by name is worse than using the name.
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Twin One", 0x7777, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Twin Two", 0x7777, kBeta);

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());
    scanToCompletion(service);
    ASSERT_EQ(service.getNumKnownPlugins(), 2);

    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Twin Two";
    identity.uid = 0x7777;
    const auto resolved = service.resolve(identity);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->name, "Twin Two") << "an ambiguous uid must defer to the name rather than guess";

    // With nothing but the ambiguous uid there is no honest answer.
    PluginIdentity uidOnly;
    uidOnly.format = "VST3";
    uidOnly.uid = 0x7777;
    EXPECT_FALSE(service.resolve(uidOnly).has_value());
}

TEST(PluginScanTest, DragPayloadRoundTripsTheIdentityAndCarriesNoPath) {
    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Sub | Bass"; // the separator, on purpose
    identity.uid = 0x1234;

    const auto payload = identity.toDragPayload();
    EXPECT_TRUE(PluginIdentity::isDragPayload(payload));
    EXPECT_FALSE(PluginIdentity::isDragPayload("Oscillator"));
    EXPECT_FALSE(PluginIdentity::isDragPayload(synth::SnippetManager::payloadForName("My Group")));
    EXPECT_FALSE(payload.contains(".vst3")) << "a drag payload must not carry a path either: " << payload;
    EXPECT_EQ(PluginIdentity::fromDragPayload(payload), identity);
    EXPECT_FALSE(PluginIdentity::fromDragPayload("Oscillator").isValid());
}

// ============================================================================
// 5. Backend integration
// ============================================================================

TEST(PluginScanTest, BackendResolvesThroughService) {
    // The placeholder test, continued: the same module goes from "not installed" to loaded with
    // no re-plumbing, purely because the scan service learned about the plugin.
    ScanningStubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    PluginScanService service;
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());

    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Alpha";
    identity.uid = 0xA1FA;

    // --- Before the scan: an unresolved identity, kept, with a message ---------------------------
    HostedPluginModule module;
    module.prepareToPlay(48000.0, 64);
    module.loadPlugin(identity);
    ASSERT_TRUE(pumpUntil([&] { return !module.isLoading(); }));
    EXPECT_FALSE(module.hasInstance()) << "no scan has happened, so nothing can resolve";
    EXPECT_EQ(module.getIdentity(), identity);
    EXPECT_TRUE(module.getStatusMessage().contains("Alpha")) << module.getStatusMessage();

    // --- The owner installs the service and scans -----------------------------------------------
    backend.setScanService(&service);
    scanToCompletion(service);

    // --- The very same restore path now resolves ------------------------------------------------
    // Driven through setExtraState, which is how a loaded patch actually reaches this code: it
    // reaches for HostedPluginBackend::getDefault() rather than being handed a backend.
    module.setExtraState(identity.toVar());
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); })) << "the scan list never resolved the identity";
    EXPECT_TRUE(module.getStatusMessage().isEmpty());
    EXPECT_EQ(backend.lastDescription.fileOrIdentifier, kAlpha)
        << "the description handed to the format must be the scanned one, path and all";

    // The backend must let go cleanly: a service that outlives its installation is a dangling read.
    backend.setScanService(nullptr);
    juce::PluginDescription unused;
    EXPECT_FALSE(backend.resolveIdentity(identity, unused));
}

TEST(PluginScanTest, BackendWithNoServiceKeepsTheTL72Behaviour) {
    synth::DefaultHostedPluginBackend backend;
    EXPECT_EQ(backend.getScanService(), nullptr);

    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Anything";
    identity.uid = 7;

    juce::PluginDescription out;
    EXPECT_FALSE(backend.resolveIdentity(identity, out)) << "an empty list resolves nothing";

    // ...and the explicit list still works as the non-scanning fallback.
    juce::PluginDescription description;
    description.name = "Anything";
    description.pluginFormatName = "VST3";
    description.uniqueId = 7;
    backend.setKnownPlugins({description});
    EXPECT_TRUE(backend.resolveIdentity(identity, out));
    EXPECT_EQ(out.name, "Anything");
}

// ============================================================================
// 6. Child mode
// ============================================================================

TEST(PluginScanTest, ChildModeArgvParsing) {
    juce::String xml;

    // No flag at all: this is an ordinary app launch, and the caller must go on to start the app.
    EXPECT_FALSE(synth::runPluginScanChildMode(juce::StringArray({"/bin/AgentSynth"}), xml).has_value());
    EXPECT_FALSE(
        synth::runPluginScanChildMode(juce::StringArray({"/bin/AgentSynth", "--some-other-flag"}), xml).has_value());
    EXPECT_TRUE(xml.isEmpty());

    // The flag with missing operands is an error, not a silent app launch — otherwise a typo in the
    // parent's argv would open a window on the user's screen once per scanned plugin.
    auto exitCode = synth::runPluginScanChildMode(juce::StringArray({"/bin/AgentSynth", "--scan-plugin"}), xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0);

    exitCode = synth::runPluginScanChildMode(juce::StringArray({"/bin/AgentSynth", "--scan-plugin", "VST3"}), xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0);

    exitCode =
        synth::runPluginScanChildMode(juce::StringArray({"/bin/AgentSynth", "--scan-plugin", "", "", "abc123"}), xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0);

    // A format this build cannot host: refused before any binary is touched.
    exitCode = synth::runPluginScanChildMode(
        juce::StringArray({"/bin/AgentSynth", "--scan-plugin", "CLAP", "/plugins/Thing.clap", "abc123"}), xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0);
    EXPECT_TRUE(xml.isEmpty()) << "nothing may be printed on a failure — the parent parses stdout";

    // A hostable format pointed at a file that is not a plugin: exit non-zero, print nothing, and the
    // parent blacklists it. (Real scanning of a real plugin is not exercisable headlessly.)
    ASSERT_FALSE(synth::hostedPluginFormatNames().isEmpty()) << "this build hosts no plugin formats at all";
    juce::StringArray realFormatArgs;
    realFormatArgs.add("/bin/AgentSynth");
    realFormatArgs.add("--scan-plugin");
    realFormatArgs.add(synth::hostedPluginFormatNames()[0]);
    realFormatArgs.add("/nonexistent/definitely/not/a/plugin.vst3");
    realFormatArgs.add("abc123");
    exitCode = synth::runPluginScanChildMode(realFormatArgs, xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0);
    EXPECT_TRUE(xml.isEmpty());
}

TEST(PluginScanTest, ChildModeFlagMatchesWhatTheLauncherSends) {
    // The two halves of the protocol live in different functions; if they ever disagree the scan
    // silently launches full app instances instead of scanners.
    EXPECT_STREQ(PluginScanService::kScanArgvFlag, "--scan-plugin");
    juce::String xml;
    EXPECT_TRUE(
        synth::runPluginScanChildMode(
            juce::StringArray({"/bin/AgentSynth", PluginScanService::kScanArgvFlag, "VST3", "/x.vst3", "abc123"}), xml)
            .has_value())
        << "the flag the launcher sends must be the flag child mode recognises";
}

TEST(PluginScanTest, ChildOutputIsRecoveredFromAroundWhateverThePluginPrinted) {
    // Verbatim shape of a real `--scan-plugin` run against an installed VST3, wrapped in the kind of
    // noise a plugin cheerfully writes to stdout while being probed. Without the sentinels that
    // noise ends up inside the document and the whole scan reports the plugin as broken.
    const juce::String token = "a1b2c3d4";
    const juce::String realDocument = descriptionXml("Gravisynth", 0xbfb4a82b, "/plugins/Gravisynth.vst3");
    const juce::String childStdout = "[Gravisynth] initialising licence manager...\n" +
                                     PluginScanService::childXmlBeginMarker(token) + "\n" + realDocument + "\n" +
                                     PluginScanService::childXmlEndMarker(token) + "\nSome trailing chatter\n";

    const auto extracted = PluginScanService::extractChildXml(childStdout, token);
    ASSERT_TRUE(extracted.isNotEmpty());
    EXPECT_FALSE(extracted.contains("licence manager")) << extracted;
    EXPECT_FALSE(extracted.contains("trailing chatter")) << extracted;

    // ...and what comes out is a document the scan can actually fold in.
    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher([&extracted](const juce::String&, const juce::String&, int, juce::String& xmlOut) {
        xmlOut = extracted;
        return true;
    });
    EXPECT_EQ(scanToCompletion(service).added, 1);
    EXPECT_EQ(service.getKnownPlugins().front().name, "Gravisynth");

    // Nothing usable when either sentinel is missing — a child killed mid-print must read as failure.
    EXPECT_TRUE(PluginScanService::extractChildXml("just noise", token).isEmpty());
    EXPECT_TRUE(PluginScanService::extractChildXml(PluginScanService::childXmlBeginMarker(token) + realDocument, token)
                    .isEmpty())
        << "a truncated document must not be half-parsed";
}

TEST(PluginScanTest, AForgedChildBlockCannotOutrankTheRealOne) {
    // The scanned plugin runs INSIDE the child and can print anything it likes to that child's
    // stdout, including a complete description block naming a binary of its choosing. Two things
    // stop it writing the parent's plugin list: the sentinels carry a token the parent generated per
    // launch, and only the LAST block bearing that token is read.
    const juce::String token = PluginScanService::makeScanToken();
    EXPECT_TRUE(PluginScanService::isValidScanToken(token));
    EXPECT_FALSE(PluginScanService::isValidScanToken({}));
    EXPECT_FALSE(PluginScanService::isValidScanToken("not-hex-at-all"));

    const juce::String forged = descriptionXml("Forged", 0xF0F0, "/tmp/attacker/Payload.vst3");
    const juce::String real = descriptionXml("Gravisynth", 0xbfb4a82b, "/plugins/Gravisynth.vst3");

    // 1. A forged block stamped with a token the parent never issued is invisible to it.
    const juce::String wrongToken = "deadbeef";
    ASSERT_NE(wrongToken, token);
    const juce::String withWrongToken = PluginScanService::childXmlBeginMarker(wrongToken) + "\n" + forged + "\n" +
                                        PluginScanService::childXmlEndMarker(wrongToken) + "\n";
    EXPECT_TRUE(PluginScanService::extractChildXml(withWrongToken, token).isEmpty())
        << "a block stamped with someone else's token must not be read";

    // 2. Even having guessed the token, a block printed while the plugin loads loses to the real
    //    document the child prints on its way out.
    const juce::String spoofedStdout = withWrongToken + PluginScanService::childXmlBeginMarker(token) + "\n" + forged +
                                       "\n" + PluginScanService::childXmlEndMarker(token) + "\n[loading]\n" +
                                       PluginScanService::childXmlBeginMarker(token) + "\n" + real + "\n" +
                                       PluginScanService::childXmlEndMarker(token) + "\n";
    const auto extracted = PluginScanService::extractChildXml(spoofedStdout, token);
    ASSERT_TRUE(extracted.isNotEmpty());
    EXPECT_FALSE(extracted.contains("Payload.vst3")) << extracted;
    EXPECT_TRUE(extracted.contains("Gravisynth")) << extracted;

    // ...and what the scan folds in is the real plugin, not the attacker's.
    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher([&extracted](const juce::String&, const juce::String&, int, juce::String& xmlOut) {
        xmlOut = extracted;
        return true;
    });
    ASSERT_EQ(scanToCompletion(service).added, 1);
    EXPECT_EQ(service.getKnownPlugins().front().name, "Gravisynth");
}

TEST(PluginScanTest, ChildModeStampsTheParentsToken) {
    // The two halves of the protocol again: the child must wrap its output in the token the parent
    // sent, or the parent throws away a perfectly good scan.
    const juce::String token = PluginScanService::makeScanToken();

    juce::String xml;
    juce::StringArray args;
    args.add("/bin/AgentSynth");
    args.add(PluginScanService::kScanArgvFlag);
    args.add("VST3");
    args.add("/x.vst3");
    args.add(token);
    EXPECT_TRUE(synth::runPluginScanChildMode(args, xml).has_value());

    // A missing or malformed token is a bad-arguments exit, not a silent unstamped print.
    args.remove(args.size() - 1);
    auto exitCode = synth::runPluginScanChildMode(args, xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0) << "no token means nothing the parent could accept";

    args.add("not-hex");
    exitCode = synth::runPluginScanChildMode(args, xml);
    ASSERT_TRUE(exitCode.has_value());
    EXPECT_NE(*exitCode, 0);
    EXPECT_TRUE(xml.isEmpty());
}

TEST(PluginScanTest, HostedFormatNamesMatchTheHostedFormats) {
    juce::AudioPluginFormatManager manager;
    synth::addHostedPluginFormats(manager);

    const auto names = synth::hostedPluginFormatNames();
    ASSERT_EQ(names.size(), manager.getNumFormats())
        << "scanning and hosting must cover exactly the same set of formats";
    for (int i = 0; i < manager.getNumFormats(); ++i)
        EXPECT_TRUE(names.contains(manager.getFormat(i)->getName())) << manager.getFormat(i)->getName();

    EXPECT_TRUE(names.contains("VST3"));
#if JUCE_MAC
    EXPECT_TRUE(names.contains("AudioUnit"));
#endif
}

// ============================================================================
// 7. Library UX
// ============================================================================

namespace {

int entryIndexForText(const ModuleLibraryComponent& library, const juce::String& text) {
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntryText(i) == text)
            return i;
    return -1;
}

int countRowsOfKind(const ModuleLibraryComponent& library, ModuleLibraryComponent::RowKind kind) {
    int count = 0;
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntry(i).kind == kind)
            ++count;
    return count;
}

} // namespace

TEST(PluginScanTest, LibraryPluginsSectionEmptyStateIsTheScanRow) {
    ModuleLibraryComponent library;

    const int header = entryIndexForText(library, ModuleLibraryComponent::kPluginsHeader);
    ASSERT_GE(header, 0) << "the Plugins section must exist even before any scan";
    EXPECT_EQ(library.getEntry(header).kind, ModuleLibraryComponent::RowKind::Header);

    EXPECT_EQ(library.getPluginCount(), 0);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Plugin), 0);
    // Exactly one Action row, and it is what an empty section shows — the hint IS the button.
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Action), 1);
    const int scanRow = entryIndexForText(library, ModuleLibraryComponent::kScanPluginsRowText);
    ASSERT_GE(scanRow, 0);
    EXPECT_EQ(library.getEntry(scanRow).section, ModuleLibraryComponent::kPluginsHeader);

    // Plugins are not module types: nothing here may leak into the factory-name list.
    EXPECT_FALSE(library.getDraggableModuleNames().contains(ModuleLibraryComponent::kScanPluginsRowText));
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Hosted Plugin"));
}

TEST(PluginScanTest, LibraryPluginsSectionShowsScannedRows) {
    ModuleLibraryComponent library;

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    PluginIdentity beta;
    beta.format = "AudioUnit";
    beta.name = "Beta";
    beta.uid = 0xB37A;
    library.setPlugins({alpha, beta});

    EXPECT_EQ(library.getPluginCount(), 2);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Plugin), 2);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Action), 1)
        << "the scan row stays, so the list can be refreshed";

    const int alphaRow = entryIndexForText(library, "Alpha");
    ASSERT_GE(alphaRow, 0);
    EXPECT_EQ(library.getEntry(alphaRow).section, ModuleLibraryComponent::kPluginsHeader);
    EXPECT_EQ(library.getEntry(alphaRow).detail, "VST3") << "the format tag distinguishes a VST3 from its AU twin";
    EXPECT_EQ(library.getPluginIdentity(alphaRow), alpha);
    EXPECT_TRUE(library.isEntryEnabled(alphaRow)) << "plugin rows are draggable";

    // Still not module types.
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Alpha"));
    EXPECT_EQ(library.getPluginIdentity(entryIndexForText(library, "Oscillator")), PluginIdentity{});

    // ...and the section empties again when a rescan finds nothing.
    library.setPlugins({});
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Plugin), 0);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Action), 1);
}

TEST(PluginScanTest, LibraryPluginsSectionSubGroupsRowsByFormat) {
    ModuleLibraryComponent library;

    // Handed in reverse-format order (and not name-sorted within a format) to prove the section
    // does its own alphabetical-by-format grouping rather than trusting caller order.
    PluginIdentity vst3Zeta;
    vst3Zeta.format = "VST3";
    vst3Zeta.name = "Zeta";
    vst3Zeta.uid = 1;
    PluginIdentity vst3Alpha;
    vst3Alpha.format = "VST3";
    vst3Alpha.name = "Alpha";
    vst3Alpha.uid = 2;
    PluginIdentity auBeta;
    auBeta.format = "AudioUnit";
    auBeta.name = "Beta";
    auBeta.uid = 3;
    library.setPlugins({vst3Zeta, vst3Alpha, auBeta});

    // Two formats -> exactly two non-clickable sub-label rows, sorted alphabetically by format
    // ("AudioUnit" before "VST3"), each carrying only its format name and living in the Plugins
    // section.
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::SubHeader), 2);

    juce::StringArray subHeaderTextsInOrder;
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntry(i).kind == ModuleLibraryComponent::RowKind::SubHeader)
            subHeaderTextsInOrder.add(library.getEntry(i).text);
    ASSERT_EQ(subHeaderTextsInOrder.size(), 2);
    EXPECT_EQ(subHeaderTextsInOrder[0], "AudioUnit");
    EXPECT_EQ(subHeaderTextsInOrder[1], "VST3");

    const int auSubHeader = entryIndexForText(library, "AudioUnit");
    const int vst3SubHeader = entryIndexForText(library, "VST3");
    ASSERT_GE(auSubHeader, 0);
    ASSERT_GE(vst3SubHeader, 0);
    EXPECT_EQ(library.getEntry(auSubHeader).section, ModuleLibraryComponent::kPluginsHeader);
    EXPECT_EQ(library.getEntry(vst3SubHeader).section, ModuleLibraryComponent::kPluginsHeader);

    // The AudioUnit group (one row) comes entirely before the VST3 group (two rows, name-sorted).
    const int betaRow = entryIndexForText(library, "Beta");
    const int alphaRow = entryIndexForText(library, "Alpha");
    const int zetaRow = entryIndexForText(library, "Zeta");
    ASSERT_GE(betaRow, 0);
    ASSERT_GE(alphaRow, 0);
    ASSERT_GE(zetaRow, 0);
    EXPECT_LT(auSubHeader, betaRow);
    EXPECT_LT(betaRow, vst3SubHeader);
    EXPECT_LT(vst3SubHeader, alphaRow);
    EXPECT_LT(alphaRow, zetaRow);

    // A sub-label is not draggable, not the click-activated scan row, and not a header — it must
    // not participate in section-collapse row accounting or keyboard/hover interaction.
    EXPECT_FALSE(library.isEntryEnabled(auSubHeader));
    EXPECT_EQ(library.getPluginIdentity(auSubHeader), PluginIdentity{});
}

TEST(PluginScanTest, SubHeaderTogglesIndependentlyOfHeader) {
    ModuleLibraryComponent library;
    library.setSize(200, 1200);

    PluginIdentity vst3Alpha;
    vst3Alpha.format = "VST3";
    vst3Alpha.name = "Alpha";
    vst3Alpha.uid = 1;
    PluginIdentity auBeta;
    auBeta.format = "AudioUnit";
    auBeta.name = "Beta";
    auBeta.uid = 2;
    library.setPlugins({vst3Alpha, auBeta});

    const int vst3SubHeader = entryIndexForText(library, "VST3");
    ASSERT_GE(vst3SubHeader, 0);
    const int y = library.getRowCentreY(vst3SubHeader);
    ASSERT_GT(y, 0);

    // Click the VST3 sub-header row — mirrors real usage, going through mouseDown rather than
    // reaching into the collapse-state key directly.
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(30.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &library, &library, juce::Time::getCurrentTime(), juce::Point<float>(30.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    library.mouseDown(evt);

    // Only the VST3 format's plugin rows disappear from the visible layout...
    EXPECT_EQ(library.getRowCentreY(entryIndexForText(library, "Alpha")), -1);
    // ...while the AudioUnit group and the Plugins header itself stay expanded.
    EXPECT_GT(library.getRowCentreY(entryIndexForText(library, "Beta")), 0);
    EXPECT_GT(library.getRowCentreY(entryIndexForText(library, "AudioUnit")), 0);
    EXPECT_FALSE(library.isSectionCollapsed(ModuleLibraryComponent::kPluginsHeader));

    // The sub-header row itself never disappears — only its own header can hide it.
    EXPECT_GT(library.getRowCentreY(vst3SubHeader), 0);

    // No focus-grabbing side effect (regression guard for #232).
    EXPECT_FALSE(library.getWantsKeyboardFocus());
}

TEST(PluginScanTest, SubHeaderCollapseSurvivesCollapseAll) {
    ModuleLibraryComponent library;
    library.setSize(200, 1200);

    PluginIdentity vst3Alpha;
    vst3Alpha.format = "VST3";
    vst3Alpha.name = "Alpha";
    vst3Alpha.uid = 1;
    PluginIdentity auBeta;
    auBeta.format = "AudioUnit";
    auBeta.name = "Beta";
    auBeta.uid = 2;
    library.setPlugins({vst3Alpha, auBeta});

    const int vst3SubHeader = entryIndexForText(library, "VST3");
    ASSERT_GE(vst3SubHeader, 0);
    const int y = library.getRowCentreY(vst3SubHeader);
    ASSERT_GT(y, 0);

    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(30.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &library, &library, juce::Time::getCurrentTime(), juce::Point<float>(30.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    library.mouseDown(evt);
    EXPECT_EQ(library.getRowCentreY(entryIndexForText(library, "Alpha")), -1);

    library.toggleAllSections(); // collapse everything
    library.toggleAllSections(); // ...then expand everything

    // The VST3 sub-header's own fold — set by the user before collapse-all ran — must come back
    // exactly as left, not reset by setAllSectionsCollapsed()'s header-only sweep.
    EXPECT_EQ(library.getRowCentreY(entryIndexForText(library, "Alpha")), -1)
        << "the user's own sub-header fold must survive a collapse-all/expand-all round trip";
    EXPECT_GT(library.getRowCentreY(entryIndexForText(library, "Beta")), 0);
}

TEST(PluginScanTest, LibrarySnippetsEmptyHintIsUnaffectedByThePluginsSection) {
    // The Plugins section uses its own row kind precisely so it does not perturb the Snippets
    // section's single EmptyHint row (or any of the counts built on it).
    ModuleLibraryComponent library;
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::EmptyHint), 1);
}

TEST(PluginScanTest, LibraryScanRowFiresTheScanAndCompletionRefreshesTheRows) {
    ModuleLibraryComponent library;

    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());

    // The owner's wiring, in miniature: the row asks, the service scans, completion refreshes.
    std::vector<juce::String> progressSeen;
    bool done = false;
    library.onScanPluginsRequested = [&] {
        service.scanAsync(
            juce::StringArray("VST3"), [&](const juce::String& what, int, int) { progressSeen.push_back(what); },
            [&](const PluginScanService::Result&) {
                library.setPlugins(service.getKnownPluginIdentities());
                done = true;
            });
    };

    const int scanRow = entryIndexForText(library, ModuleLibraryComponent::kScanPluginsRowText);
    ASSERT_GE(scanRow, 0);
    ASSERT_EQ(library.getPluginCount(), 0);

    library.activateRow(scanRow);
    ASSERT_TRUE(pumpUntil([&] { return done; }));

    EXPECT_EQ(library.getPluginCount(), 2) << "the completion refresh must reach the rows";
    EXPECT_GE(entryIndexForText(library, "Alpha"), 0);
    // Progress arrives on the message thread, once per candidate.
    EXPECT_EQ(progressSeen.size(), 2u);
}

TEST(PluginScanTest, ActivatingAPluginRowAsksTheOwnerToAddIt) {
    ModuleLibraryComponent library;

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    library.setPlugins({alpha});

    std::vector<PluginIdentity> activated;
    library.onPluginActivated = [&](const PluginIdentity& identity) { activated.push_back(identity); };

    library.activateRow(entryIndexForText(library, "Alpha"));
    ASSERT_EQ(activated.size(), 1u);
    EXPECT_EQ(activated[0], alpha);

    // Non-plugin rows never fire it.
    library.activateRow(entryIndexForText(library, "Oscillator"));
    library.activateRow(entryIndexForText(library, ModuleLibraryComponent::kPluginsHeader));
    library.activateRow(-1);
    library.activateRow(9999);
    EXPECT_EQ(activated.size(), 1u);
}

TEST(PluginScanTest, DroppingAPluginPayloadAddsAHostedPluginNode) {
    ScanningStubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    PluginScanService service;
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());
    scanToCompletion(service);
    backend.setScanService(&service);

    AudioEngine engine;
    GraphEditor editor(engine);

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    editor.addHostedPluginAtCanvasPosition(alpha, {100, 100});

    HostedPluginModule* added = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* candidate = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
            added = candidate;
    ASSERT_NE(added, nullptr) << "the drop must create a Hosted Plugin node";

    // The identity is set synchronously, BEFORE the node joins the graph — which is what puts it
    // inside the undo snapshot, so a redo brings back the same plugin rather than a bare module.
    EXPECT_EQ(added->getIdentity(), alpha);
    ASSERT_TRUE(pumpUntil([&] { return added->hasInstance(); })) << "the drop never resolved through the scan list";

    // An invalid identity is refused rather than producing a bare module nobody asked for.
    const int nodesBefore = engine.getGraph().getNumNodes();
    editor.addHostedPluginAtCanvasPosition(PluginIdentity{}, {200, 200});
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore);

    backend.setScanService(nullptr);
}

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

private:
    juce::String model = "silent-model";
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
