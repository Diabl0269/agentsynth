// MixerDockComponentTests.cpp -- FRO11 (P9-5): the dock's tab strip, the Toggle Mixer Panel
// shortcut/command, and active-tab persistence via ApplicationProperties.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "MixerDockActiveTabResetGuard.h"
#include "ShortcutManager/AppCommands.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMDCT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMDCT"; }
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

TEST(MixerDockComponentTests, TabStripSwitchesBetweenTimelineAndMixerWithoutClosingTheDock) {
    // Isolates "bottomDockActiveTab" on the shared on-disk settings file -- see the guard's own
    // comment; this test asserts the "Timeline" DEFAULT, which an earlier test's Mixer-tab switch
    // (persisted to the same file) would otherwise clobber.
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getMixerDock();

    EXPECT_EQ(dock.getActiveTab(), synth::ui::MixerDockComponent::Tab::Timeline);
    EXPECT_TRUE(mc.getTimelinePanel().isVisible());

    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    EXPECT_EQ(dock.getActiveTab(), synth::ui::MixerDockComponent::Tab::Mixer);
    EXPECT_TRUE(dock.isMixerTabActive());
    EXPECT_FALSE(mc.getTimelinePanel().isVisible()) << "switching tabs hides the other panel, not the dock";

    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
    EXPECT_FALSE(dock.isMixerTabActive());
    EXPECT_TRUE(mc.getTimelinePanel().isVisible());
}

TEST(MixerDockComponentTests, ToggleMixerCommandOpensDockOnMixerTabThenClosesOnSecondPress) {
    // Isolates "bottomDockActiveTab" (see the guard's own comment) -- this test switches to the
    // Mixer tab itself and must not leak that into a later test's "Timeline" default assumption.
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    ASSERT_FALSE(mc.isTimelineConfiguredVisible()) << "the dock starts closed";

    mc.performToggleMixerPanel();
    EXPECT_TRUE(mc.isTimelineConfiguredVisible()) << "closed -> open on the Mixer tab";
    EXPECT_TRUE(mc.getMixerDock().isMixerTabActive());

    mc.performToggleMixerPanel();
    EXPECT_FALSE(mc.isTimelineConfiguredVisible()) << "open on Mixer -> close (mirrors toggleTimelineButton)";
}

TEST(MixerDockComponentTests, ToggleMixerPanelActionIdRoundTripsToItsCommand) {
    // The command table row's actionId must resolve back to AppCommands::toggleMixerPanel -- the
    // same generic tripwire EveryActionIdRoundTripsToItsOwnCommand checks for every row, pinned
    // here explicitly for this ticket's own new row.
    EXPECT_EQ(AppCommands::getCommandForAction("toggleMixerPanel"), AppCommands::toggleMixerPanel);
}

TEST(MixerDockComponentTests, ActiveTabPersistsAcrossApplicationPropertiesReload) {
    // Same AppProperties-isolation shape ChannelFlowTestFixture.h's ChannelFlowTest::resetKeys()
    // uses: a dedicated settings key, read/written directly against the SAME on-disk "Agent Synth"
    // settings file MainComponent itself uses, cleared before AND after (the guard's ctor/dtor).
    MixerDockActiveTabResetGuardMDT resetGuard;

    {
        MainComponent mc(std::make_unique<MockProviderMDCT>());
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    }

    {
        MainComponent mc2(std::make_unique<MockProviderMDCT>());
        mc2.setSize(1400, 900);
        mc2.newPatchForTest();
        EXPECT_TRUE(mc2.getMixerDock().isMixerTabActive()) << "the persisted tab must survive a relaunch";
    }
}
