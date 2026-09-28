// ControllerProfile::Handshake round-trip/ rejection coverage -- mirrors
// RemoteModelFocusBankTests.cpp's style. The headline guarantee: every document (no "handshake"
// property at all) loads with an empty Handshake and re-serialises byte-identical to what it
// started as. Suite name contains "MidiRemote" per the ship-task --gtest_filter convention (see
// docs/control/midi-remote-device-handshake.md#device-handshake).

#include "MidiRemote/RemoteModel.h"
#include <gtest/gtest.h>

using namespace synth;

namespace {

ControllerProfile makeV1Profile() {
    ControllerProfile p;
    p.id = "profile-1";
    p.name = "Some Controller";
    p.input.identifier = "in-id";
    p.input.name = "in-name";
    return p;
}

} // namespace

TEST(MidiRemoteModelHandshakeTest, DefaultsToEmptyAndOmitsItFromJson) {
    const auto p = makeV1Profile();
    ASSERT_TRUE(p.handshake.isEmpty());
    const juce::var v = p.toVar();
    EXPECT_FALSE(v.getDynamicObject()->hasProperty("handshake"))
        << "a pre-FRO339 document must round-trip byte-identical";

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_TRUE(parsed.handshake.isEmpty());
}

TEST(MidiRemoteModelHandshakeTest, RoundTrips) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0xF0, 0x00, 0x7F, 0xF7};
    p.handshake.closeMessage = {0xF0, 0x00, 0x00, 0xF7};
    const juce::var v = p.toVar();
    EXPECT_TRUE(v.getDynamicObject()->hasProperty("handshake"));

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_EQ(parsed.handshake.openMessage, p.handshake.openMessage);
    EXPECT_EQ(parsed.handshake.closeMessage, p.handshake.closeMessage);
}

TEST(MidiRemoteModelHandshakeTest, EmptyByteArraysAreALegalHandshakeAndRoundTrip) {
    auto p = makeV1Profile();
    // Not isEmpty() -- open/close are only BOTH empty by default; a single non-empty array is
    // enough to make the whole handshake present in the JSON.
    p.handshake.closeMessage = {0x7F};
    ASSERT_FALSE(p.handshake.isEmpty());

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(p.toVar()));
    EXPECT_TRUE(parsed.handshake.openMessage.empty());
    EXPECT_EQ(parsed.handshake.closeMessage, p.handshake.closeMessage);
}

TEST(MidiRemoteModelHandshakeTest, RejectsMissingCloseKey) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0x01};
    juce::var v = p.toVar();
    v.getDynamicObject()->getProperty("handshake").getDynamicObject()->removeProperty("close");

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

TEST(MidiRemoteModelHandshakeTest, RejectsOutOfRangeByte) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0x01};
    p.handshake.closeMessage = {0x02};
    juce::var v = p.toVar();
    auto* handshakeObj = v.getDynamicObject()->getProperty("handshake").getDynamicObject();
    juce::Array<juce::var> bad;
    bad.add(256); // out of 0..255
    handshakeObj->setProperty("open", bad);

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

TEST(MidiRemoteModelHandshakeTest, RejectsNonArrayOpen) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0x01};
    p.handshake.closeMessage = {0x02};
    juce::var v = p.toVar();
    auto* handshakeObj = v.getDynamicObject()->getProperty("handshake").getDynamicObject();
    handshakeObj->setProperty("open", "not an array");

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

// The port hint round-trips like every other optional field here -- absent by default, present only
// when non-empty, and a present but non-string value is a hard rejection, same all-or-nothing
// convention as the rest of this file (see
// docs/control/midi-remote-device-handshake.md#device-handshake).
TEST(MidiRemoteModelHandshakeTest, PortHintDefaultsToEmptyAndIsOmittedFromJson) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0x01};
    p.handshake.closeMessage = {0x02};
    ASSERT_TRUE(p.handshake.port.isEmpty());

    const juce::var v = p.toVar();
    auto* handshakeObj = v.getDynamicObject()->getProperty("handshake").getDynamicObject();
    EXPECT_FALSE(handshakeObj->hasProperty("port"));

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_TRUE(parsed.handshake.port.isEmpty());
}

TEST(MidiRemoteModelHandshakeTest, PortHintRoundTrips) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0x01};
    p.handshake.closeMessage = {0x02};
    p.handshake.port = "DAW";

    const juce::var v = p.toVar();
    auto* handshakeObj = v.getDynamicObject()->getProperty("handshake").getDynamicObject();
    EXPECT_EQ(handshakeObj->getProperty("port").toString(), "DAW");

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_EQ(parsed.handshake.port, "DAW");
}

TEST(MidiRemoteModelHandshakeTest, RejectsNonStringPort) {
    auto p = makeV1Profile();
    p.handshake.openMessage = {0x01};
    p.handshake.closeMessage = {0x02};
    juce::var v = p.toVar();
    v.getDynamicObject()->getProperty("handshake").getDynamicObject()->setProperty("port", 7);

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}
