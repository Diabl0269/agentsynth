#include "Auth/KeychainTokenStore.h"
#include "MainComponent/MainComponent.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "SoundPacks/SoundPackFetch.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UserSettings.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_events/juce_events.h>
#include <string>
#include <vector>

// Drain pending async messages after each test to prevent
// cross-test pollution from callAsync/timer callbacks
class MessageQueueDrainer : public ::testing::EmptyTestEventListener {
    void OnTestEnd(const ::testing::TestInfo&) override {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(10);
    }
};

// The motion test seams are process-wide: a test that turns one on and never back off makes every later test in the
// same process (and so which shard it lands in) animate where it expects an instant change. After each test this names
// a test that left one changed, puts the default back so the rest of the run is unaffected, and fails the run.
class MotionSeamLeakCheck : public ::testing::EmptyTestEventListener {
public:
    static std::vector<std::string>& leakers() {
        static std::vector<std::string> names;
        return names;
    }

private:
    void OnTestEnd(const ::testing::TestInfo& info) override {
        const bool leaked = synth::ui::FadeVisibility::animatesOffScreenForTest() ||
                            synth::ui::PopupMotion::animatesOffScreenForTest() ||
                            synth::ui::animationMode() != synth::ui::AnimationMode::followSystem;
        if (!leaked)
            return;
        leakers().push_back(std::string(info.test_suite_name()) + "." + info.name());
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::PopupMotion::setAnimateOffScreenForTest(false);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    }
};

int main(int argc, char** argv) {
    // The real ChildLauncher re-executes THIS binary with `--scan-plugin <fmt> <file> <token>`
    // (any test that lets a MainComponent's eager scan use the default launcher). Serve that as
    // Main.cpp does and exit BEFORE gtest starts: otherwise the child re-runs the whole suite (and
    // inherits AGENTSYNTH_SETTINGS_DIR / GTEST_SHARD_INDEX), racing its parent on the same
    // settings file -- flaky tests.
    {
        juce::StringArray args;
        for (int i = 0; i < argc; ++i)
            args.add(juce::String::fromUTF8(argv[i]));
        juce::String scanXml;
        if (const auto exitCode = synth::runPluginScanChildMode(args, scanXml, /*suppressCrashDialog=*/true)) {
            if (scanXml.isNotEmpty())
                std::cout << scanXml << std::endl;
            std::cout.flush();
            return *exitCode;
        }
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new MessageQueueDrainer());
    ::testing::UnitTest::GetInstance()->listeners().Append(new MotionSeamLeakCheck());

    // The ONE call site that reads AGENTSYNTH_SETTINGS_DIR, so a shipped binary can never
    // be redirected via it (see test-patterns.md's "on-disk path" section for the full seam).
    // Without it, every run still gets a private, fresh settings folder of its own: the suite
    // must never read or write the developer's real settings file (recent projects, the plugin
    // scan list, preferences), and two runs in different worktrees must never share one.
    juce::String settingsDirOverride(
        std::getenv("AGENTSYNTH_SETTINGS_DIR") != nullptr ? std::getenv("AGENTSYNTH_SETTINGS_DIR") : "");
    juce::File ownedSettingsDir;
    if (settingsDirOverride.isEmpty()) {
        ownedSettingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("agentsynth-tests-settings-" + juce::Uuid().toDashedString());
        ownedSettingsDir.createDirectory();
        settingsDirOverride = ownedSettingsDir.getFullPathName();
    }
    synth::setSettingsDirOverrideForTests(settingsDirOverride);

    // No test may read the machine's own Reduce Motion setting: CI runners and developer Macs differ, so a test that
    // expects motion without pinning it passed locally and failed on CI. Tests that want it on say so.
    synth::ui::setSystemReducedMotionForTest(false);

    // No test may reach the real sound pack host: the default fetcher answers "offline" (tests inject their own).
    synth::soundpacks::setHttpBlockedForTesting(true);

    // Every MainComponent's MidiLearnController gets a temp ControllerProfileStore instead
    // of the developer's real folder -- set once so ~50 MainComponent*Tests.cpp files don't have
    // to know MIDI Remote exists. Nested in this run's own settings folder, so concurrent runs
    // and shards don't race each other's deleteRecursively() -- see test-patterns.md.
    const auto controllerProfilesDir =
        juce::File(settingsDirOverride).getChildFile("agentsynth-tests-controller-profiles");
    controllerProfilesDir.deleteRecursively();
    MainComponent::setControllerProfileTestDirectory(controllerProfilesDir);

    // Every MainComponent builds an AccountService that reads the stored sign-in token at
    // startup. Keep that in memory so the suite never touches the developer's real Keychain item
    // (an unsigned or re-signed Tests binary would otherwise raise the login-keychain password
    // prompt on every run).
    synth::KeychainTokenStore::useInMemoryStoreForProcess(true);

    int result = RUN_ALL_TESTS();
    for (const auto& name : MotionSeamLeakCheck::leakers()) {
        std::cerr << "[  LEAKED  ] " << name
                  << " left a motion test seam changed (FadeVisibility/PopupMotion off-screen animation or the "
                     "Animations mode); restore it before the test returns"
                  << std::endl;
        result = 1;
    }
    if (ownedSettingsDir != juce::File())
        ownedSettingsDir.deleteRecursively();
    return result;
}
