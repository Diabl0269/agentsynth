// MidiRemotePanelGroupDeleteTests.cpp -- FRO270 (docs/control/midi-remote-ui.md#surface-centre):
// the panel's group delete -- confirms once (showPrompt/promptHook_) with the total assignment
// count, then MidiLearnController::deleteControls() in one call so a single undo on either history
// restores the whole group; and the Inspector's "N controls selected" state. Suite name contains
// "MidiRemote".

#include "MidiRemotePanelTestFixture.h"

using midiremote_test::MidiRemotePanelLiveRefreshTest;
using synth::midi::PickTarget;

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

class MidiRemotePanelGroupDeleteTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {makeControl("k1", 100, "Knob 1"), makeControl("k2", 101, "Knob 2"),
                            makeControl("k3", 102, "Knob 3")};
        ASSERT_TRUE(controller_->addProfile(profile));
        panel_.selectForTest("p1", {});
    }

    const synth::ControllerProfile* find(const juce::String& id) const {
        for (const auto& p : controller_->getProfiles())
            if (p.id == id)
                return &p;
        return nullptr;
    }

    // Always answers OK -- most tests here care about what happens once confirmed, not the confirm
    // itself (that's AutoConfirmsWithTheTotalAssignmentCount below).
    void autoConfirm() {
        panel_.setPromptHookForTest(
            [](const juce::String&, const juce::String&, bool, std::function<void(bool)> done) { done(true); });
    }
};

} // namespace

TEST_F(MidiRemotePanelGroupDeleteTest, ConfirmsOnceWithTheTotalAssignmentCountAcrossTheGroup) {
    ASSERT_EQ(controller_->assignControl("p1", "k1", PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(controller_->assignControl("p1", "k2", PickTarget::parameter(node_->nodeID, "resonance")),
              synth::midi::AssignStatus::assigned);
    // k3 has no assignment.

    int promptCount = 0;
    juce::String seenMessage;
    panel_.setPromptHookForTest(
        [&](const juce::String&, const juce::String& message, bool cancellable, std::function<void(bool)> done) {
            ++promptCount;
            seenMessage = message;
            EXPECT_TRUE(cancellable);
            done(true);
        });

    panel_.selectControlsForTest({"k1", "k2", "k3"});
    panel_.requestDeleteControlsForTest({"k1", "k2", "k3"});

    EXPECT_EQ(promptCount, 1) << "one confirm for the whole group, not one per control";
    EXPECT_TRUE(seenMessage.contains("3")) << "names the control count: " << seenMessage;
    EXPECT_TRUE(seenMessage.contains("2")) << "names the total assignment count: " << seenMessage;
    EXPECT_TRUE(find("p1")->controls.empty());
}

TEST_F(MidiRemotePanelGroupDeleteTest, DecliningTheConfirmLeavesEveryControlInPlace) {
    panel_.setPromptHookForTest(
        [](const juce::String&, const juce::String&, bool, std::function<void(bool)> done) { done(false); });

    panel_.selectControlsForTest({"k1", "k2"});
    panel_.requestDeleteControlsForTest({"k1", "k2"});

    EXPECT_EQ(find("p1")->controls.size(), 3u);
}

TEST_F(MidiRemotePanelGroupDeleteTest, ConfirmedGroupDeleteIsOneUndoStepPerHistory) {
    ASSERT_EQ(controller_->assignControl("p1", "k1", PickTarget::parameter(node_->nodeID, "cutoff")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(controller_->assignControl("p1", "k2", PickTarget::parameter(node_->nodeID, "resonance")),
              synth::midi::AssignStatus::assigned);
    ASSERT_EQ(doc_.assignments.size(), 2u);
    autoConfirm();

    panel_.selectControlsForTest({"k1", "k2"});
    panel_.requestDeleteControlsForTest({"k1", "k2"});

    ASSERT_EQ(find("p1")->controls.size(), 1u) << "k3 (not in the group) survives";
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(controller_->getUndoProfileEditLabel(), "Delete controls");

    ASSERT_TRUE(controller_->undoProfileEdit());
    EXPECT_EQ(find("p1")->controls.size(), 3u) << "one controller-history undo restores both deleted controls";
    EXPECT_TRUE(doc_.assignments.empty()) << "the project half is not the controller history's to restore";

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    EXPECT_EQ(doc_.assignments.size(), 2u) << "one project undo restores both assignments";
}

TEST_F(MidiRemotePanelGroupDeleteTest, InspectorShowsControlsSelectedCountAndNoPerControlFields) {
    panel_.selectControlsForTest({"k1", "k2"});

    const auto& model = panel_.getInspectorForTest().getModelForTest();
    EXPECT_EQ(model.selectedCount, 2);
    EXPECT_FALSE(model.hasControl) << "no single control's own fields are shown for a multi-selection";

    panel_.selectControlsForTest({"k1"});
    EXPECT_TRUE(panel_.getInspectorForTest().getModelForTest().hasControl);
    EXPECT_LT(panel_.getInspectorForTest().getModelForTest().selectedCount, 2);
}

TEST_F(MidiRemotePanelGroupDeleteTest, DeletingOneControlOutOfAGroupSelectionStillConfirmsOnce) {
    // A single-control delete (e.g. the surface's Delete key with only k1 selected) reuses the very
    // same confirm path -- there is one delete implementation, not a single-only fast path.
    int promptCount = 0;
    panel_.setPromptHookForTest([&](const juce::String&, const juce::String&, bool, std::function<void(bool)> done) {
        ++promptCount;
        done(true);
    });

    panel_.selectControlsForTest({"k1"});
    panel_.requestDeleteControlsForTest({"k1"});

    EXPECT_EQ(promptCount, 1);
    EXPECT_EQ(find("p1")->controls.size(), 2u);
}
