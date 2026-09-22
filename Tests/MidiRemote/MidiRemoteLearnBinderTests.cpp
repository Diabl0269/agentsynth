// synth::midi::bindLearnResult (Source/MidiRemote/MidiRemoteLearnBinder.cpp): auto-profile /
// auto-control creation from a settled LearnResult
// (docs/control/midi-remote.md#learn-what-does-the-first-message-mean's "Auto-profile"). Pure and
// headless -- no ControllerProfileStore, no RemoteEngine, no file I/O.

#include "MidiRemote/MidiRemoteLearnBinder.h"

#include <gtest/gtest.h>

using namespace synth;
using namespace synth::midi;

namespace {

LearnResult makeResult(MessageType type, int number, const juce::String& sourceKey = "dev-id") {
    LearnResult r;
    r.sourceKey = sourceKey;
    r.spec.type = type;
    r.spec.channel = 1;
    r.spec.number = number;
    r.encoding = Encoding::abs7;
    r.buttonMode = ButtonMode::momentary;
    r.target.kind = Target::Kind::parameter;
    r.target.parameter.nodeUuid = "node-uuid";
    r.target.parameter.paramId = "cutoff";
    return r;
}

} // namespace

TEST(MidiRemoteLearnBinderTest, UnknownDeviceCreatesANewProfileNamedAfterIt) {
    const auto outcome = bindLearnResult(makeResult(MessageType::cc, 21), "Launchkey Mini MK3", {});

    EXPECT_TRUE(outcome.profileIsNew);
    EXPECT_TRUE(outcome.controlIsNew);
    EXPECT_EQ(outcome.profile.name, "Launchkey Mini MK3");
    EXPECT_EQ(outcome.profile.input.identifier, "dev-id");
    EXPECT_FALSE(outcome.profile.id.isEmpty());
    ASSERT_EQ(outcome.profile.controls.size(), 1u);
    EXPECT_EQ(outcome.profile.controls[0].name, "CC 21");
    EXPECT_EQ(outcome.profile.controls[0].kind, ControlKind::knob);
}

TEST(MidiRemoteLearnBinderTest, NoteLearnNamesAndKindsAsAButton) {
    const auto outcome = bindLearnResult(makeResult(MessageType::note, 60), "Pad Controller", {});

    ASSERT_EQ(outcome.profile.controls.size(), 1u);
    EXPECT_EQ(outcome.profile.controls[0].name, "Note C3");
    EXPECT_EQ(outcome.profile.controls[0].kind, ControlKind::button);
}

TEST(MidiRemoteLearnBinderTest, KnownDeviceReusesItsExistingProfile) {
    ControllerProfile existing;
    existing.id = "profile-1";
    existing.name = "Launchkey Mini MK3";
    existing.input.identifier = "dev-id";
    existing.input.name = "Launchkey Mini MK3";

    const auto outcome = bindLearnResult(makeResult(MessageType::cc, 21), "Launchkey Mini MK3", {existing});

    EXPECT_FALSE(outcome.profileIsNew);
    EXPECT_TRUE(outcome.controlIsNew);
    EXPECT_EQ(outcome.profile.id, "profile-1");
}

TEST(MidiRemoteLearnBinderTest, SameMessageKeyOnExistingProfileReusesTheControlNotAppendsANewOne) {
    ControllerProfile existing;
    existing.id = "profile-1";
    existing.input.identifier = "dev-id";
    Control knob;
    knob.id = "control-1";
    knob.name = "Knob 1";
    knob.kind = ControlKind::knob;
    knob.message.type = MessageType::cc;
    knob.message.channel = 1;
    knob.message.number = 21;
    knob.encoding = Encoding::abs7;
    knob.layout.col = 3;
    knob.layout.row = 2;
    existing.controls.push_back(knob);

    const auto outcome = bindLearnResult(makeResult(MessageType::cc, 21), "Launchkey Mini MK3", {existing});

    EXPECT_FALSE(outcome.controlIsNew);
    ASSERT_EQ(outcome.profile.controls.size(), 1u) << "must not append a duplicate control for the same message key";
    EXPECT_EQ(outcome.profile.controls[0].id, "control-1");
    EXPECT_EQ(outcome.profile.controls[0].name, "Knob 1") << "identity/name/layout are kept on a re-learn";
    EXPECT_EQ(outcome.profile.controls[0].layout.col, 3);
    EXPECT_EQ(outcome.profile.controls[0].layout.row, 2);
    EXPECT_EQ(outcome.assignment.control.controlId, "control-1");
}

TEST(MidiRemoteLearnBinderTest, NewControlsFillTheGridRowMajorSkippingTakenCells) {
    ControllerProfile existing;
    existing.id = "profile-1";
    existing.input.identifier = "dev-id";
    for (int col = 0; col < 8; ++col) {
        Control c;
        c.id = "existing-" + juce::String(col);
        c.message.type = MessageType::cc;
        c.message.number = col; // 8 distinct keys, filling row 0 entirely
        c.layout.col = col;
        c.layout.row = 0;
        existing.controls.push_back(c);
    }

    const auto outcome = bindLearnResult(makeResult(MessageType::cc, 99), "Dev", {existing});

    ASSERT_EQ(outcome.profile.controls.size(), 9u);
    const auto& added = outcome.profile.controls.back();
    EXPECT_EQ(added.layout.col, 0);
    EXPECT_EQ(added.layout.row, 1) << "row 0 is full -- the new control drops to the next row";
}

TEST(MidiRemoteLearnBinderTest, AssignmentCarriesADenormalisedCopyOfTheControlSpecAndTheTarget) {
    const auto result = makeResult(MessageType::cc, 21);
    const auto outcome = bindLearnResult(result, "Launchkey Mini MK3", {});

    EXPECT_FALSE(outcome.assignment.id.isEmpty());
    EXPECT_EQ(outcome.assignment.control.profileId, outcome.profile.id);
    EXPECT_EQ(outcome.assignment.control.controlId, outcome.profile.controls[0].id);
    EXPECT_TRUE(outcome.assignment.spec == result.spec);
    EXPECT_EQ(outcome.assignment.specEncoding, Encoding::abs7);
    EXPECT_EQ(outcome.assignment.specControlName, outcome.profile.controls[0].name);
    EXPECT_TRUE(outcome.assignment.target.isParameter());
    EXPECT_EQ(outcome.assignment.target.parameter.paramId, "cutoff");
    EXPECT_EQ(outcome.assignment.takeover, Takeover::useDefault);
    EXPECT_TRUE(outcome.assignment.enabled);
}
