// Concern: what ~MainComponent() must finish before its members die. Standalone's
// audioEngine.shutdown() clears the graph inside the destructor body, while the remoteEngine member
// is destroyed only after it -- so a MIDI knob gesture still inside RemoteEngine's 250 ms idle window
// must be ended in the body, or ~RemoteEngine() ends it on a freed parameter (quitting right after
// turning a mapped knob). See RemoteEngine::endAllGestures().
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"

TEST_F(MainComponentTest, DestroyingWithAMidiKnobGestureStillOpenEndsItBeforeTheGraphIsCleared) {
    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->getAudioEngine().getDeviceManager().closeAudioDevice();

    constexpr const char* kUuid = "teardown-filter-uuid";
    {
        // Scoped: a Node::Ptr held past mc.reset() would keep the parameter alive and hide the bug.
        auto node = mc->getAudioEngine().getGraph().addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", juce::String(kUuid));
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(kUuid);
    }

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
    profile.input.identifier = "teardown-source";
    profile.controls = {control};

    synth::Assignment assignment;
    assignment.id = "a1";
    assignment.control.profileId = "p";
    assignment.control.controlId = "c";
    assignment.spec = control.message;
    assignment.target.kind = synth::Target::Kind::parameter;
    assignment.target.parameter.nodeUuid = kUuid;
    assignment.target.parameter.paramId = "cutoff";
    assignment.takeover = synth::Takeover::jump;

    auto& remote = mc->getRemoteEngineForTest();
    remote.setSources({"teardown-source"});
    remote.setProfiles({profile});
    remote.setAssignments({assignment});
    remote.reconcile(mc->getAudioEngine().getGraph());

    ASSERT_TRUE(remote.handleMessage("teardown-source", juce::MidiMessage::controllerEvent(1, 20, 64)));
    remote.drain();
    ASSERT_EQ(remote.activeGestureCount(), 1) << "the knob was just turned: its gesture is still open";

    // Without the endAllGestures() call in the destructor body this is a use-after-free in
    // ~RemoteEngine() -- an ASan report on the Linux ASan job, and a segfault in most local Release
    // runs (whether the freed block has been reused yet decides it).
    mc.reset();
    SUCCEED();
}
