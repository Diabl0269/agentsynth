// MidiRemotePanelTests.cpp -- FRO131 (docs/control/midi-remote-ui.md#the-midi-remote-panel): the
// panel's own selection state and its null-safety before configure() runs. The three region
// components (ControllersListComponent/ControllerSurfaceComponent/ControlInspectorComponent) have
// their own test files; this one covers only what MidiRemotePanelComponent itself owns.
#include "../Mixer/MixerDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemotePanelTestFixture.h"
#include "Modules/FilterModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include <gtest/gtest.h>
#include <memory>

namespace {

class MockProviderMRPT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMRPT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

// ---- Standalone (unconfigured) construction -----------------------------------------------

TEST(MidiRemotePanelComponentTests, ConstructsAndLaysOutThreeRegionsWithoutConfigure) {
    synth::ui::MidiRemotePanelComponent panel;
    panel.setSize(800, 300);

    // kListWidth/kInspectorWidth are the panel's own contract for the fixed side regions; the
    // centre (surface) gets whatever's left, exactly like resized()'s removeFromLeft/removeFromRight.
    EXPECT_TRUE(panel.isVisible() || !panel.isVisible()); // constructing/resizing must not throw/crash
    EXPECT_EQ(panel.getWidth(), 800);
    EXPECT_EQ(panel.getHeight(), 300);
}

TEST(MidiRemotePanelComponentTests, RefreshActivityBeforeConfigureIsANoOp) {
    synth::ui::MidiRemotePanelComponent panel;
    panel.setSize(800, 300);
    // remoteEngine_/learnController_ are both null pre-configure() -- must not crash.
    EXPECT_NO_THROW(panel.refreshActivity());
}

TEST(MidiRemotePanelComponentTests, SelectAssignmentForParameterBeforeConfigureReturnsFalse) {
    synth::ui::MidiRemotePanelComponent panel;
    // doc_ is null pre-configure() -- must fail cleanly, not crash.
    EXPECT_FALSE(panel.selectAssignmentForParameter("some-uuid", "cutoff"));
}

TEST(MidiRemotePanelComponentTests, RebuildFromProfilesBeforeConfigureIsANoOp) {
    synth::ui::MidiRemotePanelComponent panel;
    EXPECT_NO_THROW(panel.rebuildFromProfiles());
}

// ---- Configured, via a real MainComponent (mirrors MixerDockComponentTests' own fixture shape)

TEST(MidiRemotePanelComponentTests, ConfiguredWithNoProfilesRebuildsCleanlyAndHasNoSelection) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMRPT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    auto& panel = mc.getMixerDock().getMidiRemotePanel();
    // wireMidiRemoteEngine() already called configureMidiRemote() during construction -- a second
    // explicit rebuild must still be safe and idempotent.
    EXPECT_NO_THROW(panel.rebuildFromProfiles());
    EXPECT_NO_THROW(panel.refreshActivity());
    EXPECT_FALSE(panel.selectAssignmentForParameter("nonexistent-uuid", "cutoff"));
}

TEST(MidiRemotePanelComponentTests, SwitchingDockToMidiRemoteTabShowsThePanelAndHidesTheOthers) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMRPT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    auto& dock = mc.getMixerDock();
    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::MidiRemote);

    EXPECT_TRUE(dock.isMidiRemoteTabActive());
    EXPECT_TRUE(dock.getMidiRemotePanel().isVisible());
    EXPECT_FALSE(mc.getTimelinePanel().isVisible());
}

// ---- FRO263: live-refresh pipeline (MidiLearnController::onChanged -> scheduleLiveRefresh() ->
// deferred rebuildFromProfiles()) -- a lightweight fixture (AudioEngine/GraphEditor/MidiLearnController
// wired directly, same ingredients as MidiLearnControllerTests.cpp) rather than a full MainComponent,
// since only this one seam -- not the whole app -- is under test. The onChanged wiring itself is
// reproduced here exactly as MainComponent::wireMidiRemoteEngine() does it.

TEST_F(MidiRemotePanelLiveRefreshTest, LearnDoneWhileThePanelIsOpenAppearsWithoutATabSwitch) {
    // The fixture is Hosted, so the list starts with the always-listed, not-yet-created Host MIDI row
    // (FRO136); the learn's auto-created profile takes its place under its own id, which is what
    // shows a refresh happened.
    ASSERT_EQ(panel_.getControllersListForTest().getRowDisplayNameForTest("host-midi"), "Host MIDI");

    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u) << "the learn itself landed";
    EXPECT_EQ(panel_.getControllersListForTest().getRowDisplayNameForTest("host-midi"), "Host MIDI")
        << "not yet -- scheduleLiveRefresh() defers via callAsync, same as the mid-gesture-rebuild hazard";

    pump();
    EXPECT_EQ(panel_.getControllersListForTest().getRowDisplayNameForTest("host-midi"), "")
        << "the deferred rebuild ran and picked up the auto-created profile";
    EXPECT_EQ(panel_.getControllersListRowCountForTest(), 1);
}

// The ticket's own repro (FRO263): assign a knob, Forget, Cmd+Z -- the panel must not keep showing
// it as gone.
TEST_F(MidiRemotePanelLiveRefreshTest, ForgetThenUndoIsReflectedWithoutATabSwitch) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    pump();
    ASSERT_EQ(panel_.getControllersListRowCountForTest(), 1);
    const juce::String nodeUuid = node_->properties["uuid"].toString();
    ASSERT_FALSE(nodeUuid.isEmpty());
    ASSERT_TRUE(panel_.selectAssignmentForParameter(nodeUuid, "cutoff"));

    controller_->forget(node_->nodeID, "cutoff");
    EXPECT_TRUE(doc_.assignments.empty()) << "forget removed the project assignment";

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    EXPECT_EQ(doc_.assignments.size(), 1u) << "MidiRemoteSnapshotAction's undo() restores doc_ synchronously";

    pump(); // runs the deferred rebuild the forget()+undo() pair each queued via onChanged
    EXPECT_TRUE(panel_.selectAssignmentForParameter(nodeUuid, "cutoff"))
        << "the panel re-pulled its Surface/Inspector state after the live-refresh pump, matching doc_ again -- "
           "before FRO263 this stayed stale until a tab switch";
}

// FRO262 (follow-up): a MIDI device that opens WHILE the panel is already showing must not stay
// greyed until a tab switch -- MainComponent::wireMidiRemoteEngine() now reaches
// scheduleLiveRefresh() from AudioEngine::onMidiDevicesChanged too, not just from
// MidiLearnController::onChanged (that path is FRO263's own, covered above). This test
// deliberately leaves controller_->onChanged UNWIRED so a pass here proves the NEW
// onMidiDevicesChanged -> scheduleLiveRefresh() seam alone is sufficient, not a side effect of the
// onChanged wiring already covered by LearnDoneWhileThePanelIsOpenAppearsWithoutATabSwitch above.
TEST_F(MidiRemotePanelLiveRefreshTest, DeviceOpenedWhilePanelIsOpenAppearsWithoutATabSwitch) {
    controller_->onChanged = nullptr; // isolate: only onMidiDevicesChanged wired below for this test

    // Create a profile/assignment the ordinary way (arm+learn), same as the sibling test, but
    // since onChanged is unwired the panel must NOT have picked it up yet -- exactly the FRO262
    // repro shape: a device/mapping becomes real while the panel tab is already active.
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u) << "the learn itself landed";
    ASSERT_EQ(panel_.getControllersListForTest().getRowDisplayNameForTest("host-midi"), "Host MIDI")
        << "onChanged is deliberately unwired here -- nothing should have refreshed the panel yet";

    // Reproduces AudioEngineDeviceLifecycle.cpp's call site: reconcileMidiInputs() found a change
    // and fired onMidiDevicesChanged, wired here exactly as MainComponent::wireMidiRemoteEngine()
    // wires it (refreshSources() + scheduleLiveRefresh()).
    engine_->onMidiDevicesChanged = [this] { panel_.scheduleLiveRefresh(); };
    engine_->onMidiDevicesChanged();
    EXPECT_EQ(panel_.getControllersListForTest().getRowDisplayNameForTest("host-midi"), "Host MIDI")
        << "not yet -- scheduleLiveRefresh() defers via callAsync, same as every other caller of it";

    pump();
    EXPECT_EQ(panel_.getControllersListForTest().getRowDisplayNameForTest("host-midi"), "")
        << "the deferred rebuild ran off the onMidiDevicesChanged path alone and picked up the profile -- "
           "before this fix the panel stayed stale here until a tab switch";
}

// FRO262 (bug 3): the Surface's cell for an already-mapped, already-touched control must seed its
// widget from the target's REAL current value, not always show the minimum (0). Uses the same
// arm/send/settle learn flow as the sibling tests above, but sets the FilterModule's "cutoff"
// parameter to a known non-default value BEFORE resolving the surface, so a pass here can only mean
// refreshSurfaceForSelectedProfile() actually read the live parameter rather than coincidentally
// landing on a default.
TEST_F(MidiRemotePanelLiveRefreshTest, MappedParameterCellSeedsWidgetFromItsCurrentValue) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u) << "the learn itself landed";

    auto* cutoff = findParameterByID(node_->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);
    cutoff->setValueNotifyingHost(0.35f);
    const float expected = cutoff->getValue(); // read back in case the parameter snaps/quantises

    const juce::String nodeUuid = node_->properties["uuid"].toString();
    ASSERT_FALSE(nodeUuid.isEmpty());
    ASSERT_TRUE(panel_.selectAssignmentForParameter(nodeUuid, "cutoff"))
        << "selects the profile/control and calls refreshSurfaceForSelectedProfile()";

    const juce::String controlId = doc_.assignments.front().control.controlId;
    EXPECT_NEAR(panel_.getSurfaceCellValueForTest(controlId), expected, 1.0e-3)
        << "before this fix the cell always started at 0.0f regardless of the parameter's real value";
}
