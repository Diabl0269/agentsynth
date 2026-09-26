#include "MainComponent/MainComponent.h"
#include "UserSettings.h"
#include <cstdlib>
#include <gtest/gtest.h>
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
    juce::ScopedJuceInitialiser_GUI juceInit;
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new MessageQueueDrainer());

    // FRO305: the ONE call site that reads AGENTSYNTH_SETTINGS_DIR, so a shipped binary can never
    // be redirected via it (see test-patterns.md's "on-disk path" section for the full seam).
    const juce::String settingsDirOverride(
        std::getenv("AGENTSYNTH_SETTINGS_DIR") != nullptr ? std::getenv("AGENTSYNTH_SETTINGS_DIR") : "");
    if (settingsDirOverride.isNotEmpty())
        synth::setSettingsDirOverrideForTests(settingsDirOverride);

    // FRO193: every MainComponent's MidiLearnController gets a temp ControllerProfileStore instead
    // of the developer's real folder -- set once so ~50 MainComponent*Tests.cpp files don't have
    // to know MIDI Remote exists. FRO305: nested per-shard (not one fixed name) when sharded, so
    // concurrent shards don't race each other's deleteRecursively() -- see test-patterns.md.
    const auto controllerProfilesRoot = settingsDirOverride.isNotEmpty()
                                            ? juce::File(settingsDirOverride)
                                            : juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto controllerProfilesDir = controllerProfilesRoot.getChildFile("agentsynth-tests-controller-profiles");
    controllerProfilesDir.deleteRecursively();
    MainComponent::setControllerProfileTestDirectory(controllerProfilesDir);

    return RUN_ALL_TESTS();
}
