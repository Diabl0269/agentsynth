// MainComponentMidiRemoteStartupTests.cpp -- FRO260: MainComponent::wireMidiRemoteEngine() splits
// into an early half (sink install + Hosted's fixed hostSourceKey() source, both safe before the
// AudioEngine exists) and MainComponent::openMidiRemoteDevices() -- opening the saved controller
// profiles' Standalone devices and republishing the engine's real open-input set -- which must not
// run until AudioEngine::initialise() has actually brought the engine up (its MIDI input list is
// meaningless before that). Proving "opened a real device after init" directly needs real MIDI
// hardware (Tests/Engine/MidiInputDeliveryTests.cpp already documents why that can't run
// headlessly), so the Standalone test below instead proves the ORDER through
// AudioEngine::isReceivingDeviceCallbacks() (true only once AudioEngine::initialiseDevices() has
// run), sampled by openMidiRemoteDevices() itself into
// midiRemoteDevicesOpenedAfterEngineUpForTest() -- see that seam's own doc comment in
// MainComponent.h / MainComponentSetup.cpp.
//
// Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "MainComponentTestFixture.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

using MidiRemoteStartupTest = MainComponentTest;

TEST_F(MidiRemoteStartupTest, StandaloneOpensRemoteDevicesOnlyAfterTheEngineIsUp) {
    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    // Not needed for the rest of the test, and avoids leaving a real device open past this test.
    mc->getAudioEngine().getDeviceManager().closeAudioDevice();

    EXPECT_TRUE(mc->getAudioEngine().isReceivingDeviceCallbacks())
        << "initialiseAudioEngine() must have brought the engine up during construction";
    EXPECT_TRUE(mc->midiRemoteDevicesOpenedAfterEngineUpForTest())
        << "openMidiRemoteDevices() must have run, with the engine already up, during construction "
           "-- see MainComponent::initialiseAudioEngine()'s own doc comment for why it is called "
           "from inside there rather than after initialiseCommon()'s initialiseAudioEngine() call";
}

// Hosted mode never reaches openMidiRemoteDevices() at all (initialiseAudioEngine() returns before
// calling it) -- wireMidiRemoteEngine()'s early half must still fully wire the remote engine on
// its own, primed with the fixed hostSourceKey() source. Source/Plugin/PluginEditor.cpp is the ONE
// real construction site for a Hosted MainComponent (its own comment: "no test builds an
// AgentSynthPluginEditor directly"), so this follows the established pattern
// Tests/MidiRemote/MidiLearnControllerHostedParameterTests.cpp and
// Tests/MidiRemote/RemoteEngineHostedTests.cpp already use to test this wiring without one: a raw
// Hosted AudioEngine + the same collaborators wireMidiRemoteEngine() wires, reproducing its
// refreshSources() call (the seam openMidiRemoteDevices() also uses, for Standalone) directly.
TEST(MidiRemoteStartupHostedTest, HostedWiresTheHostSourceKeyWithoutOpeningAnyDevice) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    GraphEditor graphEditor(engine);
    synth::MidiRemoteProjectDoc doc;
    AppUndoManager undo;
    StatusBarComponent statusBar;
    synth::midi::RemoteEngine remoteEngine;

    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midiremote-startup-hosted-tests-" + juce::Uuid().toString());
    root.deleteRecursively();
    synth::midi::MidiLearnController controller(engine, graphEditor, remoteEngine, doc, undo, statusBar,
                                                synth::ControllerProfileStore(root));

    synth::Control control;
    control.id = "c";
    control.name = "c";
    control.kind = synth::ControlKind::knob;
    control.message.type = synth::MessageType::cc;
    control.message.channel = 1;
    control.message.number = 20;

    synth::ControllerProfile profile;
    profile.id = "p";
    profile.name = "p";
    profile.input.identifier = synth::midi::hostSourceKey();
    profile.controls = {control};

    synth::Assignment assignment;
    assignment.id = "a1";
    assignment.control.profileId = "p";
    assignment.control.controlId = "c";
    assignment.spec = control.message;
    assignment.target.kind = synth::Target::Kind::action;
    assignment.target.action.actionId = "some.action";

    remoteEngine.setProfiles({profile});
    remoteEngine.setAssignments({assignment});

    // Mirrors wireMidiRemoteEngine()'s early-half priming: Hosted's only source is the fixed
    // hostSourceKey(), available before -- and unaffected by -- any AudioEngine bring-up.
    controller.refreshSources();
    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty()) << "Hosted never opens hardware MIDI";

    EXPECT_TRUE(remoteEngine.handleMessage(synth::midi::hostSourceKey(), juce::MidiMessage::controllerEvent(1, 20, 64)))
        << "a message on hostSourceKey() must resolve to a mapped slot -- proving refreshSources() "
           "(the same seam openMidiRemoteDevices() calls for Standalone) actually registered it as "
           "a live RemoteEngine source in Hosted mode too";

    root.deleteRecursively();
}
