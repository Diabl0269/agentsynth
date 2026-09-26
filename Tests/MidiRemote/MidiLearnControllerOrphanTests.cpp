// MidiLearnControllerOrphanTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#controllers-list-left): the
// orphan-controller repairs Re-link and Recreate on MidiLearnController. A project that references a
// controller this machine lacks is built the honest way: assign against a real profile, then delete the
// profile -- the project's assignments and controller reference stay behind. Suite name contains "MidiRemote".

#include "../UI/MidiRemote/MidiRemotePanelTestFixture.h"

using synth::midi::PickTarget;

namespace {

class MidiRemoteOrphanTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
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
        undo_.clearUndoHistory(); // each test's undo step is then the only one
        ASSERT_EQ(controller_->getProfiles().size(), 0u);
        ASSERT_EQ(doc_.assignments.size(), 2u);
    }

    synth::ControllerProfile presentProfile(const juce::String& id, std::vector<synth::Control> controls) {
        synth::ControllerProfile p;
        p.id = id;
        p.name = "Present " + id;
        p.input.identifier = synth::midi::hostSourceKey();
        p.controls = std::move(controls);
        return p;
    }

    const synth::Assignment* assignmentFor(const juce::String& paramId) const {
        for (const auto& a : doc_.assignments)
            if (a.target.isParameter() && a.target.parameter.paramId == paramId)
                return &a;
        return nullptr;
    }

    bool hasControllerRef(const juce::String& profileId) const {
        for (const auto& ref : doc_.controllers)
            if (ref.profileId == profileId)
                return true;
        return false;
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

TEST_F(MidiRemoteOrphanTest, RelinkMatchesBySpecLeavesTheRestOrphanedAndIsOneUndoStep) {
    // Only the CC 21 knob exists on the present controller; the note-36 pad does not.
    ASSERT_TRUE(
        controller_->addProfile(presentProfile("here", {makeControl("hk", synth::MessageType::cc, 21, "Cutoff")})));
    undo_.clearUndoHistory();

    const auto outcome = controller_->relinkController("orphan-1", "here");

    ASSERT_TRUE(outcome.ok);
    EXPECT_EQ(outcome.matched, 1);
    EXPECT_EQ(outcome.unmatched, 1);
    ASSERT_NE(assignmentFor("cutoff"), nullptr);
    EXPECT_EQ(assignmentFor("cutoff")->control.profileId, "here");
    EXPECT_EQ(assignmentFor("cutoff")->control.controlId, "hk");
    EXPECT_EQ(assignmentFor("resonance")->control.profileId, "orphan-1") << "no match: stays orphaned";
    EXPECT_TRUE(hasControllerRef("orphan-1")) << "still referenced by the unmatched assignment";
    EXPECT_TRUE(hasControllerRef("here"));

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    EXPECT_EQ(assignmentFor("cutoff")->control.profileId, "orphan-1");
    EXPECT_EQ(assignmentFor("cutoff")->control.controlId, "k");
    EXPECT_FALSE(hasControllerRef("here"));
    EXPECT_FALSE(undo_.canUndo()) << "the whole re-link was one step";
}

TEST_F(MidiRemoteOrphanTest, RelinkOfEverythingDropsTheOrphanReference) {
    ASSERT_TRUE(
        controller_->addProfile(presentProfile("here", {makeControl("hk", synth::MessageType::cc, 21, "A"),
                                                        makeControl("hp", synth::MessageType::note, 36, "B")})));
    const auto outcome = controller_->relinkController("orphan-1", "here");
    EXPECT_EQ(outcome.matched, 2);
    EXPECT_EQ(outcome.unmatched, 0);
    EXPECT_FALSE(hasControllerRef("orphan-1"));
}

TEST_F(MidiRemoteOrphanTest, RelinkRefusesAnUnknownTargetAndAControllerThatIsNotAnOrphan) {
    EXPECT_FALSE(controller_->relinkController("orphan-1", "nope").ok);
    ASSERT_TRUE(controller_->addProfile(presentProfile("here", {makeControl("hk", synth::MessageType::cc, 21, "A")})));
    EXPECT_FALSE(controller_->relinkController("here", "here").ok);
    EXPECT_FALSE(undo_.canUndo());
}

TEST_F(MidiRemoteOrphanTest, RecreateMintsOneControlPerSpecFromTheAssignmentsAndUndoRestoresTheOrphan) {
    synth::ControllerProfile::Input device;
    device.identifier = synth::midi::hostSourceKey();
    device.name = "Host MIDI";

    const auto newId = controller_->recreateController("orphan-1", device);

    ASSERT_FALSE(newId.isEmpty());
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    const auto& minted = controller_->getProfiles().front();
    EXPECT_EQ(minted.id, newId);
    EXPECT_EQ(minted.name, "Launchkey Mini") << "named from the project's controller reference";
    EXPECT_EQ(minted.controls.size(), 2u);
    EXPECT_EQ(assignmentFor("cutoff")->control.profileId, newId);
    EXPECT_EQ(assignmentFor("resonance")->control.profileId, newId);
    EXPECT_FALSE(hasControllerRef("orphan-1"));
    EXPECT_TRUE(hasControllerRef(newId));
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles.size(), 1u) << "the new controller is on disk";

    undo_.undo();
    EXPECT_EQ(assignmentFor("cutoff")->control.profileId, "orphan-1") << "undo puts the orphan back";
    EXPECT_TRUE(hasControllerRef("orphan-1"));
    EXPECT_EQ(controller_->getProfiles().size(), 1u)
        << "the minted profile stays (its add is on the controller history, not the project one)";
}

TEST_F(MidiRemoteOrphanTest, RecreateRefusesAControllerThatIsNotAnOrphan) {
    EXPECT_TRUE(controller_->recreateController("no-such-ref", {}).isEmpty());
    EXPECT_TRUE(controller_->getProfiles().empty());
}
