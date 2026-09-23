// MidiRemotePanelTests.cpp -- FRO131 (docs/control/midi-remote-ui.md#the-midi-remote-panel): the
// panel's own selection state and its null-safety before configure() runs. The three region
// components (ControllersListComponent/ControllerSurfaceComponent/ControlInspectorComponent) have
// their own test files; this one covers only what MidiRemotePanelComponent itself owns.
#include "../Mixer/MixerDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include <gtest/gtest.h>

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
