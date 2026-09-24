// Tests/MidiRemote/MidiRemoteFeedbackOutputsTests.cpp -- FRO139
// (docs/control/midi-remote.md#controller-feedback): the cache/failure-remembering shell around
// MidiRemoteFeedbackOutputs::Opener, driven through the injected-opener test seam -- except the last
// test, which is skipped unless a real output device is named (see its comment).

#include "MidiRemote/MidiRemoteFeedbackOutputs.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

using namespace synth;
using namespace synth::midi;

namespace {

ControllerProfile::Input makeDevice(const juce::String& identifier, const juce::String& name = "dev") {
    ControllerProfile::Input input;
    input.identifier = identifier;
    input.name = name;
    return input;
}

} // namespace

TEST(MidiRemoteFeedbackOutputsTest, OpensOnceAndCachesForRepeatSends) {
    int openCount = 0;
    std::vector<juce::MidiMessage> received;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input&) {
        ++openCount;
        return [&](const juce::MidiMessage& m) { received.push_back(m); };
    });

    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 64));
    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 65));

    EXPECT_EQ(openCount, 1);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].getControllerValue(), 64);
    EXPECT_EQ(received[1].getControllerValue(), 65);
}

TEST(MidiRemoteFeedbackOutputsTest, DistinctIdentifiersOpenSeparately) {
    int openCount = 0;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input&) {
        ++openCount;
        return [](const juce::MidiMessage&) {};
    });

    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));
    outputs.sendFeedback(makeDevice("dev2"), juce::MidiMessage::controllerEvent(1, 10, 1));

    EXPECT_EQ(openCount, 2);
}

TEST(MidiRemoteFeedbackOutputsTest, FailedOpenIsRememberedNotRetried) {
    int openCount = 0;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input&) {
        ++openCount;
        return std::function<void(const juce::MidiMessage&)>(); // failure
    });

    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));
    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));
    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));

    EXPECT_EQ(openCount, 1); // not retried every send
}

TEST(MidiRemoteFeedbackOutputsTest, CloseAllRetriesAfterAFailure) {
    int openCount = 0;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input&) {
        ++openCount;
        return std::function<void(const juce::MidiMessage&)>();
    });

    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));
    outputs.closeAll();
    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));

    EXPECT_EQ(openCount, 2);
}

TEST(MidiRemoteFeedbackOutputsTest, CloseAllReopensACachedDevice) {
    int openCount = 0;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input&) {
        ++openCount;
        return [](const juce::MidiMessage&) {};
    });

    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));
    outputs.closeAll();
    outputs.sendFeedback(makeDevice("dev1"), juce::MidiMessage::controllerEvent(1, 10, 1));

    EXPECT_EQ(openCount, 2);
}

TEST(MidiRemoteFeedbackOutputsTest, NameOnlyDeviceIsOpenedOnce) {
    int openCount = 0;
    int sent = 0;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input& device) {
        ++openCount;
        EXPECT_EQ(device.name, "Named Only");
        return [&](const juce::MidiMessage&) { ++sent; };
    });

    outputs.sendFeedback(makeDevice({}, "Named Only"), juce::MidiMessage::controllerEvent(1, 10, 1));
    outputs.sendFeedback(makeDevice({}, "Named Only"), juce::MidiMessage::controllerEvent(1, 10, 2));

    EXPECT_EQ(openCount, 1);
    EXPECT_EQ(sent, 2);
}

TEST(MidiRemoteFeedbackOutputsTest, EmptyIdentifierAndNameIsANoOp) {
    int openCount = 0;
    MidiRemoteFeedbackOutputs outputs([&](const ControllerProfile::Input&) {
        ++openCount;
        return [](const juce::MidiMessage&) {};
    });

    outputs.sendFeedback(ControllerProfile::Input{}, juce::MidiMessage::controllerEvent(1, 10, 1));

    EXPECT_EQ(openCount, 0);
}

TEST(MidiRemoteFeedbackOutputsTest, DefaultConstructorUsesRealOpenerAndDoesNotCrashWithNoDevice) {
    MidiRemoteFeedbackOutputs outputs;
    // No real device named this will exist on a CI runner -- exercises the real
    // juce::MidiOutput::openDevice/getAvailableDevices path end to end and proves a failed real
    // open is handled the same as the fake-opener failure case above, with no crash.
    outputs.sendFeedback(makeDevice("definitely-not-a-real-device-id", "definitely not a real device name"),
                         juce::MidiMessage::controllerEvent(1, 10, 1));
}

// Manual check against a REAL MIDI output (skipped unless AGENTSYNTH_FEEDBACK_TEST_DEVICE names a
// device, e.g. a virtual CoreMIDI destination or a controller with LED rings): the real engine and
// the real juce::MidiOutput path, no fakes. Expected on the device, CC 10 on channel 1:
//   1. 32   -- the parameter set from "outside" (as a mouse or automation would)
//   2. nothing while the simulated hardware sweeps CC 10 from 0 to 127, then exactly one 127 once
//      the 250 ms cooldown has passed
//   3. 96   -- the parameter set from outside again
TEST(MidiRemoteFeedbackOutputsTest, RealDeviceWhenRequested) {
    const auto deviceName = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_FEEDBACK_TEST_DEVICE", {});
    if (deviceName.isEmpty())
        GTEST_SKIP() << "set AGENTSYNTH_FEEDBACK_TEST_DEVICE to a MIDI output name to run";

    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    node->properties.set("uuid", juce::String("real-device-node"));
    if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
        mb->setNodeUuid("real-device-node");
    auto* cutoff = findParameterByID(node->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);

    MidiRemoteFeedbackOutputs outputs;
    RemoteEngine engine;
    double nowMs = 0.0;
    engine.setClock([&] { return nowMs; });
    engine.setFeedbackSink(&outputs);

    ControllerProfile profile;
    profile.id = "profile";
    profile.name = "profile";
    profile.input.identifier = "src";
    profile.input.name = "src";
    profile.hasOutput = true;
    profile.output.name = deviceName; // no identifier: exercises the open-by-name fallback
    Control knob;
    knob.id = "k1";
    knob.name = "Knob 1";
    knob.message.channel = 1;
    knob.message.number = 10;
    profile.controls.push_back(knob);

    Assignment a;
    a.id = "a1";
    a.control.profileId = "profile";
    a.control.controlId = "k1";
    a.spec = knob.message;
    a.target.parameter.nodeUuid = "real-device-node";
    a.target.parameter.paramId = "cutoff";
    a.takeover = Takeover::jump;

    engine.setProfiles({profile});
    engine.setSources({juce::String("src")});
    engine.setAssignments({a});
    engine.reconcile(graph);

    auto tick = [&](double ms) {
        nowMs += ms;
        engine.drain();
        juce::Thread::sleep(static_cast<int>(ms));
    };

    cutoff->setValueNotifyingHost(32.0f / 127.0f);
    tick(20);

    for (int v = 0; v <= 127; v += 8) {
        engine.handleMessage("src", juce::MidiMessage::controllerEvent(1, 10, v));
        tick(16);
    }
    engine.handleMessage("src", juce::MidiMessage::controllerEvent(1, 10, 127));
    tick(16);
    tick(300);

    cutoff->setValueNotifyingHost(96.0f / 127.0f);
    tick(20);
    tick(20);
}
