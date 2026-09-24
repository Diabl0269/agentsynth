// MidiRemoteMappingTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn,
// #controllers-list-left): the headless assignment builder and the two orphan-controller repairs.
// Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/MidiRemoteMapping.h"

#include <gtest/gtest.h>

using namespace synth;
using namespace synth::midi;

namespace {

Control makeControl(const juce::String& id, MessageType type, int number, const juce::String& name = {}) {
    Control c;
    c.id = id;
    c.name = name.isNotEmpty() ? name : "Ctl " + id;
    c.message.type = type;
    c.message.channel = 1;
    c.message.number = number;
    return c;
}

Assignment makeOrphanAssignment(const juce::String& id, MessageType type, int number, const juce::String& name) {
    Assignment a;
    a.id = id;
    a.control.profileId = "orphan";
    a.control.controlId = "old-" + id;
    a.spec.type = type;
    a.spec.channel = 1;
    a.spec.number = number;
    a.specControlName = name;
    a.specEncoding = Encoding::relTwos;
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = "node";
    a.target.parameter.paramId = id;
    return a;
}

} // namespace

TEST(MidiRemoteMappingTest, MakeAssignmentDenormalisesTheControlAndUsesDefaults) {
    ControllerProfile profile;
    profile.id = "p1";
    auto control = makeControl("c1", MessageType::cc, 21, "Knob 1");
    control.encoding = Encoding::relBinOffset;
    control.buttonMode = ButtonMode::toggle;
    Target target;
    target.kind = Target::Kind::action;
    target.action.actionId = "togglePlayback";

    const auto a = makeAssignmentForControl(profile, control, target);

    EXPECT_FALSE(a.id.isEmpty());
    EXPECT_EQ(a.control.profileId, "p1");
    EXPECT_EQ(a.control.controlId, "c1");
    EXPECT_EQ(a.spec, control.message);
    EXPECT_EQ(a.specControlName, "Knob 1");
    EXPECT_EQ(a.specEncoding, Encoding::relBinOffset);
    EXPECT_EQ(a.specButtonMode, ButtonMode::toggle);
    EXPECT_EQ(a.takeover, Takeover::useDefault);
    EXPECT_DOUBLE_EQ(a.range.min, 0.0);
    EXPECT_DOUBLE_EQ(a.range.max, 1.0);
    EXPECT_TRUE(a.target.isAction());
}

TEST(MidiRemoteMappingTest, RelinkMatchesBySpecAndLeavesTheRestOrphaned) {
    ControllerProfile target;
    target.id = "present";
    target.controls = {makeControl("t1", MessageType::cc, 21, "Cutoff Knob"), makeControl("t2", MessageType::note, 36)};

    std::vector<Assignment> assignments = {makeOrphanAssignment("a", MessageType::cc, 21, "CC 21"),
                                           makeOrphanAssignment("b", MessageType::cc, 99, "CC 99")};
    Assignment other = makeOrphanAssignment("c", MessageType::cc, 21, "elsewhere");
    other.control.profileId = "someone-else";
    assignments.push_back(other);

    const auto result = relinkAssignments(assignments, "orphan", target);

    EXPECT_EQ(result.matched, 1);
    EXPECT_EQ(result.unmatched, 1);
    EXPECT_EQ(assignments[0].control.profileId, "present");
    EXPECT_EQ(assignments[0].control.controlId, "t1");
    EXPECT_EQ(assignments[0].specControlName, "Cutoff Knob") << "name refreshed from the matched control";
    EXPECT_EQ(assignments[1].control.profileId, "orphan") << "no control with that message: stays orphaned";
    EXPECT_EQ(assignments[1].control.controlId, "old-b");
    EXPECT_EQ(assignments[2].control.profileId, "someone-else") << "other controllers' assignments untouched";
}

TEST(MidiRemoteMappingTest, RelinkRequiresTheSameChannelAndNumber) {
    ControllerProfile target;
    target.id = "present";
    auto onChannel2 = makeControl("t1", MessageType::cc, 21);
    onChannel2.message.channel = 2;
    target.controls = {onChannel2};
    std::vector<Assignment> assignments = {makeOrphanAssignment("a", MessageType::cc, 21, "CC 21")};

    const auto result = relinkAssignments(assignments, "orphan", target);

    EXPECT_EQ(result.matched, 0);
    EXPECT_EQ(result.unmatched, 1);
}

TEST(MidiRemoteMappingTest, RecreateMintsOneControlPerDistinctSpecAndRepointsTheAssignments) {
    std::vector<Assignment> assignments = {
        makeOrphanAssignment("a", MessageType::cc, 21, "Cutoff Knob"),
        makeOrphanAssignment("b", MessageType::note, 36, "Pad 1"),
        makeOrphanAssignment("c", MessageType::cc, 21, "Same message, other target")};

    ControllerProfile::Input input;
    input.identifier = "dev-1";
    input.name = "Launchkey";
    const auto profile = recreateProfileFromAssignments(assignments, "orphan", "Launchkey Mini", input);

    EXPECT_FALSE(profile.id.isEmpty());
    EXPECT_NE(profile.id, "orphan");
    EXPECT_EQ(profile.name, "Launchkey Mini");
    EXPECT_EQ(profile.input.identifier, "dev-1");
    ASSERT_EQ(profile.controls.size(), 2u) << "one control per distinct spec";
    EXPECT_EQ(profile.controls[0].name, "Cutoff Knob") << "named from the first assignment with that spec";
    EXPECT_EQ(profile.controls[0].kind, ControlKind::knob);
    EXPECT_EQ(profile.controls[0].encoding, Encoding::relTwos);
    EXPECT_EQ(profile.controls[1].kind, ControlKind::pad);
    EXPECT_NE(profile.controls[0].id, profile.controls[1].id);
    EXPECT_FALSE(profile.controls[0].layout.col == profile.controls[1].layout.col &&
                 profile.controls[0].layout.row == profile.controls[1].layout.row)
        << "auto-laid-out on distinct cells";

    EXPECT_EQ(assignments[0].control.profileId, profile.id);
    EXPECT_EQ(assignments[0].control.controlId, profile.controls[0].id);
    EXPECT_EQ(assignments[2].control.controlId, profile.controls[0].id) << "same spec shares the control";
    EXPECT_EQ(assignments[1].control.controlId, profile.controls[1].id);

    ControllerProfile reloaded;
    EXPECT_TRUE(reloaded.fromVar(profile.toVar())) << "the minted profile is a valid profile document";
}
