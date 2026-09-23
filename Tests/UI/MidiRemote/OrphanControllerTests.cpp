// OrphanControllerTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#controllers-list-left,
// docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project): the panel side of an
// orphan controller (Re-link / Recreate) and of an orphan node (an assignment whose target no longer
// resolves: "(missing module)" in the warning colour, Forget the only action, never re-bound). The repairs'
// own rules are MidiLearnControllerOrphanTests.cpp; this file drives them through the panel. Suite names
// contain "MidiRemote" per the ship-task --gtest_filter convention.

#include "../Mixer/MixerDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemoteMockProvider.h"
#include "MidiRemotePanelTestFixture.h"
#include "Modules/FilterModule.h"

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

synth::Control makeControl(const juce::String& id, synth::MessageType type, int number, const juce::String& name) {
    synth::Control c;
    c.id = id;
    c.name = name;
    c.message.type = type;
    c.message.channel = 1;
    c.message.number = number;
    return c;
}

class MidiRemoteOrphanPanelTest : public MidiRemotePanelLiveRefreshTest {
protected:
    // Two project assignments on a controller, then the controller is removed from this "machine".
    void makeOrphan() {
        synth::ControllerProfile orphan;
        orphan.id = "orphan-1";
        orphan.name = "Launchkey Mini";
        orphan.input.identifier = "usb-launchkey";
        orphan.controls = {makeControl("k", synth::MessageType::cc, 21, "Knob 1"),
                           makeControl("p", synth::MessageType::note, 36, "Pad 1")};
        ASSERT_TRUE(controller_->addProfile(orphan));
        ASSERT_EQ(controller_->assignControl("orphan-1", "k", PickTarget::parameter(node_->nodeID, "cutoff")),
                  synth::midi::AssignStatus::assigned);
        ASSERT_EQ(controller_->assignControl("orphan-1", "p", PickTarget::parameter(node_->nodeID, "resonance")),
                  synth::midi::AssignStatus::assigned);
        ASSERT_TRUE(controller_->deleteProfile("orphan-1"));
        undo_.clearUndoHistory();
        panel_.setSize(900, 400);
        panel_.rebuildFromProfiles();
    }

    synth::ControllerProfile presentProfile(std::vector<synth::Control> controls) {
        synth::ControllerProfile p;
        p.id = "here";
        p.name = "Nanokontrol";
        p.input.identifier = "usb-nano";
        p.controls = std::move(controls);
        return p;
    }
};

} // namespace

TEST_F(MidiRemoteOrphanPanelTest, SelectingAnOrphanRowShowsItsViewInsteadOfTheInspector) {
    makeOrphan();
    EXPECT_FALSE(panel_.isOrphanSelected());

    panel_.selectForTest("orphan-1", "");

    EXPECT_TRUE(panel_.isOrphanSelected());
    EXPECT_TRUE(panel_.isOrphanViewShownForTest());
    EXPECT_FALSE(panel_.getInspectorForTest().isVisible());
    EXPECT_EQ(panel_.getOrphanViewForTest().getTitleForTest(), "Launchkey Mini (not on this machine)");
    panel_.rebuildFromProfiles();
    EXPECT_TRUE(panel_.isOrphanSelected()) << "a live refresh keeps the orphan selected";

    panel_.selectForTest("", "");
    EXPECT_FALSE(panel_.isOrphanViewShownForTest());
    EXPECT_TRUE(panel_.getInspectorForTest().isVisible());
}

TEST_F(MidiRemoteOrphanPanelTest, RelinkMatchesBySpecSaysWhichStayOrphanedAndUndoRestoresIt) {
    makeOrphan();
    ASSERT_TRUE(controller_->addProfile(presentProfile({makeControl("hk", synth::MessageType::cc, 21, "Fader 1")})));
    panel_.selectForTest("orphan-1", "");

    const auto outcome = panel_.relinkSelectedOrphanTo("here");

    EXPECT_EQ(outcome.matched, 1);
    EXPECT_EQ(outcome.unmatched, 1);
    EXPECT_TRUE(panel_.isOrphanSelected()) << "one assignment is still orphaned, so the view stays";
    EXPECT_EQ(panel_.getOrphanStatusTextForTest(),
              "1 of 2 assignments linked; 1 stay orphaned (that controller has no control with the same message).");

    undo_.undo();
    panel_.rebuildFromProfiles();
    EXPECT_TRUE(panel_.isOrphanSelected());
    for (const auto& a : doc_.assignments)
        EXPECT_EQ(a.control.profileId, "orphan-1");
}

TEST_F(MidiRemoteOrphanPanelTest, RelinkOfEverythingSelectsTheLinkedController) {
    makeOrphan();
    ASSERT_TRUE(controller_->addProfile(presentProfile(
        {makeControl("hk", synth::MessageType::cc, 21, "A"), makeControl("hp", synth::MessageType::note, 36, "B")})));
    panel_.selectForTest("orphan-1", "");

    const auto outcome = panel_.relinkSelectedOrphanTo("here");

    EXPECT_EQ(outcome.unmatched, 0);
    EXPECT_FALSE(panel_.isOrphanSelected());
    EXPECT_FALSE(panel_.isOrphanViewShownForTest());
    EXPECT_NE(panel_.findSurfaceCellForTest("hk"), nullptr) << "the linked controller's surface is shown";
    EXPECT_FALSE(panel_.findSurfaceCellForTest("hk")->getAssignmentLabelForTest().isEmpty());
}

TEST_F(MidiRemoteOrphanPanelTest, RecreateMintsAControllerFromTheAssignmentsAndSelectsIt) {
    makeOrphan();
    panel_.selectForTest("orphan-1", "");
    const auto inputs = panel_.getRecreateInputs();
    ASSERT_EQ(inputs.size(), 1u) << "hosted: the host's MIDI stream";
    EXPECT_EQ(inputs.front().name, "Host MIDI");

    const auto newId = panel_.recreateSelectedOrphanOn(inputs.front());

    ASSERT_FALSE(newId.isEmpty());
    EXPECT_FALSE(panel_.isOrphanSelected());
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    const auto& minted = controller_->getProfiles().front();
    EXPECT_EQ(minted.name, "Launchkey Mini");
    ASSERT_EQ(minted.controls.size(), 2u) << "one control per distinct spec";
    for (const auto& control : minted.controls) {
        const auto* cell = panel_.findSurfaceCellForTest(control.id);
        ASSERT_NE(cell, nullptr) << "the new controller's surface is shown";
        EXPECT_NE(cell->getAssignmentLabelForTest(), "-") << "its controls carry the assignments";
    }

    undo_.undo();
    panel_.rebuildFromProfiles();
    for (const auto& a : doc_.assignments)
        EXPECT_EQ(a.control.profileId, "orphan-1") << "undo restores the orphan";
}

TEST_F(MidiRemoteOrphanPanelTest, RecreateIsOfferedNoInputOnceTheHostStreamIsAlreadyAController) {
    makeOrphan();
    ASSERT_TRUE(controller_->addProfile(presentProfile({})));
    auto hostProfile = presentProfile({});
    hostProfile.id = "host";
    hostProfile.input.identifier = synth::midi::hostSourceKey();
    ASSERT_TRUE(controller_->addProfile(hostProfile));
    panel_.selectForTest("orphan-1", "");

    EXPECT_TRUE(panel_.getRecreateInputs().empty());
    EXPECT_FALSE(panel_.getOrphanViewForTest().getRecreateButtonForTest().isEnabled());
}

// ---- Orphan node -----------------------------------------------------------------------------------------

TEST_F(MidiRemoteOrphanPanelTest, AnOrphanNodeShowsMissingModuleInTheWarningColourAndOffersForgetOnly) {
    synth::ControllerProfile profile;
    profile.id = "p1";
    profile.name = "Launchkey";
    profile.input.identifier = synth::midi::hostSourceKey();
    profile.controls = {makeControl("k", synth::MessageType::cc, 21, "Knob 1")};
    ASSERT_TRUE(controller_->addProfile(profile));
    ASSERT_EQ(controller_->assignControl("p1", "k", PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    const auto assignmentId = doc_.assignments[0].id;
    undo_.clearUndoHistory();

    // The module is deleted the way a delete command does it: the card goes first, then the node,
    // then the reconcile pass -- the assignment is NOT re-bound to anything.
    graphEditor_->detachAllModuleComponents();
    engine_->getGraph().removeNode(node_->nodeID);
    controller_->publishAssignments();
    panel_.setSize(900, 400);
    panel_.selectForTest("p1", "k");

    const auto* cell = panel_.findSurfaceCellForTest("k");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->getAssignmentLabelForTest(), "(missing module)");
    EXPECT_TRUE(cell->isAssignmentWarningForTest());

    auto* drives = dynamic_cast<juce::Label*>(findById(panel_, "assignmentDrivesLabel0"));
    ASSERT_NE(drives, nullptr);
    EXPECT_EQ(drives->getText(), "(missing module)");
    EXPECT_FALSE(findById(panel_, "assignmentTakeoverCombo0")->isEnabled());
    EXPECT_FALSE(findById(panel_, "assignmentRangeMinEditor0")->isEnabled());
    EXPECT_FALSE(findById(panel_, "assignmentInvertToggle0")->isEnabled());
    auto* forget = dynamic_cast<juce::Button*>(findById(panel_, "assignmentForgetButton0"));
    ASSERT_NE(forget, nullptr);
    EXPECT_TRUE(forget->isEnabled());

    // Forget removes it even though its module is gone, and it is undoable.
    forget->onClick();
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(panel_.findSurfaceCellForTest("k")->getAssignmentLabelForTest(), "-");
    undo_.undo();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].id, assignmentId);
}

// End-to-end through a real MainComponent: with the panel open, deleting the module from the canvas leaves
// the surface saying "(missing module)" with no tab switch. (The structural-change hook that schedules the
// refresh is what the real app needed -- the surface stayed stale until a tab switch without it -- but this
// headless path is also refreshed by the graph reconcile, so it checks the end state, not that hook alone.)
TEST(MidiRemoteOrphanMainComponentTest, ADeletedModuleShowsAsMissingWithoutATabSwitch) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("agentsynth-orphan-mc-" + juce::Uuid().toString());
    {
        MainComponent mc(std::make_unique<MidiRemoteMockProvider>(), synth::AIProviderRegistry::createDefault(),
                         synth::ControllerProfileStore(root));
        mc.setSize(1400, 900);
        mc.setVisible(true);
        mc.newPatchForTest();

        auto& editor = mc.getGraphEditor();
        auto node = editor.getAudioEngine().getGraph().addNode(std::make_unique<FilterModule>());
        editor.updateComponents();

        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {makeControl("k", synth::MessageType::cc, 21, "Knob 1")};
        auto& controller = mc.getMidiLearnControllerForTest();
        ASSERT_TRUE(controller.addProfile(profile));
        ASSERT_EQ(controller.assignControl("p1", "k", PickTarget::parameter(node->nodeID, "cutoff")),
                  synth::midi::AssignStatus::assigned);

        mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::MidiRemote); // the panel is open
        auto& panel = mc.getMixerDock().getMidiRemotePanel();
        panel.selectForTest("p1", "k");
        const auto* before = panel.findSurfaceCellForTest("k");
        ASSERT_NE(before, nullptr);
        EXPECT_NE(before->getAssignmentLabelForTest(), "(missing module)");

        editor.requestDeleteModule(node->nodeID); // the card's own delete button
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

        const auto* after = panel.findSurfaceCellForTest("k");
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(after->getAssignmentLabelForTest(), "(missing module)");
        EXPECT_TRUE(after->isAssignmentWarningForTest());
    }
    root.deleteRecursively();
}
