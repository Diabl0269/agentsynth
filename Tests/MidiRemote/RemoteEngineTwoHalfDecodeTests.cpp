// MIDI/audio thread: the two-message encodings -- paired 14-bit CCs (abs14 / abs14LsbFirst) and NRPN
// -- through RemoteEngine::handleMessage (docs/control/midi-remote.md#14-bit-and-nrpn-encodings).
// Same idiom as RemoteEngineDecodeTests.cpp: drive handleMessage() directly, read every decoded
// event back through drainActivity(). handleMessage's return value is "consumed" -- true only for a
// mapped message with "also pass mapped messages" off. Suite names contain "MidiRemote" per the
// ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <gtest/gtest.h>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";

struct TwoHalfHarness {
    RemoteEngine engine;
    std::vector<Control> controls;
    std::vector<Assignment> assignments;

    void addControl(const juce::String& id, MessageType type, int channel, int number, Encoding encoding) {
        Control control;
        control.id = id;
        control.name = id;
        control.message = {type, channel, number};
        control.encoding = encoding;
        controls.push_back(control);

        Assignment assignment;
        assignment.id = id + "-assign";
        assignment.control.profileId = "profile";
        assignment.control.controlId = id;
        assignment.spec = control.message;
        assignment.specEncoding = encoding;
        assignment.target.kind = Target::Kind::parameter;
        assignment.target.parameter.nodeUuid = "unresolvable-node";
        assignment.target.parameter.paramId = "unresolvable-param";
        assignments.push_back(assignment);
    }

    void finalize() {
        ControllerProfile profile;
        profile.id = "profile";
        profile.name = "profile";
        profile.input.identifier = kSource;
        profile.input.name = kSource;
        profile.controls = controls;
        engine.setProfiles({profile});
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(assignments);
    }

    bool cc(int channel, int number, int value) {
        return engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(channel, number, value));
    }

    std::vector<RemoteEvent> drainActivity() {
        std::vector<RemoteEvent> events;
        engine.drainActivity([&](const juce::String&, const RemoteEvent& event) { events.push_back(event); });
        return events;
    }

    // Arms NRPN `address` on `channel` (CC 99 = MSB, CC 98 = LSB).
    void armNrpn(int channel, int address) {
        cc(channel, 99, address >> 7);
        cc(channel, 98, address & 0x7f);
    }
};

float value14(int msb, int lsb) { return static_cast<float>((msb << 7) | lsb) / 16383.0f; }

} // namespace

// ============================================================================
// Paired CC: abs14 (MSB first) and abs14LsbFirst
// ============================================================================

TEST(MidiRemoteEngineTwoHalfTest, Abs14CommitsOnTheLsbUsingTheLastMsb) {
    TwoHalfHarness h;
    h.addControl("k", MessageType::cc, 1, 21, Encoding::abs14);
    h.finalize();

    EXPECT_TRUE(h.cc(1, 21, 64)) << "a held MSB is still consumed like any mapped message";
    EXPECT_TRUE(h.drainActivity().empty()) << "the first half alone moves nothing";

    EXPECT_TRUE(h.cc(1, 53, 5));
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, RemoteEventKind::absolute);
    EXPECT_NEAR(events[0].value, value14(64, 5), 1e-6f);
    EXPECT_EQ(events[0].specType, static_cast<std::uint8_t>(MessageType::cc));
    EXPECT_EQ(events[0].specNumber, 53) << "the completing half names the event";
    EXPECT_NE(events[0].slotIndex, -1) << "it reached the slot, not the unassigned activity path";
}

TEST(MidiRemoteEngineTwoHalfTest, Abs14LsbAloneRecommitsWithTheCachedMsb) {
    TwoHalfHarness h;
    h.addControl("k", MessageType::cc, 1, 21, Encoding::abs14);
    h.finalize();

    h.cc(1, 21, 100);
    h.cc(1, 53, 0);
    h.drainActivity();

    h.cc(1, 53, 77); // a fine-only movement: no new MSB
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, value14(100, 77), 1e-6f);
}

TEST(MidiRemoteEngineTwoHalfTest, Abs14PartialPairsProduceNothing) {
    TwoHalfHarness h;
    h.addControl("k", MessageType::cc, 1, 21, Encoding::abs14);
    h.finalize();

    h.cc(1, 53, 9); // an LSB with no MSB ever seen
    EXPECT_TRUE(h.drainActivity().empty());

    h.cc(1, 21, 10);
    h.cc(1, 21, 11); // two MSBs in a row: still held
    EXPECT_TRUE(h.drainActivity().empty());
}

TEST(MidiRemoteEngineTwoHalfTest, Abs14FullResolutionHasNoStairSteps) {
    TwoHalfHarness h;
    h.addControl("k", MessageType::cc, 1, 21, Encoding::abs14);
    h.finalize();

    h.cc(1, 21, 40);
    float previous = -1.0f;
    for (int lsb = 0; lsb < 128; ++lsb) {
        h.cc(1, 53, lsb);
        const auto events = h.drainActivity();
        ASSERT_EQ(events.size(), 1u);
        if (previous >= 0.0f)
            EXPECT_LT(events[0].value - previous, 1.5f / 16383.0f) << "one LSB is one 14-bit step, not a 7-bit jump";
        previous = events[0].value;
    }
}

TEST(MidiRemoteEngineTwoHalfTest, Abs14LsbFirstCommitsOnTheMsb) {
    TwoHalfHarness h;
    h.addControl("k", MessageType::cc, 1, 21, Encoding::abs14LsbFirst);
    h.finalize();

    EXPECT_TRUE(h.cc(1, 53, 3)); // LSB first: held
    EXPECT_TRUE(h.drainActivity().empty());

    h.cc(1, 21, 64);
    auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, value14(64, 3), 1e-6f);
    EXPECT_EQ(events[0].specNumber, 21);

    h.cc(1, 21, 65); // a coarse-only movement re-commits with the cached LSB
    events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, value14(65, 3), 1e-6f);
}

TEST(MidiRemoteEngineTwoHalfTest, PairedHalvesAreRememberedPerChannel) {
    TwoHalfHarness h;
    h.addControl("k", MessageType::cc, 0, 21, Encoding::abs14); // any channel
    h.finalize();

    h.cc(1, 21, 50);
    h.cc(2, 53, 9); // channel 2 never sent an MSB
    EXPECT_TRUE(h.drainActivity().empty());

    h.cc(1, 53, 9);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, value14(50, 9), 1e-6f);
    EXPECT_EQ(events[0].specChannel, 1);
}

TEST(MidiRemoteEngineTwoHalfTest, ExplicitMappingOnTheLsbNumberKeepsThatMessage) {
    TwoHalfHarness h;
    h.addControl("pair", MessageType::cc, 1, 21, Encoding::abs14);
    h.addControl("plain", MessageType::cc, 1, 53, Encoding::abs7);
    h.finalize();

    h.cc(1, 53, 127);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, 1.0f, 1e-6f) << "CC 53 is its own abs7 control, never a stolen LSB";
}

TEST(MidiRemoteEngineTwoHalfTest, UnmappedCcPairStaysTwoPlainEvents) {
    TwoHalfHarness h;
    h.finalize();

    EXPECT_FALSE(h.cc(1, 21, 64));
    EXPECT_FALSE(h.cc(1, 53, 5));
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 2u) << "pairing is the slot's decision; Detect folds unmapped halves itself";
    EXPECT_EQ(events[0].specNumber, 21);
    EXPECT_EQ(events[1].specNumber, 53);
}

// ============================================================================
// NRPN
// ============================================================================

TEST(MidiRemoteEngineTwoHalfTest, UnmappedNrpnShowsUpAsOneAddressedControl) {
    TwoHalfHarness h;
    h.finalize();

    EXPECT_FALSE(h.cc(1, 99, 1)) << "address CCs always pass through";
    EXPECT_FALSE(h.cc(1, 98, 2));
    EXPECT_TRUE(h.drainActivity().empty()) << "address CCs are state, not events";

    EXPECT_FALSE(h.cc(1, 6, 64)) << "unmapped data entry passes through too";
    h.cc(1, 38, 3);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].specType, static_cast<std::uint8_t>(MessageType::nrpn));
    EXPECT_EQ(events[0].specNumber, (1 << 7) | 2) << "the address is the key";
    EXPECT_EQ(events[0].specChannel, 1);
    EXPECT_NEAR(events[0].value, value14(64, 0), 1e-6f);
    EXPECT_NEAR(events[1].value, value14(64, 3), 1e-6f);
}

TEST(MidiRemoteEngineTwoHalfTest, MappedNrpnAbs14CommitsOnDataLsbAndConsumesDataCcs) {
    TwoHalfHarness h;
    h.addControl("n", MessageType::nrpn, 1, 1000, Encoding::abs14);
    h.finalize();

    EXPECT_FALSE(h.cc(1, 99, 1000 >> 7)) << "address CCs are never consumed";
    EXPECT_FALSE(h.cc(1, 98, 1000 & 0x7f));
    EXPECT_TRUE(h.cc(1, 6, 90)) << "a mapped NRPN's data CCs are consumed";
    EXPECT_TRUE(h.drainActivity().empty());

    EXPECT_TRUE(h.cc(1, 38, 17));
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specNumber, 1000);
    EXPECT_NEAR(events[0].value, value14(90, 17), 1e-6f);
    EXPECT_NE(events[0].slotIndex, -1);
}

TEST(MidiRemoteEngineTwoHalfTest, NrpnAbs7UsesDataMsbOnly) {
    TwoHalfHarness h;
    h.addControl("n", MessageType::nrpn, 1, 300, Encoding::abs7);
    h.finalize();

    h.armNrpn(1, 300);
    h.cc(1, 6, 127);
    auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, 1.0f, 1e-6f);

    EXPECT_TRUE(h.cc(1, 38, 5));
    EXPECT_TRUE(h.drainActivity().empty()) << "CC 38 is ignored (but consumed) on a 7-bit NRPN";
}

TEST(MidiRemoteEngineTwoHalfTest, NrpnAddressChangeMidStreamNeverMixesValues) {
    TwoHalfHarness h;
    h.addControl("a", MessageType::nrpn, 1, 130, Encoding::abs14);
    h.addControl("b", MessageType::nrpn, 1, 131, Encoding::abs14);
    h.finalize();

    h.armNrpn(1, 130);
    h.cc(1, 6, 100);
    h.cc(1, 38, 50);
    auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specNumber, 130);

    h.armNrpn(1, 131);
    h.cc(1, 38, 9); // an LSB for the new address with no MSB of its own yet
    EXPECT_TRUE(h.drainActivity().empty()) << "address A's MSB must not carry over to address B";

    h.cc(1, 6, 20);
    h.cc(1, 38, 9);
    events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specNumber, 131);
    EXPECT_NEAR(events[0].value, value14(20, 9), 1e-6f);
}

TEST(MidiRemoteEngineTwoHalfTest, NrpnAddressesAreTrackedPerChannel) {
    TwoHalfHarness h;
    h.addControl("a", MessageType::nrpn, 0, 130, Encoding::abs14);
    h.finalize();

    h.armNrpn(1, 130);
    h.armNrpn(2, 999); // another channel arming a different address
    h.cc(1, 6, 10);
    h.cc(1, 38, 1);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specNumber, 130);
}

TEST(MidiRemoteEngineTwoHalfTest, PartialNrpnAddressIsNotArmed) {
    TwoHalfHarness h;
    h.finalize();

    h.cc(1, 99, 3); // no CC 98 yet
    h.cc(1, 6, 64);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specType, static_cast<std::uint8_t>(MessageType::cc)) << "an unarmed CC 6 is a plain CC";
    EXPECT_EQ(events[0].specNumber, 6);
}

TEST(MidiRemoteEngineTwoHalfTest, RpnSelectDisarmsTheNrpnAddress) {
    TwoHalfHarness h;
    h.finalize();

    h.armNrpn(1, 5);
    h.cc(1, 101, 0);
    h.cc(1, 100, 0);
    h.cc(1, 6, 12);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specType, static_cast<std::uint8_t>(MessageType::cc));
    EXPECT_EQ(events[0].specNumber, 6);
}

TEST(MidiRemoteEngineTwoHalfTest, ExplicitMappingOnCc6BeatsItsNrpnReading) {
    TwoHalfHarness h;
    h.addControl("plain", MessageType::cc, 1, 6, Encoding::abs7);
    h.finalize();

    h.armNrpn(1, 300);
    h.cc(1, 6, 127);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].specType, static_cast<std::uint8_t>(MessageType::cc));
    EXPECT_NEAR(events[0].value, 1.0f, 1e-6f);
}

TEST(MidiRemoteEngineTwoHalfTest, EventsCarryAMillisecondStamp) {
    TwoHalfHarness h;
    h.finalize();

    const auto before = static_cast<std::uint16_t>(juce::Time::getMillisecondCounter() & 0xffffu);
    h.cc(1, 21, 1);
    const auto events = h.drainActivity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_LE(static_cast<std::uint16_t>(events[0].timeMs - before), 1000);
}
