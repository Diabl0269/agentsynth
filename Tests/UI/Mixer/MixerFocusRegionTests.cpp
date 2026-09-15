// MixerFocusRegionTests.cpp -- FRO18: the "mixer" focus region's open predicate, and the fix to
// "timeline"'s own (MainComponent::registerFocusRegions, plan (a)) now that both share one dock
// and only one tab is ever on screen at a time. Isolates BOTH persisted dock keys the shared
// on-disk settings file carries ("bottomDockActiveTab" via the existing guard, "timelinePanelVisible"
// by reading the current state and only toggling when it disagrees with what each test needs) --
// see MixerDockActiveTabResetGuard.h's own comment for why an unguarded persisted key leaks
// between test instances in the same binary run.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "MixerDockActiveTabResetGuard.h"
#include "UI/Layout/FocusRegion.h"
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
    if (!mc.isTimelineConfiguredVisible())
        mc.simulateToggleTimelineClick();
}

void ensureDockClosed(MainComponent& mc) {
    if (mc.isTimelineConfiguredVisible())
        mc.simulateToggleTimelineClick();
}

} // namespace

TEST(MixerFocusRegionTest, MixerRegionOpenOnlyWhenDockOpenAndMixerTabActive) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);
    mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);

    auto* mixerRegion = mc.getFocusRegionsForTest().findById("mixer");
    ASSERT_NE(mixerRegion, nullptr);
    EXPECT_FALSE(mixerRegion->isCurrentlyOpen()) << "dock open, but the Timeline tab is active";

    mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    EXPECT_TRUE(mixerRegion->isCurrentlyOpen()) << "dock open AND Mixer tab active";

    ensureDockClosed(mc);
    EXPECT_FALSE(mixerRegion->isCurrentlyOpen())
        << "even with the Mixer tab selected, a closed dock closes the region too -- it has no "
           "closed state of its own to open (no `open` callback, like modMatrix)";
}

TEST(MixerFocusRegionTest, TimelineRegionClosesWhenMixerTabActive) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMFRT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    ensureDockOpen(mc);
    mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);

    auto* timelineRegion = mc.getFocusRegionsForTest().findById("timeline");
    ASSERT_NE(timelineRegion, nullptr);
    EXPECT_TRUE(timelineRegion->isCurrentlyOpen());

    mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    EXPECT_FALSE(timelineRegion->isCurrentlyOpen())
        << "the dock shows one tab at a time -- \"timeline\"'s own isOpen must now also check "
           "!mixerDock.isMixerTabActive(), not just isTimelineVisible (plan (a)'s fix)";
}

TEST(MixerFocusRegionTest, TabCycleNeverLandsOnAHiddenDockPanel) {
    // Tab-cycling only ever lands in FocusRegionRegistry::openRegions() -- proving "timeline" and
    // "mixer" are never BOTH open (nor both closed, while the dock itself is open) is exactly what
    // rules out the cycle ever landing on whichever one is hidden behind the other tab.
    MixerDockActiveTabResetGuardMDT resetGuard;
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

    mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
    EXPECT_TRUE(isOpen("timeline"));
    EXPECT_FALSE(isOpen("mixer"));

    mc.getMixerDock().setActiveTab(synth::ui::MixerDockComponent::Tab::Mixer);
    EXPECT_TRUE(isOpen("mixer"));
    EXPECT_FALSE(isOpen("timeline"));
}
