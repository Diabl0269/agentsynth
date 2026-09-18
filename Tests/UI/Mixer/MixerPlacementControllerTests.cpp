// MixerPlacementControllerTests.cpp -- FRO12 (P9-6, docs/mixer/panel.md): the Mixer placement
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

    // Own panel reparents the Mixer host OUT of the dock and INTO the placement controller itself
    // -- it IS the second strip (MixerPlacementController.h's class comment) -- not to nullptr.
    EXPECT_EQ(mc.getMixerDock().getMixerHost().getParentComponent(), &mc.getMixerPlacementControllerForTest())
        << "reparented out of the dock, into the controller's own second strip";
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
    // juce::PropertiesFile's ChangeBroadcaster notification is posted, not synchronous -- the loop
    // has to turn once before MainComponent::changeListenerCallback sees it. Same idiom as
    // FocusArbitrationZoomGridTests.cpp's NaturalScrollingPreferenceReachesTheTimelineAndTheRoll.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(mc.getMixerDock().getMixerHost().getParentComponent(), &mc.getMixerPlacementControllerForTest())
        << "a live Preferences change must move the Mixer immediately, no restart";
}

// ============================================================================
// Focus-region rebuild on a live placement change (review follow-up on PR #378)
//
// applyPlacementPreference() only moves the Mixer between its three homes -- it does not, on its
// own, re-evaluate the "mixer"/"timeline" registration guards in MainComponent::rebuildFocusRegions
// (MainComponentSetup.cpp), so a placement switch away from Tab used to leave a stale "mixer"
// region pointing at a panel that was no longer showing there. changeListenerCallback now calls
// rebuildFocusRegions() right after applyPlacementPreference() -- this drives that live wiring, not
// just the applier.
// ============================================================================

TEST(MixerPlacementControllerTests, LivePlacementChangeRebuildsTheMixerFocusRegion) {
    MixerPlacementResetGuard guard;
    MainComponent mc(std::make_unique<MockProviderMPCXT>());
    mc.setSize(1400, 900);

    ASSERT_NE(mc.getFocusRegionsForTest().findById("mixer"), nullptr)
        << "Tab placement registers a docked mixer region at launch";

    // Tab -> Window: the host detaches on first reveal, but even before that, Window placement's
    // docked "mixer" region must disappear -- a future DetachedPanelWindow gets its own one-region
    // registry (DetachablePanelHost::setHostedPanelFocusRegion), never MainComponent's.
    mc.getAppPropertiesForTest().getUserSettings()->setValue("mixerPlacement", "window");
    mc.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(mc.getFocusRegionsForTest().findById("mixer"), nullptr)
        << "Window placement has no docked mixer region to Tab-cycle to";
    // Tab-cycling in the main window must never resolve to the now-hidden docked mixer panel.
    for (const auto& region : mc.getFocusRegionsForTest().getRegions())
        EXPECT_NE(region.root, &mc.getMixerDock().getMixerPanel())
            << "region '" << region.id << "' must not point at the hidden docked mixer panel";

    // Window -> Tab: the region reappears live, no restart.
    mc.getAppPropertiesForTest().getUserSettings()->setValue("mixerPlacement", "tab");
    mc.getAppPropertiesForTest().getUserSettings()->saveIfNeeded();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_NE(mc.getFocusRegionsForTest().findById("mixer"), nullptr)
        << "switching back to Tab placement live re-registers the docked mixer region";
}

// ============================================================================
// Own-panel placement's own MainComponent-level mixer focus region (review follow-up on PR #378)
//
// Own panel is still the SAME top-level window as MainComponent (not a DetachedPanelWindow), so
// Tab-cycling there must be able to reach it -- registerMixerFocusRegion's helper hardcodes
// dock.isMixerTabActive(), which is always false once Own-panel disables the dock's Mixer tab, so
// this needs its own direct registration gated on the strip's own visibility instead.
// ============================================================================

TEST(MixerPlacementControllerTests, OwnPanelPlacementRegistersAnOpenMixerFocusRegion) {
    MixerPlacementResetGuard guard;
    writeMixerPlacement("ownPanel");

    MainComponent mc(std::make_unique<MockProviderMPCXT>());
    mc.setSize(1400, 900);

    const auto* region = mc.getFocusRegionsForTest().findById("mixer");
    ASSERT_NE(region, nullptr) << "Own panel placement must register its own mixer focus region";
    EXPECT_EQ(region->root, &mc.getMixerDock().getMixerPanel());
    ASSERT_TRUE(mc.getMixerPlacementControllerForTest().isOwnPanelShowing()) << "visible immediately at launch";
    EXPECT_TRUE(region->isCurrentlyOpen()) << "open while the own-panel strip is showing";

    // Hiding the strip (the toolbar/Cmd+M reveal path) must close the SAME region live -- isOpen is
    // a live callback, not a value baked in at registration time, so no rebuild is needed here.
    mc.performToggleMixerPanel();
    ASSERT_FALSE(mc.getMixerPlacementControllerForTest().isOwnPanelShowing());
    EXPECT_FALSE(region->isCurrentlyOpen()) << "closed once the own-panel strip is hidden";
}
