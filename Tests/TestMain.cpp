#include "MainComponent/MainComponent.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "UserSettings.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_events/juce_events.h>

// Drain pending async messages after each test to prevent
// cross-test pollution from callAsync/timer callbacks
class MessageQueueDrainer : public ::testing::EmptyTestEventListener {
    void OnTestEnd(const ::testing::TestInfo&) override {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(10);
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

    // The ONE call site that reads AGENTSYNTH_SETTINGS_DIR, so a shipped binary can never
    // be redirected via it (see test-patterns.md's "on-disk path" section for the full seam).
    const juce::String settingsDirOverride(
        std::getenv("AGENTSYNTH_SETTINGS_DIR") != nullptr ? std::getenv("AGENTSYNTH_SETTINGS_DIR") : "");
    if (settingsDirOverride.isNotEmpty())
        synth::setSettingsDirOverrideForTests(settingsDirOverride);

    // Every MainComponent's MidiLearnController gets a temp ControllerProfileStore instead
    // of the developer's real folder -- set once so ~50 MainComponent*Tests.cpp files don't have
    // to know MIDI Remote exists. Nested per-shard (not one fixed name) when sharded, so
    // concurrent shards don't race each other's deleteRecursively() -- see test-patterns.md.
    const auto controllerProfilesRoot = settingsDirOverride.isNotEmpty()
                                            ? juce::File(settingsDirOverride)
                                            : juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto controllerProfilesDir = controllerProfilesRoot.getChildFile("agentsynth-tests-controller-profiles");
    controllerProfilesDir.deleteRecursively();
    MainComponent::setControllerProfileTestDirectory(controllerProfilesDir);

    return RUN_ALL_TESTS();
}
