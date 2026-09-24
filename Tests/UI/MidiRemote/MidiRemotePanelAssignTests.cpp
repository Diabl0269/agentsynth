// MidiRemotePanelAssignTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// the panel side of "Assign...": the toolbar and inspector entry points, the pick-target hand-off and the
// action choice reaching the surface and the inspector. The pick-target overlay itself is
// PickTargetOverlayTests.cpp; the action list is ActionPickerTests.cpp. Suite names contain "MidiRemote".

#include "MidiRemotePanelTestFixture.h"

#include "ShortcutManager/ShortcutManager.h"

using synth::midi::PickTarget;

namespace {

juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

class MidiRemotePanelAssignTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        panel_.setSize(900, 400);
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        synth::Control pad;
        pad.id = "pad";
        pad.name = "Pad 1";
        pad.kind = synth::ControlKind::pad;
        pad.message.type = synth::MessageType::note;
        pad.message.channel = 1;
        pad.message.number = 36;
        profile.controls = {pad};
        ASSERT_TRUE(controller_->addProfile(profile));
        panel_.rebuildFromProfiles();
        undo_.clearUndoHistory();
    }

    bool assignButtonEnabled() {
        auto* button = dynamic_cast<juce::Button*>(findById(panel_, "assignButton"));
        return button != nullptr && button->isEnabled();
    }
};

} // namespace

TEST_F(MidiRemotePanelAssignTest, AssignNeedsASelectedControl) {
    EXPECT_FALSE(assignButtonEnabled());
    panel_.selectForTest("p1", "");
    EXPECT_FALSE(assignButtonEnabled()) << "a controller alone is not enough";
    panel_.selectForTest("p1", "pad");
    EXPECT_TRUE(assignButtonEnabled());
    panel_.selectForTest("", "");
    EXPECT_FALSE(assignButtonEnabled());
}

TEST_F(MidiRemotePanelAssignTest, LearnTargetInTheInspectorIsLiveAndSharesTheAssignEntryPoint) {
    panel_.selectForTest("p1", "pad");
    auto* learnTarget = dynamic_cast<juce::TextButton*>(findById(panel_, "learnTargetButton"));
    ASSERT_NE(learnTarget, nullptr);
    EXPECT_TRUE(learnTarget->isVisible());
    EXPECT_TRUE(learnTarget->isEnabled());

    juce::Component* anchor = nullptr;
    panel_.getInspectorForTest().onLearnTargetRequested = [&](juce::Component& a) { anchor = &a; };
    learnTarget->onClick();
    EXPECT_EQ(anchor, learnTarget);

    panel_.selectForTest("p1", "");
    EXPECT_FALSE(learnTarget->isVisible()) << "no control selected: nothing to assign";
}

TEST_F(MidiRemotePanelAssignTest, PickAControlNeedsASelectionAndHandsOffToTheOverlay) {
    juce::Component host;
    host.setSize(400, 300);
    controller_->setPickOverlayHost(&host);

    EXPECT_FALSE(panel_.startPickTargetForSelectedControl()) << "nothing selected";
    EXPECT_FALSE(controller_->isPickingTarget());

    panel_.selectForTest("p1", "pad");
    EXPECT_TRUE(panel_.startPickTargetForSelectedControl());
    EXPECT_TRUE(controller_->isPickingTarget());
    controller_->cancelPickTarget();
    controller_->setPickOverlayHost(nullptr);
}

TEST_F(MidiRemotePanelAssignTest, ChoosingAnActionAssignsGloballyAndShowsOnTheSurfaceAndInTheInspector) {
    panel_.selectForTest("p1", "pad");
    ASSERT_TRUE(panel_.assignSelectedControlToAction("transportTogglePlayStop"));

    ASSERT_EQ(controller_->getProfiles().front().actions.size(), 1u);
    EXPECT_TRUE(doc_.assignments.empty());
    const auto* cell = panel_.findSurfaceCellForTest("pad");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->getAssignmentLabelForTest(), ShortcutManager::getActionDescription("transportTogglePlayStop"));
    auto* drives = dynamic_cast<juce::Label*>(findById(panel_, "assignmentDrivesLabel0"));
    ASSERT_NE(drives, nullptr);
    EXPECT_EQ(drives->getText(), ShortcutManager::getActionDescription("transportTogglePlayStop"));
}

// FRO236 (docs/control/midi-remote.md#continuous-targets): mirrors
// ChoosingAnActionAssignsGloballyAndShowsOnTheSurfaceAndInTheInspector above.
TEST_F(MidiRemotePanelAssignTest, ChoosingAContinuousTargetAssignsGloballyAndShowsOnTheSurfaceAndInTheInspector) {
    panel_.selectForTest("p1", "pad");
    ASSERT_TRUE(panel_.assignSelectedControlToContinuous(synth::ContinuousTargetKind::masterVolume));

    ASSERT_EQ(controller_->getProfiles().front().actions.size(), 1u);
    EXPECT_TRUE(doc_.assignments.empty());
    const auto* cell = panel_.findSurfaceCellForTest("pad");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->getAssignmentLabelForTest(), "Master Volume");
    auto* drives = dynamic_cast<juce::Label*>(findById(panel_, "assignmentDrivesLabel0"));
    ASSERT_NE(drives, nullptr);
    EXPECT_EQ(drives->getText(), "Master Volume");
}

TEST_F(MidiRemotePanelAssignTest, AnActionWithNoCommandIsRefusedAndChangesNothing) {
    panel_.selectForTest("p1", "pad");
    EXPECT_FALSE(panel_.assignSelectedControlToAction("timelineToggleLoop"));
    EXPECT_TRUE(controller_->getProfiles().front().actions.empty());
    panel_.selectForTest("", "");
    EXPECT_FALSE(panel_.assignSelectedControlToAction("transportTogglePlayStop")) << "nothing selected";
}
