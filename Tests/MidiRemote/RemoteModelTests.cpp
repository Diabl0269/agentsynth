// Round-trip and rejection coverage for every type in Source/MidiRemote/RemoteModel.h
// (docs/control/midi-remote.md#data-model, docs/control/midi-remote.md#persistence-and-the-trust-boundary). Suite name
// deliberately contains "MidiRemote" so it matches the ship-task verification filter (--gtest_filter="*MidiRemote*").
#include "MidiRemote/RemoteModel.h"
#include <gtest/gtest.h>

using namespace synth;

namespace {

MessageSpec makeSpec(MessageType type, int channel, int number) {
    MessageSpec s;
    s.type = type;
    s.channel = channel;
    s.number = number;
    return s;
}

Control makeControl(const juce::String& id, const MessageSpec& spec) {
    Control c;
    c.id = id;
    c.name = "Knob " + id;
    c.kind = ControlKind::knob;
    c.message = spec;
    c.encoding = Encoding::abs7;
    c.buttonMode = ButtonMode::momentary;
    c.layout.col = 1;
    c.layout.row = 2;
    return c;
}

Assignment makeParameterAssignment(const juce::String& id) {
    Assignment a;
    a.id = id;
    a.control.profileId = "profile-1";
    a.control.controlId = "control-1";
    a.spec = makeSpec(MessageType::cc, 1, 21);
    a.specEncoding = Encoding::relTwos;
    a.specButtonMode = ButtonMode::toggle;
    a.specControlName = "Knob 1";
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = "node-uuid-1";
    a.target.parameter.paramId = "cutoff";
    a.target.parameter.paramIndexHint = 3;
    a.takeover = Takeover::scale;
    a.range.min = 0.0;
    a.range.max = 1.0;
    a.enabled = true;
    return a;
}

Assignment makeActionAssignment(const juce::String& id) {
    Assignment a;
    a.id = id;
    a.control.profileId = "profile-1";
    a.control.controlId = "control-2";
    a.spec = makeSpec(MessageType::note, 0, 60);
    a.specEncoding = Encoding::abs7;
    a.specButtonMode = ButtonMode::momentary;
    a.specControlName = "Pad 1";
    a.target.kind = Target::Kind::action;
    a.target.action.actionId = "transport.play";
    a.takeover = Takeover::useDefault;
    a.range.min = 0.0;
    a.range.max = 1.0;
    a.enabled = true;
    return a;
}

} // namespace

// -- MessageSpec ----------------------------------------------------------------------------------

TEST(MidiRemoteModelTest, MessageSpecRoundTrips) {
    const auto spec = makeSpec(MessageType::channelPressure, 0, 5);
    MessageSpec parsed;
    ASSERT_TRUE(MessageSpec::fromVar(spec.toVar(), parsed));
    EXPECT_TRUE(parsed == spec);
}

TEST(MidiRemoteModelTest, MessageSpecRejectsUnknownType) {
    juce::var v = juce::JSON::parse(R"({"type":"bogus","channel":1,"number":21})");
    MessageSpec parsed;
    EXPECT_FALSE(MessageSpec::fromVar(v, parsed));
}

TEST(MidiRemoteModelTest, MessageSpecRejectsOutOfRangeChannel) {
    juce::var v = juce::JSON::parse(R"({"type":"cc","channel":17,"number":21})");
    MessageSpec parsed;
    EXPECT_FALSE(MessageSpec::fromVar(v, parsed));
}

// -- Control ---------------------------------------------------------------------------------------

TEST(MidiRemoteModelTest, ControlRoundTrips) {
    const auto control = makeControl("ctrl-1", makeSpec(MessageType::cc, 1, 21));
    Control parsed;
    ASSERT_TRUE(Control::fromVar(control.toVar(), parsed));
    EXPECT_EQ(parsed.id, control.id);
    EXPECT_EQ(parsed.name, control.name);
    EXPECT_EQ(parsed.kind, control.kind);
    EXPECT_TRUE(parsed.message == control.message);
    EXPECT_EQ(parsed.encoding, control.encoding);
    EXPECT_EQ(parsed.buttonMode, control.buttonMode);
    EXPECT_EQ(parsed.layout.col, control.layout.col);
    EXPECT_EQ(parsed.layout.row, control.layout.row);
}

TEST(MidiRemoteModelTest, ControlRejectsUnknownEncodingString) {
    auto control = makeControl("ctrl-1", makeSpec(MessageType::cc, 1, 21));
    auto v = control.toVar();
    v.getDynamicObject()->setProperty("encoding", "bogus");

    Control parsed;
    EXPECT_FALSE(Control::fromVar(v, parsed));
}

TEST(MidiRemoteModelTest, ControlRejectsEmptyId) {
    auto control = makeControl("", makeSpec(MessageType::cc, 1, 21));
    Control parsed;
    EXPECT_FALSE(Control::fromVar(control.toVar(), parsed));
}

// -- Target ----------------------------------------------------------------------------------------

TEST(MidiRemoteModelTest, TargetParameterVariantRoundTrips) {
    Target target;
    target.kind = Target::Kind::parameter;
    target.parameter.nodeUuid = "node-1";
    target.parameter.paramId = "cutoff";
    target.parameter.paramIndexHint = 7;

    Target parsed;
    ASSERT_TRUE(Target::fromVar(target.toVar(), parsed));
    EXPECT_TRUE(parsed.isParameter());
    EXPECT_FALSE(parsed.isAction());
    EXPECT_EQ(parsed.parameter.nodeUuid, target.parameter.nodeUuid);
    EXPECT_EQ(parsed.parameter.paramId, target.parameter.paramId);
    EXPECT_EQ(parsed.parameter.paramIndexHint, target.parameter.paramIndexHint);
}

TEST(MidiRemoteModelTest, TargetActionVariantRoundTrips) {
    Target target;
    target.kind = Target::Kind::action;
    target.action.actionId = "transport.play";

    Target parsed;
    ASSERT_TRUE(Target::fromVar(target.toVar(), parsed));
    EXPECT_TRUE(parsed.isAction());
    EXPECT_FALSE(parsed.isParameter());
    EXPECT_EQ(parsed.action.actionId, target.action.actionId);
}

TEST(MidiRemoteModelTest, TargetWithBothParameterAndActionIsRejected) {
    juce::var v = juce::JSON::parse(R"({
        "parameter": {"nodeUuid": "n1", "paramId": "p1", "paramIndexHint": -1},
        "action": {"actionId": "a1"}
    })");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelTest, TargetWithNeitherParameterNorActionIsRejected) {
    juce::var v = juce::JSON::parse(R"({})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

// -- Target::NodeCommand (FRO253, docs/control/midi-remote.md#node-command-targets) -----------------

TEST(MidiRemoteModelTest, TargetNodeCommandVariantRoundTrips) {
    Target target;
    target.kind = Target::Kind::nodeCommand;
    target.nodeCommand.nodeUuid = "node-1";
    target.nodeCommand.command = NodeCommandKind::toggleSolo;

    Target parsed;
    ASSERT_TRUE(Target::fromVar(target.toVar(), parsed));
    EXPECT_TRUE(parsed.isNodeCommand());
    EXPECT_FALSE(parsed.isParameter());
    EXPECT_FALSE(parsed.isAction());
    EXPECT_EQ(parsed.nodeCommand.nodeUuid, target.nodeCommand.nodeUuid);
    EXPECT_EQ(parsed.nodeCommand.command, NodeCommandKind::toggleSolo);
}

TEST(MidiRemoteModelTest, TargetNodeCommandSerialisesAsToggleSoloString) {
    Target target;
    target.kind = Target::Kind::nodeCommand;
    target.nodeCommand.nodeUuid = "node-1";
    target.nodeCommand.command = NodeCommandKind::toggleSolo;

    const auto v = target.toVar();
    EXPECT_EQ(v.getDynamicObject()->getProperty("nodeCommand").getDynamicObject()->getProperty("command").toString(),
              "toggleSolo");
}

TEST(MidiRemoteModelTest, TargetWithTwoOfThreeKindsIsRejected) {
    juce::var v = juce::JSON::parse(R"({
        "parameter": {"nodeUuid": "n1", "paramId": "p1", "paramIndexHint": -1},
        "nodeCommand": {"nodeUuid": "n1", "command": "toggleSolo"}
    })");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelTest, TargetNodeCommandRejectsUnknownCommandString) {
    juce::var v = juce::JSON::parse(R"({"nodeCommand": {"nodeUuid": "n1", "command": "toggleMute"}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

TEST(MidiRemoteModelTest, TargetNodeCommandRejectsEmptyNodeUuid) {
    juce::var v = juce::JSON::parse(R"({"nodeCommand": {"nodeUuid": "", "command": "toggleSolo"}})");
    Target parsed;
    EXPECT_FALSE(Target::fromVar(v, parsed));
}

// -- Assignment -------------------------------------------------------------------------------------

TEST(MidiRemoteModelTest, AssignmentRoundTrips) {
    const auto assignment = makeParameterAssignment("assign-1");
    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(assignment.toVar(), parsed));
    EXPECT_EQ(parsed.id, assignment.id);
    EXPECT_EQ(parsed.control.profileId, assignment.control.profileId);
    EXPECT_EQ(parsed.control.controlId, assignment.control.controlId);
    EXPECT_TRUE(parsed.spec == assignment.spec);
    EXPECT_EQ(parsed.specEncoding, assignment.specEncoding);
    EXPECT_EQ(parsed.specButtonMode, assignment.specButtonMode);
    EXPECT_EQ(parsed.specControlName, assignment.specControlName);
    EXPECT_TRUE(parsed.target.isParameter());
    EXPECT_EQ(parsed.target.parameter.nodeUuid, assignment.target.parameter.nodeUuid);
    EXPECT_EQ(parsed.target.parameter.paramId, assignment.target.parameter.paramId);
    EXPECT_EQ(parsed.takeover, assignment.takeover);
    EXPECT_DOUBLE_EQ(parsed.range.min, assignment.range.min);
    EXPECT_DOUBLE_EQ(parsed.range.max, assignment.range.max);
    EXPECT_EQ(parsed.enabled, assignment.enabled);
}

TEST(MidiRemoteModelTest, ActionAssignmentRoundTrips) {
    const auto assignment = makeActionAssignment("assign-action-1");
    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(assignment.toVar(), parsed));
    EXPECT_TRUE(parsed.target.isAction());
    EXPECT_EQ(parsed.target.action.actionId, assignment.target.action.actionId);
}

TEST(MidiRemoteModelTest, AssignmentDenormalisedSpecRoundTripsWithoutAnyProfileReference) {
    // The whole point of §4.1's denormalised copy: nothing here ever looks up control.profileId
    // against a live ControllerProfile, so it must round-trip identically even though no profile
    // with this id is ever loaded in this test.
    auto assignment = makeParameterAssignment("assign-orphan");
    assignment.control.profileId = "profile-that-does-not-exist-anywhere";
    assignment.control.controlId = "control-that-does-not-exist-either";

    Assignment parsed;
    ASSERT_TRUE(Assignment::fromVar(assignment.toVar(), parsed));
    EXPECT_EQ(parsed.control.profileId, assignment.control.profileId);
    EXPECT_EQ(parsed.control.controlId, assignment.control.controlId);
    EXPECT_TRUE(parsed.spec == assignment.spec);
    EXPECT_EQ(parsed.specEncoding, assignment.specEncoding);
    EXPECT_EQ(parsed.specButtonMode, assignment.specButtonMode);
    EXPECT_EQ(parsed.specControlName, assignment.specControlName);
}

TEST(MidiRemoteModelTest, AssignmentRejectsInvalidTarget) {
    auto assignment = makeParameterAssignment("assign-bad-target");
    auto v = assignment.toVar();
    // Both "parameter" and "action" present on the target — Target::fromVar's own rule.
    auto* targetObj = v.getDynamicObject()->getProperty("target").getDynamicObject();
    ASSERT_NE(targetObj, nullptr);
    auto* actionObj = new juce::DynamicObject();
    actionObj->setProperty("actionId", "transport.play");
    targetObj->setProperty("action", juce::var(actionObj));

    Assignment parsed;
    EXPECT_FALSE(Assignment::fromVar(v, parsed));
}

// -- ControllerProfile --------------------------------------------------------------------------------

TEST(MidiRemoteModelTest, ControllerProfileRoundTrips) {
    ControllerProfile profile;
    profile.id = "profile-1";
    profile.name = "Launchkey Mini MK3";
    profile.input.identifier = "launchkey-in";
    profile.input.name = "Launchkey Mini MK3";
    profile.hasOutput = false;
    profile.passMapped = true;
    profile.controls.push_back(makeControl("ctrl-1", makeSpec(MessageType::cc, 1, 21)));
    profile.controls.push_back(makeControl("ctrl-2", makeSpec(MessageType::cc, 1, 22)));
    profile.actions.push_back(makeActionAssignment("action-1"));
    profile.version = 1;

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(profile.toVar()));
    EXPECT_EQ(parsed.id, profile.id);
    EXPECT_EQ(parsed.name, profile.name);
    EXPECT_EQ(parsed.input.identifier, profile.input.identifier);
    EXPECT_EQ(parsed.input.name, profile.input.name);
    EXPECT_FALSE(parsed.hasOutput);
    EXPECT_EQ(parsed.passMapped, profile.passMapped);
    ASSERT_EQ(parsed.controls.size(), profile.controls.size());
    EXPECT_EQ(parsed.controls[0].id, profile.controls[0].id);
    EXPECT_EQ(parsed.controls[1].id, profile.controls[1].id);
    ASSERT_EQ(parsed.actions.size(), profile.actions.size());
    EXPECT_EQ(parsed.actions[0].id, profile.actions[0].id);
    EXPECT_EQ(parsed.version, 1);
}

TEST(MidiRemoteModelTest, ControllerProfileWithOutputRoundTrips) {
    ControllerProfile profile;
    profile.id = "profile-2";
    profile.name = "Some Controller";
    profile.input.identifier = "in-id";
    profile.input.name = "in-name";
    profile.hasOutput = true;
    profile.output.identifier = "out-id";
    profile.output.name = "out-name";

    ControllerProfile parsed;
    ASSERT_TRUE(parsed.fromVar(profile.toVar()));
    EXPECT_TRUE(parsed.hasOutput);
    EXPECT_EQ(parsed.output.identifier, profile.output.identifier);
    EXPECT_EQ(parsed.output.name, profile.output.name);
}

TEST(MidiRemoteModelTest, ControllerProfileRejectsTwoControlsSharingAMessageSpec) {
    ControllerProfile profile;
    profile.id = "profile-conflict";
    profile.name = "Conflict";
    profile.input.identifier = "in";
    profile.input.name = "in";
    const auto spec = makeSpec(MessageType::cc, 1, 21);
    profile.controls.push_back(makeControl("ctrl-a", spec));
    profile.controls.push_back(makeControl("ctrl-b", spec)); // same MessageSpec key

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(profile.toVar()));
}

TEST(MidiRemoteModelTest, ControllerProfileRejectsUnknownVersion) {
    ControllerProfile profile;
    profile.id = "profile-v2";
    profile.name = "Future";
    profile.input.identifier = "in";
    profile.input.name = "in";
    auto v = profile.toVar();
    v.getDynamicObject()->setProperty("version", 2);

    ControllerProfile parsed;
    parsed.id = "sentinel-untouched"; // proves fromVar never partially applies on rejection
    EXPECT_FALSE(parsed.fromVar(v));
    EXPECT_EQ(parsed.id, "sentinel-untouched");
}

TEST(MidiRemoteModelTest, ControllerProfileRejectsMissingVersion) {
    ControllerProfile profile;
    profile.id = "profile-no-version";
    profile.name = "NoVersion";
    profile.input.identifier = "in";
    profile.input.name = "in";
    auto v = profile.toVar();
    v.getDynamicObject()->removeProperty("version");

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

TEST(MidiRemoteModelTest, ControllerProfileRejectsUnknownEnumString) {
    ControllerProfile profile;
    profile.id = "profile-bad-enum";
    profile.name = "Bad";
    profile.input.identifier = "in";
    profile.input.name = "in";
    profile.controls.push_back(makeControl("ctrl-1", makeSpec(MessageType::cc, 1, 21)));

    auto v = profile.toVar();
    auto* controlsArr = v.getDynamicObject()->getProperty("controls").getArray();
    ASSERT_NE(controlsArr, nullptr);
    ASSERT_FALSE(controlsArr->isEmpty());
    controlsArr->getReference(0).getDynamicObject()->setProperty("encoding", "bogus");

    ControllerProfile parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

// -- MidiRemoteProjectDoc ------------------------------------------------------------------------------

TEST(MidiRemoteModelTest, MidiRemoteProjectDocRoundTrips) {
    MidiRemoteProjectDoc doc;
    doc.version = 1;
    doc.assignments.push_back(makeParameterAssignment("assign-1"));
    doc.assignments.push_back(makeParameterAssignment("assign-2"));
    MidiRemoteProjectDoc::ControllerRef ref;
    ref.profileId = "profile-1";
    ref.name = "Launchkey Mini MK3";
    doc.controllers.push_back(ref);

    MidiRemoteProjectDoc parsed;
    ASSERT_TRUE(parsed.fromVar(doc.toVar()));
    EXPECT_EQ(parsed.version, 1);
    ASSERT_EQ(parsed.assignments.size(), doc.assignments.size());
    EXPECT_EQ(parsed.assignments[0].id, doc.assignments[0].id);
    EXPECT_EQ(parsed.assignments[1].id, doc.assignments[1].id);
    ASSERT_EQ(parsed.controllers.size(), 1u);
    EXPECT_EQ(parsed.controllers[0].profileId, ref.profileId);
    EXPECT_EQ(parsed.controllers[0].name, ref.name);
}

TEST(MidiRemoteModelTest, MidiRemoteProjectDocWithNodeCommandAssignmentRoundTrips) {
    Assignment a = makeParameterAssignment("assign-solo");
    a.target.kind = Target::Kind::nodeCommand;
    a.target.parameter = {}; // FRO253: the previous kind's payload must not survive the switch
    a.target.nodeCommand.nodeUuid = "strip-node-uuid";
    a.target.nodeCommand.command = NodeCommandKind::toggleSolo;

    MidiRemoteProjectDoc doc;
    doc.version = 1;
    doc.assignments.push_back(a);

    MidiRemoteProjectDoc parsed;
    ASSERT_TRUE(parsed.fromVar(doc.toVar()));
    ASSERT_EQ(parsed.assignments.size(), 1u);
    EXPECT_TRUE(parsed.assignments[0].target.isNodeCommand());
    EXPECT_EQ(parsed.assignments[0].target.nodeCommand.nodeUuid, "strip-node-uuid");
    EXPECT_EQ(parsed.assignments[0].target.nodeCommand.command, NodeCommandKind::toggleSolo);
}

TEST(MidiRemoteModelTest, MidiRemoteProjectDocEmptyDocRoundTrips) {
    MidiRemoteProjectDoc doc; // version 1, no assignments, no controllers
    MidiRemoteProjectDoc parsed;
    ASSERT_TRUE(parsed.fromVar(doc.toVar()));
    EXPECT_EQ(parsed.version, 1);
    EXPECT_TRUE(parsed.assignments.empty());
    EXPECT_TRUE(parsed.controllers.empty());
}

TEST(MidiRemoteModelTest, MidiRemoteProjectDocRejectsUnknownVersion) {
    MidiRemoteProjectDoc doc;
    auto v = doc.toVar();
    v.getDynamicObject()->setProperty("version", 2);

    MidiRemoteProjectDoc parsed;
    parsed.version = -1; // sentinel, proves fromVar never partially applies on rejection
    EXPECT_FALSE(parsed.fromVar(v));
    EXPECT_EQ(parsed.version, -1);
}

TEST(MidiRemoteModelTest, MidiRemoteProjectDocRejectsMissingVersion) {
    MidiRemoteProjectDoc doc;
    auto v = doc.toVar();
    v.getDynamicObject()->removeProperty("version");

    MidiRemoteProjectDoc parsed;
    EXPECT_FALSE(parsed.fromVar(v));
}

// -- 14-bit and NRPN encodings (FRO140) -----------------------------------------------------------------

TEST(MidiRemoteModelTest, NrpnAndPairedControlsRoundTrip) {
    auto nrpn = makeControl("ctrl-nrpn", makeSpec(MessageType::nrpn, 1, MessageSpec::maxNumber(MessageType::nrpn)));
    nrpn.encoding = Encoding::abs14;
    auto lsbFirst = makeControl("ctrl-lsb", makeSpec(MessageType::cc, 1, 21));
    lsbFirst.encoding = Encoding::abs14LsbFirst;

    Control parsedNrpn;
    ASSERT_TRUE(Control::fromVar(nrpn.toVar(), parsedNrpn));
    EXPECT_EQ(parsedNrpn.message.type, MessageType::nrpn);
    EXPECT_EQ(parsedNrpn.message.number, 16383);
    EXPECT_EQ(parsedNrpn.encoding, Encoding::abs14);

    Control parsedLsb;
    ASSERT_TRUE(Control::fromVar(lsbFirst.toVar(), parsedLsb));
    EXPECT_EQ(parsedLsb.message.number, 21);
    EXPECT_EQ(parsedLsb.encoding, Encoding::abs14LsbFirst);
}

TEST(MidiRemoteModelTest, AssignmentCarryingNrpnAndPairedEncodingsRoundTrips) {
    auto nrpn = makeParameterAssignment("assign-nrpn");
    nrpn.spec = makeSpec(MessageType::nrpn, 0, 16383);
    nrpn.specEncoding = Encoding::abs14;
    auto paired = makeParameterAssignment("assign-paired");
    paired.spec = makeSpec(MessageType::cc, 1, 21);
    paired.specEncoding = Encoding::abs14LsbFirst;

    Assignment parsedNrpn;
    ASSERT_TRUE(Assignment::fromVar(nrpn.toVar(), parsedNrpn));
    EXPECT_TRUE(parsedNrpn.spec == nrpn.spec);
    EXPECT_EQ(parsedNrpn.specEncoding, Encoding::abs14);

    Assignment parsedPaired;
    ASSERT_TRUE(Assignment::fromVar(paired.toVar(), parsedPaired));
    EXPECT_TRUE(parsedPaired.spec == paired.spec);
    EXPECT_EQ(parsedPaired.specEncoding, Encoding::abs14LsbFirst);
}

TEST(MidiRemoteModelTest, MessageSpecRejectsNrpnAddressAboveFourteenBits) {
    MessageSpec parsed;
    EXPECT_FALSE(MessageSpec::fromVar(juce::JSON::parse(R"({"type":"nrpn","channel":1,"number":16384})"), parsed));
    EXPECT_TRUE(MessageSpec::fromVar(juce::JSON::parse(R"({"type":"nrpn","channel":1,"number":16383})"), parsed));
}

TEST(MidiRemoteModelTest, MessageSpecStillRejectsCcNumberAbove127) {
    // The widened range is nrpn-only; every other type keeps its 7-bit cap.
    MessageSpec parsed;
    EXPECT_FALSE(MessageSpec::fromVar(juce::JSON::parse(R"({"type":"cc","channel":1,"number":128})"), parsed));
}

TEST(MidiRemoteModelTest, ControlRejectsPairedEncodingOnCcWithoutLsbPartner) {
    auto control = makeControl("ctrl-1", makeSpec(MessageType::cc, 1, 40)); // 40 + 32 > 63
    control.encoding = Encoding::abs14;

    Control parsed;
    parsed.id = "sentinel-untouched"; // all-or-nothing: a rejected control leaves `out` alone
    EXPECT_FALSE(Control::fromVar(control.toVar(), parsed));
    EXPECT_EQ(parsed.id, "sentinel-untouched");
}

TEST(MidiRemoteModelTest, ControlRejectsRelativeEncodingOnNrpn) {
    auto control = makeControl("ctrl-1", makeSpec(MessageType::nrpn, 1, 1024));
    control.encoding = Encoding::relTwos;

    Control parsed;
    EXPECT_FALSE(Control::fromVar(control.toVar(), parsed));
}

TEST(MidiRemoteModelTest, ControlStillRejectsUnknownEncodingStringOnNrpn) {
    auto control = makeControl("ctrl-1", makeSpec(MessageType::nrpn, 1, 1024));
    auto v = control.toVar();
    v.getDynamicObject()->setProperty("encoding", "abs15");

    Control parsed;
    EXPECT_FALSE(Control::fromVar(v, parsed));
}

TEST(MidiRemoteModelTest, AssignmentRejectsEncodingInvalidForItsSpec) {
    auto pairedOnHighCc = makeParameterAssignment("assign-bad-1");
    pairedOnHighCc.spec = makeSpec(MessageType::cc, 1, 40);
    pairedOnHighCc.specEncoding = Encoding::abs14;
    auto relativeNrpn = makeParameterAssignment("assign-bad-2");
    relativeNrpn.spec = makeSpec(MessageType::nrpn, 1, 1024);
    relativeNrpn.specEncoding = Encoding::relBinOffset;

    Assignment parsed;
    EXPECT_FALSE(Assignment::fromVar(pairedOnHighCc.toVar(), parsed));
    EXPECT_FALSE(Assignment::fromVar(relativeNrpn.toVar(), parsed));
}

TEST(MidiRemoteModelTest, EncodingValidForSpecBoundaries) {
    // The highest MSB with an LSB partner (31 + 32 = 63) is fine; CC 32 has none.
    EXPECT_TRUE(encodingValidForSpec(makeSpec(MessageType::cc, 1, 31), Encoding::abs14));
    EXPECT_FALSE(encodingValidForSpec(makeSpec(MessageType::cc, 1, 32), Encoding::abs14LsbFirst));
    EXPECT_TRUE(encodingValidForSpec(makeSpec(MessageType::nrpn, 1, 5), Encoding::abs7));
    EXPECT_FALSE(encodingValidForSpec(makeSpec(MessageType::note, 1, 60), Encoding::abs14));
    EXPECT_TRUE(encodingValidForSpec(makeSpec(MessageType::note, 1, 60), Encoding::relTwos));
}
