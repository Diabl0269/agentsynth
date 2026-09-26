// FRO141 (docs/control/midi-remote.md#focus-bank): Control::focusBank round-trip/rejection
// coverage -- mirrors RemoteModelPagesTests.cpp's style. The headline guarantee: every pre-FRO141
// document (no "focusBank" property anywhere) loads with focusBank == false and re-serialises
// byte-identical to what it started as. Suite name contains "MidiRemote" per the ship-task
// --gtest_filter convention.

#include "MidiRemote/RemoteModel.h"
#include <gtest/gtest.h>

using namespace synth;

namespace {

Control makeV1Control() {
    Control c;
    c.id = "control-1";
    c.name = "Knob 1";
    c.kind = ControlKind::knob;
    c.message.type = MessageType::cc;
    c.message.channel = 1;
    c.message.number = 21;
    c.encoding = Encoding::abs7;
    c.buttonMode = ButtonMode::momentary;
    c.layout.col = 0;
    c.layout.row = 0;
    return c;
}

} // namespace

TEST(MidiRemoteModelFocusBankTest, ControlDefaultsToFalseAndOmitsItFromJson) {
    const auto c = makeV1Control();
    ASSERT_FALSE(c.focusBank);
    const juce::var v = c.toVar();
    EXPECT_FALSE(v.getDynamicObject()->hasProperty("focusBank"))
        << "a pre-FRO141 document must round-trip byte-identical";

    Control parsed;
    ASSERT_TRUE(Control::fromVar(v, parsed));
    EXPECT_FALSE(parsed.focusBank);
}

TEST(MidiRemoteModelFocusBankTest, ControlFocusBankTrueRoundTrips) {
    auto c = makeV1Control();
    c.focusBank = true;
    const juce::var v = c.toVar();
    EXPECT_TRUE(v.getDynamicObject()->getProperty("focusBank").equals(true));

    Control parsed;
    ASSERT_TRUE(Control::fromVar(v, parsed));
    EXPECT_TRUE(parsed.focusBank);
}

TEST(MidiRemoteModelFocusBankTest, ControlRejectsNonBoolFocusBank) {
    juce::var v = makeV1Control().toVar();
    v.getDynamicObject()->setProperty("focusBank", 1); // truthy int, not a strict bool
    Control parsed;
    EXPECT_FALSE(Control::fromVar(v, parsed));
}

TEST(MidiRemoteModelFocusBankTest, ControlRejectsStringFocusBank) {
    juce::var v = makeV1Control().toVar();
    v.getDynamicObject()->setProperty("focusBank", "true");
    Control parsed;
    EXPECT_FALSE(Control::fromVar(v, parsed));
}

// The full-profile round-trip: a profile with a mix of focus-bank and ordinary controls survives
// ControllerProfile::toVar/fromVar, and an old (no focusBank anywhere) profile stays byte-identical.
TEST(MidiRemoteModelFocusBankTest, ProfileWithFocusBankControlsRoundTripsAndOldProfileIsByteIdentical) {
    ControllerProfile profile;
    profile.version = 1;
    profile.id = "profile-1";
    profile.name = "Controller";
    profile.input.identifier = "dev-1";
    profile.input.name = "Device 1";

    auto plain = makeV1Control();
    auto bank = makeV1Control();
    bank.id = "control-2";
    bank.focusBank = true;
    bank.layout.col = 1;
    bank.message.number = 22; // distinct MessageSpec -- two controls may not share one
    profile.controls = {plain, bank};

    const juce::var beforeJson = profile.toVar();
    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(beforeJson));
    ASSERT_EQ(parsed.controls.size(), 2u);
    EXPECT_FALSE(parsed.controls[0].focusBank);
    EXPECT_TRUE(parsed.controls[1].focusBank);
    EXPECT_EQ(juce::JSON::toString(parsed.toVar()), juce::JSON::toString(beforeJson));

    // No control on the profile ever sets focusBank -- the whole document must be byte-identical
    // to a pre-FRO141 profile that never mentions the key at all.
    ControllerProfile oldProfile = profile;
    oldProfile.controls = {plain};
    const juce::var oldJson = oldProfile.toVar();
    ControllerProfile oldParsed;
    ASSERT_TRUE(oldParsed.fromVar(oldJson));
    EXPECT_EQ(juce::JSON::toString(oldParsed.toVar()), juce::JSON::toString(oldJson));
}
