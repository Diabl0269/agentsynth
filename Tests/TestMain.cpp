#include "MainComponent/MainComponent.h"
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

    // FRO193: every MainComponent this whole binary constructs (the delegating ctor's own default
    // argument, MainComponent::controllerProfileStoreForCtor()) uses this temp dir for its
    // MidiLearnController's ControllerProfileStore instead of the developer's real
    // <settings>/MidiRemote/Controllers folder -- set once, here, so none of the ~50
    // MainComponent*Tests.cpp files need to know MIDI Remote exists.
    const auto controllerProfilesDir =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-tests-controller-profiles");
    controllerProfilesDir.deleteRecursively();
    MainComponent::setControllerProfileTestDirectory(controllerProfilesDir);

    return RUN_ALL_TESTS();
}
