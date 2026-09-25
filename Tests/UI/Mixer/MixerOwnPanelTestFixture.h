#pragma once

// MixerOwnPanelTestFixture.h -- FRO231. Shared by MixerOwnPanelResizeTests.cpp and
// MixerOwnPanelSlideTests.cpp (header-only; not compiled on its own, not in Tests/CMakeLists.txt).
// A real off-screen MainComponent in "Own panel" placement, with the on-disk settings keys the
// placement, the dock and the Own panel's height persist under reset before AND after each test.

#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestFixture.h"
#include "UI/Mixer/MixerPlacementController.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

class MixerOwnPanelTest : public TimelinePanelIntegrationTest {
protected:
    using Controller = synth::ui::MixerPlacementController;

    void SetUp() override {
        TimelinePanelIntegrationTest::SetUp();
        resetOwnKeys();
    }
    void TearDown() override {
        resetOwnKeys();
        TimelinePanelIntegrationTest::TearDown();
    }

    static void writeSetting(const juce::String& key, const juce::var& value) {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            s->setValue(key, value);
            s->saveIfNeeded();
        }
    }

    static int readPersistedOwnHeight(MainComponent& mc) {
        return mc.getAppPropertiesForTest().getUserSettings()->getIntValue(Controller::kOwnPanelHeightKey, -1);
    }

    // A window with the Own panel placement already persisted, so it is open at launch.
    static void showOwnPanel(MainComponent& mc, int width = 1600, int height = 900) {
        mc.setSize(width, height);
        ASSERT_TRUE(mc.getMixerPlacementControllerForTest().isOwnPanelShowing());
    }

    static void useOwnPanelPlacement() { writeSetting("mixerPlacement", "ownPanel"); }

private:
    BottomDockActiveTabResetGuardMDT tabGuard_; // clears mixerPlacement / bottomDockActiveTab / dock visibility

    static void resetOwnKeys() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            s->removeValue(Controller::kOwnPanelHeightKey);
            s->saveIfNeeded();
        }
    }
};
