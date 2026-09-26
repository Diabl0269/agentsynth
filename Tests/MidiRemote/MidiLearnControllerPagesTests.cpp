// FRO142 (docs/control/midi-remote.md#pages): MidiLearnController::assignControl tags a new PROJECT
// assignment with whatever page was active on its profile at assignment time, and only replaces
// another assignment on the SAME control/target when that one is on the same page too. Reuses
// MidiRemotePanelTestFixture.h (MidiLearnControllerAssignTests.cpp's own fixture). Suite name
// contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "../UI/MidiRemote/MidiRemotePanelTestFixture.h"

using synth::midi::AssignStatus;
using synth::midi::PickTarget;

namespace {

class MidiRemoteAssignPagesTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.pageCount = 2;
        profile.controls = {makeControl("knob", synth::MessageType::cc, 21, "Knob 1")};
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
};

} // namespace

TEST_F(MidiRemoteAssignPagesTest, AssignmentIsTaggedWithTheActivePageAtAssignmentTime) {
    remoteEngine_.setActivePage("p1", 2);
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].page, 2);
}

TEST_F(MidiRemoteAssignPagesTest, LearningOnPageTwoDoesNotReplaceThePageOneAssignment) {
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].page, 1);

    remoteEngine_.setActivePage("p1", 2);
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "resonance")),
              AssignStatus::assigned);

    ASSERT_EQ(doc_.assignments.size(), 2u) << "page 1's mapping of this control must survive a page-2 assignment";
    const auto& page1 = doc_.assignments[0];
    const auto& page2 = doc_.assignments[1];
    EXPECT_EQ(page1.page, 1);
    EXPECT_EQ(page1.target.parameter.paramId, "cutoff");
    EXPECT_EQ(page2.page, 2);
    EXPECT_EQ(page2.target.parameter.paramId, "resonance");
}

TEST_F(MidiRemoteAssignPagesTest, ReassigningTheSameControlOnTheSamePageStillReplaces) {
    remoteEngine_.setActivePage("p1", 2);
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
              AssignStatus::assigned);
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "resonance")),
              AssignStatus::assigned);

    ASSERT_EQ(doc_.assignments.size(), 1u) << "same control, same page: still one assignment per control";
    EXPECT_EQ(doc_.assignments[0].page, 2);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "resonance");
}

TEST_F(MidiRemoteAssignPagesTest, ActionTargetIgnoresTheActivePageAndStaysGlobal) {
    remoteEngine_.setActivePage("p1", 2);
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::action("togglePlayback")), AssignStatus::assigned);

    EXPECT_TRUE(doc_.assignments.empty()) << "an action is global, never in the project doc";
    ASSERT_EQ(controller_->getProfiles().front().actions.size(), 1u);
}

// FRO142: a page target assigned from the panel/inspector's "Pages" group is GLOBAL too -- same
// branch as action/continuous -- and its own page number/command round-trip through the profile.
TEST_F(MidiRemoteAssignPagesTest, PageTargetIsGlobalAndCarriesItsCommandAndPageNumber) {
    ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::pageTarget(synth::PageCommand::go, 2)),
              AssignStatus::assigned);

    EXPECT_TRUE(doc_.assignments.empty()) << "a page target is global, never in the project doc";
    ASSERT_EQ(controller_->getProfiles().front().actions.size(), 1u);
    const auto& action = controller_->getProfiles().front().actions.front();
    EXPECT_TRUE(action.target.isPage());
    EXPECT_EQ(action.target.page.command, synth::PageCommand::go);
    EXPECT_EQ(action.target.page.page, 2);
}
