// ProfileEditHistoryTests.cpp -- FRO273 (docs/control/midi-remote.md#undo): the controller edit
// history. Every profile mutation MidiLearnController makes records one before/after step, and
// undo/redo applies them back through the same mutation path (saved to disk, republished). The
// project half of a split edit (Delete control) stays on AppUndoManager. Suite name contains
// "MidiRemote".

#include "../UI/MidiRemote/MidiRemotePanelTestFixture.h"

#include "MidiRemote/ControllerProfileStore.h"
#include "MidiRemote/ProfileEditHistory.h"

using synth::midi::PickTarget;
using synth::midi::ProfileEditHistory;

namespace {

synth::Control makeControl(const juce::String& id, int cc, const juce::String& name) {
    synth::Control c;
    c.id = id;
    c.name = name;
    c.message.type = synth::MessageType::cc;
    c.message.channel = 1;
    c.message.number = cc;
    return c;
}

class MidiRemoteProfileHistoryTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {makeControl("k1", 100, "Knob 1")}; // clear of the template's CC 21..28
        ASSERT_TRUE(controller_->addProfile(profile));
    }

    const synth::ControllerProfile* find(const juce::String& id) const {
        for (const auto& p : controller_->getProfiles())
            if (p.id == id)
                return &p;
        return nullptr;
    }
    std::vector<synth::ControllerProfile> onDisk() const {
        return synth::ControllerProfileStore(root_).loadAll().profiles;
    }

    void rename(const juce::String& name) {
        auto updated = *find("p1");
        updated.name = name;
        ASSERT_TRUE(controller_->updateProfile(updated, "Rename controller"));
    }
};

} // namespace

TEST_F(MidiRemoteProfileHistoryTest, ApplyTemplateUndoRestoresThePriorControlSetAndRedoReappliesIt) {
    panel_.selectForTest("p1", {});
    ASSERT_EQ(panel_.applyTemplateToSelectedProfile("template-8-knobs"), 8);
    ASSERT_EQ(find("p1")->controls.size(), 9u);
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Apply template");

    ASSERT_TRUE(controller_->undoProfileEdit());
    ASSERT_EQ(find("p1")->controls.size(), 1u);
    EXPECT_EQ(find("p1")->controls[0].id, "k1");
    ASSERT_EQ(onDisk().size(), 1u);
    EXPECT_EQ(onDisk()[0].controls.size(), 1u) << "the restored state is saved, not only held in memory";
    EXPECT_FALSE(undo_.canUndo()) << "a profile edit never touches the project history";

    EXPECT_EQ(controller_->getRedoProfileEditLabel(), "Apply template");
    ASSERT_TRUE(controller_->redoProfileEdit());
    EXPECT_EQ(find("p1")->controls.size(), 9u);
}

TEST_F(MidiRemoteProfileHistoryTest, AddControllerUndoRemovesTheProfileAndRedoAddsItBack) {
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Add controller");
    ASSERT_TRUE(controller_->undoProfileEdit());
    EXPECT_EQ(find("p1"), nullptr);
    EXPECT_TRUE(onDisk().empty()) << "undoing the add deletes the profile file";
    EXPECT_FALSE(controller_->canUndoProfileEdit());

    ASSERT_TRUE(controller_->redoProfileEdit());
    ASSERT_NE(find("p1"), nullptr);
    EXPECT_EQ(find("p1")->controls.size(), 1u);
}

TEST_F(MidiRemoteProfileHistoryTest, DeleteControllerUndoRestoresItWithItsControls) {
    ASSERT_TRUE(controller_->deleteProfile("p1"));
    ASSERT_EQ(find("p1"), nullptr);
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Delete controller");

    ASSERT_TRUE(controller_->undoProfileEdit());
    ASSERT_NE(find("p1"), nullptr);
    ASSERT_EQ(find("p1")->controls.size(), 1u);
    EXPECT_EQ(find("p1")->controls[0].name, "Knob 1");
    ASSERT_EQ(onDisk().size(), 1u) << "the file is written back";
}

// The split (docs/control/midi-remote.md#undo): the control comes back from the controller history,
// its project assignment from the project history -- each undo restores exactly its own half.
TEST_F(MidiRemoteProfileHistoryTest, DeleteControlSplitsAcrossTheTwoHistories) {
    ASSERT_EQ(controller_->assignControl("p1", "k1", PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 1u);

    ASSERT_TRUE(controller_->deleteControl("p1", "k1"));
    EXPECT_TRUE(find("p1")->controls.empty());
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Delete control");

    ASSERT_TRUE(controller_->undoProfileEdit());
    ASSERT_EQ(find("p1")->controls.size(), 1u) << "the control is back";
    EXPECT_TRUE(doc_.assignments.empty()) << "the project half is not the controller history's to restore";

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    ASSERT_EQ(doc_.assignments.size(), 1u) << "the project history restores the assignment";
    EXPECT_EQ(doc_.assignments[0].control.controlId, "k1");
}

// FRO270: the surface's group delete -- deleteControls() removes BOTH controls (and both project
// assignments) in ONE step on each history, so a single undo on either restores the whole pair.
TEST_F(MidiRemoteProfileHistoryTest, DeleteControlsRemovesAGroupInOneStepPerHistory) {
    auto withSecondControl = *find("p1");
    withSecondControl.controls.push_back(makeControl("k2", 101, "Knob 2"));
    ASSERT_TRUE(controller_->updateProfile(withSecondControl, "Add control"));

    ASSERT_EQ(controller_->assignControl("p1", "k1", PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(controller_->assignControl("p1", "k2", PickTarget::parameter(node_->nodeID, "resonance")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 2u);

    ASSERT_TRUE(controller_->deleteControls("p1", {"k1", "k2"}));
    EXPECT_TRUE(find("p1")->controls.empty());
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Delete controls");

    ASSERT_TRUE(controller_->undoProfileEdit());
    ASSERT_EQ(find("p1")->controls.size(), 2u) << "ONE undo restores both controls";
    EXPECT_TRUE(doc_.assignments.empty()) << "the project half is not the controller history's to restore";

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    ASSERT_EQ(doc_.assignments.size(), 2u) << "ONE project undo restores both assignments";
}

TEST_F(MidiRemoteProfileHistoryTest, ANewEditAfterAnUndoDropsTheRedoTail) {
    rename("A");
    rename("B");
    ASSERT_TRUE(controller_->undoProfileEdit());
    ASSERT_TRUE(controller_->canRedoProfileEdit());

    rename("C");
    EXPECT_FALSE(controller_->canRedoProfileEdit());
    ASSERT_TRUE(controller_->undoProfileEdit());
    EXPECT_EQ(find("p1")->name, "A") << "B is gone for good; C undoes back to A";
}

TEST_F(MidiRemoteProfileHistoryTest, HistoryIsCappedAtOneHundredSteps) {
    for (int i = 0; i < ProfileEditHistory::kMaxSteps + 5; ++i)
        rename("Name " + juce::String(i));
    EXPECT_EQ(controller_->getProfileEditHistory().getNumSteps(), ProfileEditHistory::kMaxSteps);

    int undone = 0;
    while (controller_->undoProfileEdit())
        ++undone;
    EXPECT_EQ(undone, ProfileEditHistory::kMaxSteps);
    // The add and the first five renames fell off the front: the oldest reachable state is the
    // one before rename #5, i.e. the name rename #4 set.
    EXPECT_EQ(find("p1")->name, "Name 4");
}

TEST_F(MidiRemoteProfileHistoryTest, UndoAndRedoDoNotRecordThemselves) {
    rename("A");
    const int steps = controller_->getProfileEditHistory().getNumSteps();
    ASSERT_TRUE(controller_->undoProfileEdit());
    EXPECT_EQ(controller_->getProfileEditHistory().getNumSteps(), steps);
    EXPECT_TRUE(controller_->canRedoProfileEdit()) << "a recorded undo would have dropped the redo tail";
    ASSERT_TRUE(controller_->redoProfileEdit());
    EXPECT_EQ(controller_->getProfileEditHistory().getNumSteps(), steps);
    EXPECT_EQ(find("p1")->name, "A");
}

TEST_F(MidiRemoteProfileHistoryTest, ANoOpEditRecordsNothing) {
    const int steps = controller_->getProfileEditHistory().getNumSteps();
    ASSERT_TRUE(controller_->updateProfile(*find("p1"), "Move control")); // a drag dropped on its own cell
    EXPECT_EQ(controller_->getProfileEditHistory().getNumSteps(), steps);
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Add controller");
}

// updateControl() rewrites the assignment's denormalised copies in place; undoing the control edit
// must put them back too, or the engine keeps decoding with the undone name/encoding.
TEST_F(MidiRemoteProfileHistoryTest, UndoingAControlEditResyncsTheProjectAssignmentCopies) {
    ASSERT_EQ(controller_->assignControl("p1", "k1", PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    auto edited = find("p1")->controls[0];
    edited.name = "Cutoff Knob";
    edited.encoding = synth::Encoding::relTwos;
    ASSERT_TRUE(controller_->updateControl("p1", edited));
    ASSERT_EQ(doc_.assignments[0].specControlName, "Cutoff Knob");

    ASSERT_TRUE(controller_->undoProfileEdit());
    EXPECT_EQ(find("p1")->controls[0].name, "Knob 1");
    EXPECT_EQ(doc_.assignments[0].specControlName, "Knob 1");
    EXPECT_EQ(doc_.assignments[0].specEncoding, synth::Encoding::abs7);
}

TEST_F(MidiRemoteProfileHistoryTest, ForgettingAGlobalActionIsOneUndoableStep) {
    ASSERT_EQ(controller_->assignControl("p1", "k1", PickTarget::action("togglePlayback")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(find("p1")->actions.size(), 1u);

    controller_->forgetAction("togglePlayback");
    ASSERT_TRUE(find("p1")->actions.empty());
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Forget action");
    ASSERT_TRUE(controller_->undoProfileEdit());
    EXPECT_EQ(find("p1")->actions.size(), 1u);
}

// The toolbar cue: shown only while the panel holds focus (the test override stands in for a real
// focus grab) and there is something to undo; the text names the step.
TEST_F(MidiRemoteProfileHistoryTest, ToolbarCueNamesTheNextUndoOnlyWhileThePanelHoldsFocus) {
    rename("A");
    pump();
    EXPECT_TRUE(panel_.getToolbarForTest().getUndoHint().isEmpty()) << "not focused -- no cue";

    panel_.setHoldsUndoFocusForTest(true);
#if JUCE_MAC
    EXPECT_EQ(panel_.getToolbarForTest().getUndoHint(), "Cmd+Z undoes: Rename controller");
#else
    EXPECT_EQ(panel_.getToolbarForTest().getUndoHint(), "Ctrl+Z undoes: Rename controller");
#endif

    // The cue follows the history through the live-refresh path.
    ASSERT_TRUE(controller_->undoProfileEdit());
    pump();
    EXPECT_TRUE(panel_.getToolbarForTest().getUndoHint().endsWith("undoes: Add controller"));
    ASSERT_TRUE(controller_->undoProfileEdit());
    pump();
    EXPECT_TRUE(panel_.getToolbarForTest().getUndoHint().isEmpty()) << "nothing left to undo -- no cue";

    ASSERT_TRUE(controller_->redoProfileEdit());
    pump();
    panel_.setHoldsUndoFocusForTest(false);
    EXPECT_TRUE(panel_.getToolbarForTest().getUndoHint().isEmpty()) << "focus left the panel -- cue hidden";
}
