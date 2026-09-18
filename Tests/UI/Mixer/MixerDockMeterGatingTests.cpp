// MixerDockMeterGatingTests.cpp -- FRO146 follow-up: a detached mixer window's meters never
// ticked, because MainComponent::timerCallback()'s gate only ever looked at the DOCKED
// MixerDockComponent's own tab/visibility state (`isMixerTabActive() && isVisible()`), which stays
// false the instant the mixer is reparented into its own DetachedPanelWindow (Window placement's
// setMixerTabEnabled(false) forces it false outright) or the docked bottom dock is closed. Covers
// both halves of the fix:
//   1. MixerDockComponent::isMixerShowing() (docked-active-visible OR detached) is the real gate
//      MainComponent::timerCallback() now checks (ORed with mixerPlacement_.isOwnPanelShowing()).
//   2. The detach/redock callback (onEitherHostDetachStateChanged) must NOT rebuild the mixer's
//      columns -- see DetachRedockStateTests.cpp's own header comment on why detach/redock must
//      never disturb a panel's live state; MixerDockComponent::applyTabVisibility()'s
//      `allowMixerRebuild` parameter is what stops that specific caller (unlike a real tab switch)
//      from silently wiping every column's latched clip-readout state back to "-inf".
//
// Drives a real, off-screen MainComponent, same rig style as MixerColumnComponentMeterTests.cpp /
// DetachRedockStateTests.cpp.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "MixerDockActiveTabResetGuard.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>
#include <optional>

namespace {

class MockProviderMDMGT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMDMGT"; }
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

// Same on-disk-settings-leak guard DetachRedockStateTests.cpp uses -- detaching the mixer host for
// real writes "mixerWindowBounds" into the SAME settings file every MainComponent in this process
// (and a real shipped build) reads.
juce::PropertiesFile::Options userSettingsTestOptions() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    return opts;
}

class PersistedKeysGuardMDMGT {
public:
    explicit PersistedKeysGuardMDMGT(juce::StringArray keys) {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        for (const auto& key : keys) {
            std::optional<juce::String> value;
            if (settings != nullptr && settings->containsKey(key))
                value = settings->getValue(key);
            saved_.emplace_back(key, value);
        }
    }

    ~PersistedKeysGuardMDMGT() {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        if (settings == nullptr)
            return;
        for (const auto& [key, value] : saved_) {
            if (value.has_value())
                settings->setValue(key, *value);
            else
                settings->removeValue(key);
        }
        settings->saveIfNeeded();
    }

private:
    std::vector<std::pair<juce::String, std::optional<juce::String>>> saved_;
};

} // namespace

TEST(MixerDockMeterGatingTests, IsMixerShowingIsFalseWhenNeitherDockedActiveNorDetached) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDMGT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getMixerDock();

    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
    dock.setVisible(false); // simulates the bottom dock being closed entirely
    EXPECT_FALSE(dock.isMixerShowing());
}

TEST(MixerDockMeterGatingTests, IsMixerShowingIsTrueWhenDockedOnTheMixerTabAndTheDockIsOpen) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDMGT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getMixerDock();

    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    dock.setVisible(true);
    EXPECT_TRUE(dock.isMixerShowing());
}

TEST(MixerDockMeterGatingTests, IsMixerShowingStaysTrueWhenDetachedEvenWithTheDockedTabOnTimelineAndTheDockClosed) {
    PersistedKeysGuardMDMGT boundsGuard({"mixerWindowBounds"});
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDMGT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getMixerDock();

    // The exact scenario the coordinator hit in the real app: detach the mixer, then close the
    // docked bottom dock entirely (its own tab left on Timeline) to reclaim space -- the detached
    // window is still fully on screen and must keep ticking.
    dock.getMixerHost().setDetached(true);
    ASSERT_TRUE(dock.getMixerHost().isDetached());
    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
    dock.setVisible(false);

    EXPECT_TRUE(dock.isMixerShowing()) << "a detached window doesn't care what the docked tab strip is doing";

    dock.getMixerHost().setDetached(false); // redock, so the fixture's own teardown is a normal dock
}

TEST(MixerDockMeterGatingTests, TimerCallbackRefreshesMetersWhenTheMixerIsDetachedEvenWithTheDockHidden) {
    PersistedKeysGuardMDMGT boundsGuard({"mixerWindowBounds"});
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDMGT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    auto& dock = mc.getMixerDock();
    auto& mixerPanel = dock.getMixerPanel();
    mixerPanel.rebuild();

    dock.getMixerHost().setDetached(true);
    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
    dock.setVisible(false);
    ASSERT_TRUE(dock.isMixerShowing());

    const int before = mixerPanel.getRefreshMetersCallCountForTest();
    mc.timerCallback();
    EXPECT_EQ(mixerPanel.getRefreshMetersCallCountForTest(), before + 1)
        << "the detached window's meters must tick on the same 10 Hz poll";

    dock.getMixerHost().setDetached(false); // redock for a clean teardown
}

TEST(MixerDockMeterGatingTests, TimerCallbackDoesNotRefreshMetersWhenTheMixerIsShowingNowhereAtAll) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDMGT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    auto& dock = mc.getMixerDock();
    auto& mixerPanel = dock.getMixerPanel();
    mixerPanel.rebuild();

    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
    dock.setVisible(false);
    ASSERT_FALSE(dock.isMixerShowing());
    ASSERT_FALSE(mc.getMixerPlacementControllerForTest().isOwnPanelShowing());

    const int before = mixerPanel.getRefreshMetersCallCountForTest();
    mc.timerCallback();
    EXPECT_EQ(mixerPanel.getRefreshMetersCallCountForTest(), before)
        << "no work while the mixer isn't showing anywhere -- docs/layout/rendering.md";
}

TEST(MixerDockMeterGatingTests, DetachingAndRedockingTheMixerPreservesEveryColumnsLatchedReadoutState) {
    PersistedKeysGuardMDMGT boundsGuard({"mixerWindowBounds"});
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMDMGT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& dock = mc.getMixerDock();
    dock.setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    auto& mixerPanel = dock.getMixerPanel();
    mixerPanel.rebuild();

    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    column->getMeterReadoutForTest().updatePeak(6.0f); // above 0 dBFS -- latches clipped
    ASSERT_TRUE(column->getMeterReadoutForTest().isClippedForTest());

    auto& host = dock.getMixerHost();
    host.setDetached(true);
    ASSERT_TRUE(host.isDetached());
    // The SAME column instance, not a rebuilt one -- rebuild() would replace stripColumns_ entirely.
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), column) << "detaching must not rebuild the columns";
    EXPECT_TRUE(column->getMeterReadoutForTest().isClippedForTest())
        << "the latched clip state must survive being reparented into the detached window";

    host.setDetached(false);
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), column) << "redocking must not rebuild the columns either";
    EXPECT_TRUE(column->getMeterReadoutForTest().isClippedForTest())
        << "and survive redocking back -- this was the reported bug (readouts reset to -inf)";
}
