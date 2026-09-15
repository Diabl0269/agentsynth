// MixerPlacementControllerTests.cpp -- FRO12 (P9-6, docs/mixer.md §5.9): the Mixer placement
// preference (Tab beside the Timeline / Own panel / Window), read once at launch and re-applied
// live on every settings-file change. Drives a real, off-screen MainComponent, writing
// "mixerPlacement" into its ApplicationProperties BEFORE construction -- the same "persist first,
// then construct" shape MixerDockComponentTests.cpp's ActiveTabPersistsAcrossApplicationPropertiesReload
// uses for "bottomDockActiveTab".
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMPCXT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPCXT"; }
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

// Isolates "mixerPlacement" + "bottomDockActiveTab" on the shared on-disk settings file every
// MainComponent instance in this process reads -- same shape as MixerDockActiveTabResetGuard.h.
class MixerPlacementResetGuard {
public:
    MixerPlacementResetGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            s->removeValue("mixerPlacement");
            s->removeValue("bottomDockActiveTab");
            s->saveIfNeeded();
        }
    }
    ~MixerPlacementResetGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            s->removeValue("mixerPlacement");
            s->removeValue("bottomDockActiveTab");
            s->saveIfNeeded();
        }
    }
};

void writeMixerPlacement(const juce::String& value) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    auto* s = props.getUserSettings();
    ASSERT_NE(s, nullptr);
    s->setValue("mixerPlacement", value);
    s->saveIfNeeded();
}

} // namespace

TEST(MixerPlacementControllerTests, TabPlacementIsTheDefaultAtLaunch) {
    MixerPlacementResetGuard guard;
    MainComponent mc(std::make_unique<MockProviderMPCXT>());
    mc.setSize(1400, 900);

    EXPECT_EQ(mc.getMixerDock().getMixerHost().getParentComponent(), &mc.getMixerDock())
        << "the mixer host stays parented inside the dock's own tab strip";
    EXPECT_FALSE(mc.getMixerDock().getMixerHost().isDetached());
}

TEST(MixerPlacementControllerTests, OwnPanelPlacementHonouredAtLaunch) {
    MixerPlacementResetGuard guard;
    writeMixerPlacement("ownPanel");

    MainComponent mc(std::make_unique<MockProviderMPCXT>());
    mc.setSize(1400, 900);

    EXPECT_EQ(mc.getMixerDock().getMixerHost().getParentComponent(), nullptr) << "reparented out of the dock";
    EXPECT_FALSE(mc.getMixerDock().getMixerHost().isDetached());
}

TEST(MixerPlacementControllerTests, WindowPlacementNeverEagerlyDetachesAtLaunch) {
    MixerPlacementResetGuard guard;
    writeMixerPlacement("window");

    MainComponent mc(std::make_unique<MockProviderMPCXT>());
    mc.setSize(1400, 900);

    // "opened on first reveal, not eagerly" -- the ticket's own scope statement.
    EXPECT_FALSE(mc.getMixerDock().getMixerHost().isDetached());

    // performToggleMixerPanel() is the reveal path (toolbar / Cmd+M) -- the first call opens it.
    mc.performToggleMixerPanel();
    EXPECT_TRUE(mc.getMixerDock().getMixerHost().isDetached());
}

TEST(MixerPlacementControllerTests, LivePreferenceChangeAppliesWithoutRestart) {
    MixerPlacementResetGuard guard;
    MainComponent mc(std::make_unique<MockProviderMPCXT>());
    mc.setSize(1400, 900);
    ASSERT_NE(mc.getMixerDock().getMixerHost().getParentComponent(), nullptr) << "starts in Tab placement";

    // The same settings-file write path Preferences itself uses -- MainComponent's ChangeListener
    // on appProperties.getUserSettings() is what applies this live (MainComponentCallbacks.cpp).
    mc.getAppPropertiesForTest().getUserSettings()->setValue("mixerPlacement", "ownPanel");
    mc.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();

    EXPECT_EQ(mc.getMixerDock().getMixerHost().getParentComponent(), nullptr)
        << "a live Preferences change must move the Mixer immediately, no restart";
}
