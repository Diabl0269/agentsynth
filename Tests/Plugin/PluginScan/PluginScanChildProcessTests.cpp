// Child-process scanning mechanics: `--scan-plugin` argv parsing/exit codes, the token-stamped
// output sentinels that isolate a real description from anything the plugin itself prints, and the
// format list the launcher and the host agree on.

#include "PluginScanTestHelpers.h"

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
