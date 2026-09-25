// MixerFocusRegionTests.cpp -- FRO18: the "mixer" focus region's open predicate, and the fix to
// "timeline"'s own (MainComponent::registerFocusRegions, plan (a)) now that both share one dock
// and only one tab is ever on screen at a time. Isolates BOTH persisted dock keys the shared
// on-disk settings file carries ("bottomDockActiveTab" via the existing guard, "bottomDockVisible"
// by reading the current state and only toggling when it disagrees with what each test needs) --
// see BottomDockActiveTabResetGuard.h's own comment for why an unguarded persisted key leaks
// between test instances in the same binary run.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Mixer/MixerPanelComponent/MixerFocusRegion.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMFRT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMFRT"; }
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

void ensureDockOpen(MainComponent& mc) {
    if (!mc.isBottomDockConfiguredVisible())
        mc.simulateToggleTimelineClick();
}

void ensureDockClosed(MainComponent& mc) {
    if (mc.isBottomDockConfiguredVisible())
        mc.simulateToggleTimelineClick();
}

} // namespace

TEST(MixerFocusRegionTest, MixerRegionOpenOnlyWhenDockOpenAndMixerTabActive) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);

    auto* mixerRegion = mc.getFocusRegionsForTest().findById("mixer");
    ASSERT_NE(mixerRegion, nullptr);
    EXPECT_FALSE(mixerRegion->isCurrentlyOpen()) << "dock open, but the Timeline tab is active";

    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_TRUE(mixerRegion->isCurrentlyOpen()) << "dock open AND Mixer tab active";

    ensureDockClosed(mc);
    EXPECT_FALSE(mixerRegion->isCurrentlyOpen())
        << "even with the Mixer tab selected, a closed dock closes the region too -- it has no "
           "closed state of its own to open (no `open` callback, like modMatrix)";
}

TEST(MixerFocusRegionTest, TimelineRegionClosesWhenMixerTabActive) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);

    auto* timelineRegion = mc.getFocusRegionsForTest().findById("timeline");
    ASSERT_NE(timelineRegion, nullptr);
    EXPECT_TRUE(timelineRegion->isCurrentlyOpen());

    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_FALSE(timelineRegion->isCurrentlyOpen())
        << "the dock shows one tab at a time -- \"timeline\"'s own isOpen must now also check "
           "!bottomDock.isMixerTabActive(), not just isBottomDockVisible (plan (a)'s fix)";
}

TEST(MixerFocusRegionTest, TabCycleNeverLandsOnAHiddenDockPanel) {
    // Tab-cycling only ever lands in FocusRegionRegistry::openRegions() -- proving "timeline" and
    // "mixer" are never BOTH open (nor both closed, while the dock itself is open) is exactly what
    // rules out the cycle ever landing on whichever one is hidden behind the other tab.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);

    auto isOpen = [&](const juce::String& id) {
        for (auto* r : mc.getFocusRegionsForTest().openRegions())
            if (r->id == id)
                return true;
        return false;
    };

    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    EXPECT_TRUE(isOpen("timeline"));
    EXPECT_FALSE(isOpen("mixer"));

    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    EXPECT_TRUE(isOpen("mixer"));
    EXPECT_FALSE(isOpen("timeline"));
}

TEST(MixerFocusRegionTest, RegisterMixerFocusRegionIsReusableAcrossIndependentRegistries) {
    // FRO18 plan (a)'s FRO12 seam: registerMixerFocusRegion() must work unmodified against a
    // SECOND, independent FocusRegionRegistry driven by a DIFFERENT dockOpen predicate -- exactly
    // what a future detached mixer window (FRO12) would do, constructing its own registry and
    // calling this same helper with its own open/closed notion instead of MainComponent's
    // isBottomDockVisible-backed one. MainComponent::registerFocusRegions() (registerFocusRegions'
    // "mixer" region -- MainComponentSetup.cpp) exercises the helper with ITS predicate elsewhere;
    // this proves the helper itself, not MainComponent's one call site.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);

    synth::ui::FocusRegionRegistry secondRegistry;
    bool alwaysOpenFlag = true;
    synth::ui::registerMixerFocusRegion(secondRegistry, mc.getBottomDock(),
                                        [&alwaysOpenFlag] { return alwaysOpenFlag; });

    auto* region = secondRegistry.findById("mixer");
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->root, &mc.getBottomDock().getMixerPanel());
    EXPECT_TRUE(region->isCurrentlyOpen()) << "dockOpen() true AND Mixer tab active";

    alwaysOpenFlag = false;
    EXPECT_FALSE(region->isCurrentlyOpen())
        << "the same helper call must keep honouring ITS OWN dockOpen predicate, not some fixed rule";

    // Registering against a second registry must not touch MainComponent's own -- still exactly
    // the eight regions RegistersExactlyTheEightDocumentedRegionsInOrder documents.
    EXPECT_EQ(mc.getFocusRegionsForTest().getRegions().size(), 8u);
}

TEST(MixerFocusRegionTest, RegisterMixerFocusRegionTreatsANullDockOpenAsAlwaysOpen) {
    // FocusRegion::isOpen's own contract: null means "always open" (the graph canvas has no closed
    // state at all). A detached FRO12 window with no closed state of its own passes a null/empty
    // std::function rather than `[]{ return true; }` -- must not crash and must behave identically.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);

    synth::ui::FocusRegionRegistry registry;
    synth::ui::registerMixerFocusRegion(registry, mc.getBottomDock(), nullptr);

    auto* region = registry.findById("mixer");
    ASSERT_NE(region, nullptr);
    EXPECT_TRUE(region->isCurrentlyOpen()) << "null dockOpen -- always open, gated only by the Mixer tab";

    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    EXPECT_FALSE(region->isCurrentlyOpen()) << "still gated by isMixerTabActive() regardless of dockOpen";
}
