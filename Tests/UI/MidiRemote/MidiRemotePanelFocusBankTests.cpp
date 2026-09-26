// MidiRemotePanelFocusBankTests.cpp -- FRO141 (docs/control/midi-remote.md#focus-bank,
// docs/control/midi-remote-ui.md#surface-centre): a focus-bank control's surface cell shows
// "Follows selection" with nothing bound, and "Focus: <param>" once a transient binding exists --
// driven through the real panel, reusing MidiRemotePanelTestFixture.h exactly like
// MidiRemotePanelPagesTests.cpp.

#include "MidiRemotePanelTestFixture.h"
#include "Modules/ModuleBase.h"

namespace {

class MidiRemotePanelFocusBankTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        // Transient assignments in these tests are built by hand (never through
        // MidiLearnController::assignControl, which is what normally lazily assigns a node its
        // uuid) -- give node_ one up front so target.parameter.nodeUuid resolves.
        node_->properties.set("uuid", "node-uuid");
        if (auto* mb = dynamic_cast<ModuleBase*>(node_->getProcessor()))
            mb->setNodeUuid("node-uuid");

        synth::Control bank;
        bank.id = "bank-1";
        bank.name = "Bank 1";
        bank.message.type = synth::MessageType::cc;
        bank.message.channel = 1;
        bank.message.number = 20;
        bank.focusBank = true;

        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Controller";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {bank};
        ASSERT_TRUE(controller_->addProfile(profile));
        panel_.selectForTest("p1", "bank-1");
    }
};

} // namespace

TEST_F(MidiRemotePanelFocusBankTest, UnboundFocusBankCellShowsFollowsSelection) {
    auto* cell = panel_.findSurfaceCellForTest("bank-1");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->getAssignmentLabelForTest(), "Follows selection");
    EXPECT_FALSE(cell->isAssignmentWarningForTest());
}

TEST_F(MidiRemotePanelFocusBankTest, BoundFocusBankCellShowsFocusPrefixedLabel) {
    synth::Assignment transient;
    transient.id = "focus:p1:bank-1";
    transient.control.profileId = "p1";
    transient.control.controlId = "bank-1";
    transient.target.kind = synth::Target::Kind::parameter;
    transient.target.parameter.nodeUuid = "node-uuid";
    transient.target.parameter.paramId = "cutoff";
    remoteEngine_.setTransientAssignments({transient});
    remoteEngine_.reconcile(engine_->getGraph());
    panel_.scheduleLiveRefresh();
    pump();

    auto* cell = panel_.findSurfaceCellForTest("bank-1");
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->getAssignmentLabelForTest().startsWith("Focus: "));
    EXPECT_TRUE(cell->getAssignmentLabelForTest().containsIgnoreCase("cutoff"));
}

// An explicit project assignment on the SAME control always wins the display too, mirroring the
// engine's own explicit-wins rule (RemoteEngineReconcile.cpp).
TEST_F(MidiRemotePanelFocusBankTest, ExplicitProjectAssignmentTakesTheCellOverTheTransientOne) {
    ASSERT_EQ(
        controller_->assignControl("p1", "bank-1", synth::midi::PickTarget::parameter(node_->nodeID, "resonance")),
        synth::midi::AssignStatus::assigned);

    synth::Assignment transient;
    transient.id = "focus:p1:bank-1";
    transient.control.profileId = "p1";
    transient.control.controlId = "bank-1";
    transient.target.kind = synth::Target::Kind::parameter;
    transient.target.parameter.nodeUuid = "node-uuid";
    transient.target.parameter.paramId = "cutoff";
    remoteEngine_.setTransientAssignments({transient});
    remoteEngine_.reconcile(engine_->getGraph());
    panel_.scheduleLiveRefresh();
    pump();

    auto* cell = panel_.findSurfaceCellForTest("bank-1");
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->getAssignmentLabelForTest().containsIgnoreCase("resonance"));
    EXPECT_FALSE(cell->getAssignmentLabelForTest().startsWith("Focus: "));
}
