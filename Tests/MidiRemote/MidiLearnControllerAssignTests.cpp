// MidiLearnControllerAssignTests.cpp -- FRO135
// (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn): MidiLearnController::assignControl /
// forgetAssignment, the seam every panel-side assignment (the pick-target overlay's click, the action picker's choice)
// ends in. Suite name contains "MidiRemote".

#include "../UI/MidiRemote/MidiRemotePanelTestFixture.h"

#include "ShortcutManager/AppCommands.h"

using synth::midi::AssignStatus;
using synth::midi::PickTarget;

namespace {

class MidiRemoteAssignTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {makeControl("knob", synth::MessageType::cc, 21, "Knob 1"),
                            makeControl("pad", synth::MessageType::note, 36, "Pad 1")};
        ASSERT_TRUE(controller_->addProfile(profile));
    }

    static synth::Control makeControl(const juce::String& id, synth::MessageType type, int number,
                                      const juce::String& name) {
        synth::Control c;
        c.id = id;
        c.name = name;
        c.message.type = type;
        c.message.channel = 1;
        c.message.number = number;
        return c;
    }

    const synth::ControllerProfile& profile() const { return controller_->getProfiles().front(); }
};

} // namespace

TEST_F(MidiRemoteAssignTest, ParameterTargetIsProjectScopeAndUndoable) {
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);

    ASSERT_EQ(doc_.assignments.size(), 1u);
    const auto& a = doc_.assignments[0];
    EXPECT_TRUE(a.target.isParameter());
    EXPECT_EQ(a.target.parameter.paramId, "cutoff");
    EXPECT_EQ(a.control.profileId, "p1");
    EXPECT_EQ(a.control.controlId, "knob");
    EXPECT_EQ(a.specControlName, "Knob 1");
    EXPECT_TRUE(profile().actions.empty()) << "a parameter is never a global assignment";
    ASSERT_EQ(doc_.controllers.size(), 1u);
    EXPECT_EQ(doc_.controllers[0].profileId, "p1");
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "'Knob 1' now drives Cutoff");

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(controller_->getProfiles().size(), 1u) << "the profile stays";
}

TEST_F(MidiRemoteAssignTest, AssigningReplacesWhatTheTargetAndTheControlDroveBefore) {
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);

    ASSERT_EQ(controller_->assignControl("p1", "pad", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 1u) << "the target has one driver";
    EXPECT_EQ(doc_.assignments[0].control.controlId, "pad");

    ASSERT_EQ(controller_->assignControl("p1", "pad", PickTarget::parameter(node_->nodeID, "resonance")),
              AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 1u) << "the control drives one project target";
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "resonance");
}

TEST_F(MidiRemoteAssignTest, ActionTargetIsGlobalAndStoredOnTheProfileNotTheProject) {
    ASSERT_EQ(controller_->assignControl("p1", "pad", PickTarget::action("togglePlayback")), AssignStatus::assigned);

    EXPECT_TRUE(doc_.assignments.empty()) << "an action is global, never in the project doc";
    ASSERT_EQ(profile().actions.size(), 1u);
    EXPECT_EQ(profile().actions[0].target.action.actionId, "togglePlayback");
    EXPECT_EQ(profile().actions[0].control.controlId, "pad");
    const auto stored = synth::ControllerProfileStore(root_).loadAll().profiles;
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].actions.size(), 1u) << "written to disk";
    EXPECT_FALSE(undo_.canUndo()) << "a profile edit is not undoable";

    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::action("togglePlayback")), AssignStatus::assigned);
    ASSERT_EQ(profile().actions.size(), 1u) << "the action has one driver";
    EXPECT_EQ(profile().actions[0].control.controlId, "knob");
}

TEST_F(MidiRemoteAssignTest, NodeCommandTargetIsProjectScope) {
    ASSERT_EQ(controller_->assignControl(
                  "p1", "pad", PickTarget::nodeCommandTarget(node_->nodeID, synth::NodeCommandKind::toggleSolo)),
              AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_TRUE(doc_.assignments[0].target.isNodeCommand());
    EXPECT_TRUE(profile().actions.empty());
}

TEST_F(MidiRemoteAssignTest, RefusesWhatCannotBeMappedAndChangesNothing) {
    EXPECT_EQ(controller_->assignControl("nope", "knob", PickTarget::action("togglePlayback")),
              AssignStatus::unknownControl);
    EXPECT_EQ(controller_->assignControl("p1", "nope", PickTarget::action("togglePlayback")),
              AssignStatus::unknownControl);
    EXPECT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "nope")),
              AssignStatus::unresolvedTarget);
    EXPECT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter({}, "cutoff")),
              AssignStatus::unresolvedTarget);
    // A surface action (no command to dispatch) and an id nobody registered are not invokable.
    ASSERT_EQ(AppCommands::getCommandForAction("timelineToggleLoop"), AppCommands::kNoCommand);
    EXPECT_EQ(controller_->assignControl("p1", "pad", PickTarget::action("timelineToggleLoop")),
              AssignStatus::invalidAction);
    EXPECT_EQ(controller_->assignControl("p1", "pad", PickTarget::action("noSuchAction")), AssignStatus::invalidAction);

    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_TRUE(profile().actions.empty());
    EXPECT_FALSE(undo_.canUndo());
}

TEST_F(MidiRemoteAssignTest, ForgetByIdWorksForAnOrphanedTargetAndIsUndoable) {
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);
    const auto assignmentId = doc_.assignments[0].id;

    engine_->getGraph().removeNode(node_->nodeID); // the module is gone: forget(nodeId, paramId) has nothing to resolve
    ASSERT_TRUE(controller_->forgetAssignment(assignmentId));
    EXPECT_TRUE(doc_.assignments.empty());

    undo_.undo();
    ASSERT_EQ(doc_.assignments.size(), 1u) << "Forget is one undo step";
    EXPECT_EQ(doc_.assignments[0].id, assignmentId);
}

TEST_F(MidiRemoteAssignTest, ForgetByIdRemovesAGlobalActionAssignmentToo) {
    ASSERT_EQ(controller_->assignControl("p1", "pad", PickTarget::action("togglePlayback")), AssignStatus::assigned);
    ASSERT_TRUE(controller_->forgetAssignment(profile().actions[0].id));
    EXPECT_TRUE(profile().actions.empty());
    EXPECT_FALSE(controller_->forgetAssignment("no-such-id"));
}
