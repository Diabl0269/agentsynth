// Identity resolution precedence (uid-then-name, drag payloads) and the HostedPluginBackend
// integration that resolves a placeholder into a loaded module once the scan service knows about it.

#include "PluginScanTestHelpers.h"

#include "Plugin/Hosting/HostedPluginModule.h"
#include "SnippetManager.h"

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
