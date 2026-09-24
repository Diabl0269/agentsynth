// Tests/MidiRemote/MidiRemoteFeedbackOutputsTests.cpp -- FRO139
// (docs/control/midi-remote.md#controller-feedback): the cache/failure-remembering shell around
// MidiRemoteFeedbackOutputs::Opener, driven entirely through the injected-opener test seam so no
// real MIDI hardware is touched.

#include "MidiRemote/MidiRemoteFeedbackOutputs.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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

TEST(MidiRemoteFeedbackOutputsTest, EmptyIdentifierIsANoOp) {
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
