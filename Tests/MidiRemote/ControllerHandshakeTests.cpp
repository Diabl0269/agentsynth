// FRO339 (docs/control/midi-remote-device-handshake.md#device-handshake): ControllerHandshakeCoordinator --
// sends a profile's declared open bytes once its device is open, sends close once it stops
// qualifying (removed, handshake cleared, or device closed), and shutdownAll() closes everything
// still open. A FakeFeedbackSink stands in for the real juce::MidiOutput seam
// (MidiRemoteFeedbackOutputs, already covered by its own MidiRemoteFeedbackOutputsTests.cpp for
// "missing device" / "failed open" -- this coordinator only ever calls RemoteFeedbackSink, so it
// inherits that no-crash contract rather than re-testing it here). Suite name contains
// "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/ControllerHandshake.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

class FakeFeedbackSink : public RemoteFeedbackSink {
public:
    struct Sent {
        ControllerProfile::Input device;
        juce::MidiMessage message;
    };
    std::vector<Sent> sent;

    void sendFeedback(const ControllerProfile::Input& outputDevice, const juce::MidiMessage& message) override {
        sent.push_back({outputDevice, message});
    }
};

ControllerProfile makeProfile(const juce::String& id, const juce::String& deviceIdentifier,
                              std::vector<std::uint8_t> openMessage, std::vector<std::uint8_t> closeMessage) {
    ControllerProfile p;
    p.id = id;
    p.name = id;
    p.input.identifier = deviceIdentifier;
    p.input.name = deviceIdentifier;
    p.handshake.openMessage = std::move(openMessage);
    p.handshake.closeMessage = std::move(closeMessage);
    return p;
}

} // namespace

TEST(ControllerHandshakeTest, SendsOpenOnceForAProfileWithAnOpenDevice) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});

    coordinator.reconcile({profile}, {"dev1"});
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].device.identifier, "dev1");
    EXPECT_EQ(sink.sent[0].message.getRawDataSize(), 3);

    // A second reconcile with nothing changed must not re-send -- the device latches.
    coordinator.reconcile({profile}, {"dev1"});
    EXPECT_EQ(sink.sent.size(), 1u);
}

TEST(ControllerHandshakeTest, SendsNothingForAProfileWhoseDeviceIsNotOpen) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});

    coordinator.reconcile({profile}, {"some-other-dev"});
    EXPECT_TRUE(sink.sent.empty());
}

TEST(ControllerHandshakeTest, SendsNothingForATemplateWithNoHandshake) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    ControllerProfile profile;
    profile.id = "p1";
    profile.input.identifier = "dev1";
    ASSERT_TRUE(profile.handshake.isEmpty());

    coordinator.reconcile({profile}, {"dev1"});
    EXPECT_TRUE(sink.sent.empty());
}

TEST(ControllerHandshakeTest, SendsCloseWhenTheProfileIsRemoved) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    coordinator.reconcile({profile}, {"dev1"});
    ASSERT_EQ(sink.sent.size(), 1u);

    coordinator.reconcile({}, {"dev1"}); // profile gone -- device still open, doesn't matter
    ASSERT_EQ(sink.sent.size(), 2u);
    EXPECT_EQ(sink.sent[1].device.identifier, "dev1");
    EXPECT_EQ(sink.sent[1].message.getRawDataSize(), 3);
    EXPECT_EQ(sink.sent[1].message.getRawData()[1], 0x00); // the close byte, not the open one
}

TEST(ControllerHandshakeTest, SendsCloseWhenTheDeviceCloses) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    coordinator.reconcile({profile}, {"dev1"});
    ASSERT_EQ(sink.sent.size(), 1u);

    coordinator.reconcile({profile}, {}); // device unplugged, profile still there
    ASSERT_EQ(sink.sent.size(), 2u);
}

TEST(ControllerHandshakeTest, ReopeningAfterACloseSendsOpenAgain) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});

    coordinator.reconcile({profile}, {"dev1"});
    coordinator.reconcile({profile}, {});       // close
    coordinator.reconcile({profile}, {"dev1"}); // replug
    ASSERT_EQ(sink.sent.size(), 3u);
    EXPECT_EQ(sink.sent[2].message.getRawData()[1], 0x7F); // open again
}

TEST(ControllerHandshakeTest, ShutdownAllClosesEveryOpenProfileAndForgetsThem) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto p1 = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    const auto p2 = makeProfile("p2", "dev2", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    coordinator.reconcile({p1, p2}, {"dev1", "dev2"});
    ASSERT_EQ(sink.sent.size(), 2u);

    coordinator.shutdownAll();
    EXPECT_EQ(sink.sent.size(), 4u); // both closes

    // A second shutdownAll() has nothing left to close.
    coordinator.shutdownAll();
    EXPECT_EQ(sink.sent.size(), 4u);
}

TEST(ControllerHandshakeTest, EmptyCloseBytesSendNothingOnClose) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {}); // no close bytes at all
    coordinator.reconcile({profile}, {"dev1"});
    ASSERT_EQ(sink.sent.size(), 1u);

    coordinator.shutdownAll();
    EXPECT_EQ(sink.sent.size(), 1u); // nothing sent for the empty close message
}
