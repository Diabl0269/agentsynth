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

// Every pre-existing test below used a profile whose `input` identifier/name are literally the
// device's own key ("dev1"/"dev2") -- a symmetric-name device is exactly the case
// resolveHandshakeOutput()'s exact-identifier-match step still resolves in one step, so this output
// list preserves their original behaviour byte-for-byte after reconcile() grew its third parameter.
std::vector<ControllerProfile::Input> symmetricOutputs(std::initializer_list<juce::String> deviceIdentifiers) {
    std::vector<ControllerProfile::Input> outputs;
    for (const auto& id : deviceIdentifiers)
        outputs.push_back({id, id});
    return outputs;
}

} // namespace

TEST(ControllerHandshakeTest, SendsOpenOnceForAProfileWithAnOpenDevice) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    const auto outputs = symmetricOutputs({"dev1"});

    coordinator.reconcile({profile}, {"dev1"}, outputs);
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].device.identifier, "dev1");
    EXPECT_EQ(sink.sent[0].message.getRawDataSize(), 3);

    // A second reconcile with nothing changed must not re-send -- the device latches.
    coordinator.reconcile({profile}, {"dev1"}, outputs);
    EXPECT_EQ(sink.sent.size(), 1u);
}

TEST(ControllerHandshakeTest, SendsNothingForAProfileWhoseDeviceIsNotOpen) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});

    coordinator.reconcile({profile}, {"some-other-dev"}, symmetricOutputs({"dev1"}));
    EXPECT_TRUE(sink.sent.empty());
}

TEST(ControllerHandshakeTest, SendsNothingForATemplateWithNoHandshake) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    ControllerProfile profile;
    profile.id = "p1";
    profile.input.identifier = "dev1";
    ASSERT_TRUE(profile.handshake.isEmpty());

    coordinator.reconcile({profile}, {"dev1"}, symmetricOutputs({"dev1"}));
    EXPECT_TRUE(sink.sent.empty());
}

TEST(ControllerHandshakeTest, SendsNothingWhenNoOutputResolves) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});

    // The device's INPUT is open, but no output anywhere matches it -- e.g. an output-less device,
    // or (pre-FRO339's bug) an asymmetric in/out name pair with no port hint to bridge them.
    coordinator.reconcile({profile}, {"dev1"}, {});
    EXPECT_TRUE(sink.sent.empty());
}

TEST(ControllerHandshakeTest, SendsCloseWhenTheProfileIsRemoved) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    const auto outputs = symmetricOutputs({"dev1"});
    coordinator.reconcile({profile}, {"dev1"}, outputs);
    ASSERT_EQ(sink.sent.size(), 1u);

    coordinator.reconcile({}, {"dev1"}, outputs); // profile gone -- device still open, doesn't matter
    ASSERT_EQ(sink.sent.size(), 2u);
    EXPECT_EQ(sink.sent[1].device.identifier, "dev1");
    EXPECT_EQ(sink.sent[1].message.getRawDataSize(), 3);
    EXPECT_EQ(sink.sent[1].message.getRawData()[1], 0x00); // the close byte, not the open one
}

TEST(ControllerHandshakeTest, SendsCloseWhenTheDeviceCloses) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    const auto outputs = symmetricOutputs({"dev1"});
    coordinator.reconcile({profile}, {"dev1"}, outputs);
    ASSERT_EQ(sink.sent.size(), 1u);

    coordinator.reconcile({profile}, {}, outputs); // device unplugged, profile still there
    ASSERT_EQ(sink.sent.size(), 2u);
}

TEST(ControllerHandshakeTest, ReopeningAfterACloseSendsOpenAgain) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto profile = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    const auto outputs = symmetricOutputs({"dev1"});

    coordinator.reconcile({profile}, {"dev1"}, outputs);
    coordinator.reconcile({profile}, {}, outputs);       // close
    coordinator.reconcile({profile}, {"dev1"}, outputs); // replug
    ASSERT_EQ(sink.sent.size(), 3u);
    EXPECT_EQ(sink.sent[2].message.getRawData()[1], 0x7F); // open again
}

TEST(ControllerHandshakeTest, ShutdownAllClosesEveryOpenProfileAndForgetsThem) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    const auto p1 = makeProfile("p1", "dev1", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    const auto p2 = makeProfile("p2", "dev2", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    coordinator.reconcile({p1, p2}, {"dev1", "dev2"}, symmetricOutputs({"dev1", "dev2"}));
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
    coordinator.reconcile({profile}, {"dev1"}, symmetricOutputs({"dev1"}));
    ASSERT_EQ(sink.sent.size(), 1u);

    coordinator.shutdownAll();
    EXPECT_EQ(sink.sent.size(), 1u); // nothing sent for the empty close message
}

// -- FRO339: the asymmetric-port real-hardware case (2026-09-28) ------------------------------
// A connected Launch Control XL 3 showed CoreMIDI naming its four ports asymmetrically: the
// SOURCES (this app's `input` choices) are "LCXL3 1 MIDI Out"/"LCXL3 1 DAW Out"; the DESTINATIONS
// (what a handshake actually writes to) are "LCXL3 1 MIDI In"/"LCXL3 1 DAW In"/two "To DIN Out"
// ports -- no destination shares either source's exact name, so the pre-FRO339 code (send to
// whatever matches `input` literally) always failed silently for this device.

std::vector<ControllerProfile::Input> lcxl3Outputs() {
    return {{"out-midi-in", "LCXL3 1 MIDI In"},
            {"out-daw-in", "LCXL3 1 DAW In"},
            {"out-din-1", "LCXL3 1 To DIN Out"},
            {"out-din-2", "LCXL3 1 To DIN Out 2"}};
}

TEST(ResolveHandshakeOutputTest, ExactIdentifierMatchWinsOverEverythingElse) {
    ControllerProfile::Input input{"out-daw-in", "some unrelated name"};
    const auto resolved = resolveHandshakeOutput(input, "", lcxl3Outputs());
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->identifier, "out-daw-in");
}

TEST(ResolveHandshakeOutputTest, ExactNameMatchForASymmetricallyNamedDevice) {
    ControllerProfile::Input input{"", "LCXL3 1 DAW In"}; // already the OUTPUT's own name
    const auto resolved = resolveHandshakeOutput(input, "", lcxl3Outputs());
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->name, "LCXL3 1 DAW In");
}

TEST(ResolveHandshakeOutputTest, TrailingOutSwappedToInResolvesTheRightPortWithNoHint) {
    ControllerProfile::Input input{"", "LCXL3 1 DAW Out"}; // the correct input, no hint needed
    const auto resolved = resolveHandshakeOutput(input, "", lcxl3Outputs());
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->name, "LCXL3 1 DAW In");
}

TEST(ResolveHandshakeOutputTest, PortHintRetargetsTheWrongSiblingPortBeforeMatching) {
    ControllerProfile::Input input{"", "LCXL3 1 MIDI Out"}; // the WRONG port picked as input
    const auto resolved = resolveHandshakeOutput(input, "DAW", lcxl3Outputs());
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->name, "LCXL3 1 DAW In");
}

TEST(ResolveHandshakeOutputTest, NoneWhenNothingMatches) {
    ControllerProfile::Input input{"", "Some Other Device"};
    EXPECT_FALSE(resolveHandshakeOutput(input, "", lcxl3Outputs()).has_value());
}

TEST(ResolveHandshakeOutputTest, NoneWhenTheHintedPortHasNoOutputEither) {
    ControllerProfile::Input input{"", "LCXL3 1 USB Out"};
    EXPECT_FALSE(resolveHandshakeOutput(input, "Bluetooth", lcxl3Outputs()).has_value());
}

TEST(ApplyHandshakePortHintTest, SwapsTheWordBeforeTheTrailingDirectionWord) {
    EXPECT_EQ(applyHandshakePortHint("LCXL3 1 MIDI Out", "DAW"), "LCXL3 1 DAW Out");
    EXPECT_EQ(applyHandshakePortHint("LCXL3 1 DAW Out", "DAW"), "LCXL3 1 DAW Out"); // already right
}

TEST(ApplyHandshakePortHintTest, LeavesNameUnchangedWithNoHintOrNoDirectionWord) {
    EXPECT_EQ(applyHandshakePortHint("LCXL3 1 MIDI Out", ""), "LCXL3 1 MIDI Out");
    EXPECT_EQ(applyHandshakePortHint("Widget", "DAW"), "Widget");
}

TEST(DescribeHandshakeIssueTest, EmptyWhenResolvedAndInputAlreadyMatchesTheHint) {
    EXPECT_TRUE(describeHandshakeIssue({"", "LCXL3 1 DAW Out"}, "DAW", true).isEmpty());
}

TEST(DescribeHandshakeIssueTest, WarnsToPickTheHintedPortEvenIfTheHandshakeStillResolved) {
    const auto message = describeHandshakeIssue({"", "LCXL3 1 MIDI Out"}, "DAW", /*resolved=*/true);
    EXPECT_EQ(message, "This template needs the device's DAW port: pick \"LCXL3 1 DAW Out\" as the input.");
}

TEST(DescribeHandshakeIssueTest, WarnsWhenNothingResolvedAndThereIsNoHint) {
    const auto message = describeHandshakeIssue({"", "Some Device"}, "", /*resolved=*/false);
    EXPECT_EQ(message, "This controller's handshake couldn't find a matching MIDI output for \"Some Device\".");
}

// -- FRO339: the coordinator end-to-end, through the real names above -------------------------

TEST(ControllerHandshakeTest, SendsTheHandshakeToTheHintedDawInputEvenWhenTheWrongPortWasChosen) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    ControllerProfile profile =
        makeProfile("p1", "dev-midi-out", {0xF0, 0x00, 0x20, 0x29, 0x02, 0x15, 0x02, 0x7F, 0xF7},
                    {0xF0, 0x00, 0x20, 0x29, 0x02, 0x15, 0x02, 0x00, 0xF7});
    profile.input.name = "LCXL3 1 MIDI Out"; // the wrong port, picked in the AddController popover
    profile.handshake.port = "DAW";

    coordinator.reconcile({profile}, {"dev-midi-out"}, lcxl3Outputs());
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].device.name, "LCXL3 1 DAW In");
    EXPECT_EQ(sink.sent[0].message.getRawData()[7], 0x7F); // the DAW-mode-enable byte

    EXPECT_EQ(coordinator.getHandshakeIssue("p1"),
              "This template needs the device's DAW port: pick \"LCXL3 1 DAW Out\" as the input.");

    coordinator.shutdownAll();
    ASSERT_EQ(sink.sent.size(), 2u);
    EXPECT_EQ(sink.sent[1].device.name, "LCXL3 1 DAW In"); // close to the SAME resolved output
}

TEST(ControllerHandshakeTest, NoIssueOnceTheCorrectDawPortIsChosen) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    ControllerProfile profile = makeProfile("p1", "dev-daw-out", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    profile.input.name = "LCXL3 1 DAW Out";
    profile.handshake.port = "DAW";

    coordinator.reconcile({profile}, {"dev-daw-out"}, lcxl3Outputs());
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].device.name, "LCXL3 1 DAW In");
    EXPECT_TRUE(coordinator.getHandshakeIssue("p1").isEmpty());
}

TEST(ControllerHandshakeTest, IssueClearsWhenTheProfileIsRemovedOrTheDeviceCloses) {
    FakeFeedbackSink sink;
    ControllerHandshakeCoordinator coordinator(sink);
    ControllerProfile profile = makeProfile("p1", "dev-midi-out", {0xF0, 0x7F, 0xF7}, {0xF0, 0x00, 0xF7});
    profile.input.name = "LCXL3 1 MIDI Out";
    profile.handshake.port = "DAW";

    coordinator.reconcile({profile}, {"dev-midi-out"}, lcxl3Outputs());
    ASSERT_FALSE(coordinator.getHandshakeIssue("p1").isEmpty());

    coordinator.reconcile({profile}, {}, lcxl3Outputs()); // device closed
    EXPECT_TRUE(coordinator.getHandshakeIssue("p1").isEmpty());
}
