#pragma once

// BottomDockActiveTabResetGuard.h -- FRO11 (P9-5), extended FRO255. Shared by
// BottomDockComponentTests.cpp and MixerPanelComponentTests.cpp (header-only; not compiled on its
// own and not registered in Tests/CMakeLists.txt).
//
// Any test that calls BottomDockComponent::setActiveTab(Mixer) on a real, non-isolated
// MainComponent persists "bottomDockActiveTab" to the SAME shared on-disk "Agent Synth" settings
// file every MainComponent test instance reads -- ChannelFlowTestFixture.h's own "settings-file
// hygiene" comment explains why (the delegating MainComponent ctor always wires the real
// appProperties, never a per-test-isolated one). Left uncleared, one test's Mixer-tab switch
// survives into every later test in the same binary run (and a real developer's own settings file
// on this machine) that constructs a fresh MainComponent and assumes the "Timeline" default --
// exactly the flake this guard exists to prevent. RAII, clearing the key on construction AND
// destruction, matching ChannelFlowTest::resetKeys()'s before-AND-after shape.
//
// FRO255: "bottomDockActiveTab" alone was not enough -- two separate, confirmed-by-repro gaps:
//
// 1. ActiveTabPersistsAcrossApplicationPropertiesReload:103 (the ticket's own failure) -- its
//    SECOND MainComponent reads BottomDockComponent::isMixerTabActive() right after construction.
//    setApplicationProperties() restores "bottomDockActiveTab" ("mixer") first, but
//    MainComponent::wireTimelinePanel() then calls mixerPlacement_.applyPlacementPreference()
//    (MainComponentSetupTimeline.cpp), which reads the separate "mixerPlacement" key
//    (MixerPlacementController.cpp). When that key is "window" or "ownPanel",
//    MixerPlacementController::applyPlacement() calls bottomDock.setMixerTabEnabled(false), which
//    unconditionally forces activeTab_ back to Timeline (BottomDockComponent.cpp:112-113) and
//    RE-PERSISTS "bottomDockActiveTab" as "timeline", overwriting the restore this test just made
//    -- so mc2.isMixerTabActive() reads false. Confirmed by direct injection: writing
//    mixerPlacement="window" into the real on-disk settings file and re-running
//    `--gtest_filter='BottomDockComponentTests.ActiveTab*'` ALONE reproduces the exact failure at
//    :103 ("the persisted tab must survive a relaunch"); removing that key makes it pass again.
//    This key is never written by any test in this binary without its own reset guard
//    (MixerPlacementControllerTests.cpp's MixerPlacementResetGuard clears it before/after), so in
//    practice it goes stale from a developer's OWN prior real app usage on this machine (moving
//    the mixer to its own window/panel) -- exactly the un-isolated-shared-file risk
//    ChannelFlowTestFixture.h's "settings-file hygiene" comment warns about. Cleared here the same
//    way MixerPlacementResetGuard already clears it for its own suite.
//
// 2. ToggleMixerCommandOpensDockOnMixerTabThenClosesOnSecondPress:69 (a second, independently
//    reproduced order-sensitivity) -- MainComponent's ctor (MainComponentSetup.cpp's
//    restorePanelPreferences()) seeds isBottomDockVisible from "bottomDockVisible" BEFORE this
//    guard's OWN dock-tab reset ever runs, and the test's own ASSERT_FALSE(dock starts closed)
//    assumes that key's documented default (MainComponentSetup.cpp:
//    getBoolValue("bottomDockVisible", false)). Left dirty ("1") by an earlier
//    dock-opening test in the same binary run (or, again, a developer's real usage), the assert
//    fails before this test even reaches the Mixer-tab assertions the guard was written for.
//    Hard-reset to "0" -- the documented default -- rather than snapshot/restore:
//    TimelinePlayheadTests.cpp / AutomationEditorTests.cpp / TimelineTransportBarTests.cpp /
//    ChannelFlowTestFixture.h's own resetKeys() all reset this same key to this same fixed "0"
//    before/after their own MainComponent-constructing tests, so a fixed reset here matches the
//    established convention rather than inventing a new one.
#include "../../TestSettingsHelpers.h"
#include "MainComponent/MainComponent.h"

struct BottomDockActiveTabResetGuardMDT {
    BottomDockActiveTabResetGuardMDT() { resetKey(); }
    ~BottomDockActiveTabResetGuardMDT() { resetKey(); }

    static void resetKey() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        if (auto* s = props.getUserSettings()) {
            s->removeValue("bottomDockActiveTab");
            // FRO255: see the class comment -- both gaps confirmed by direct repro.
            s->removeValue("mixerPlacement");
            s->setValue("bottomDockVisible", "0");
            s->saveIfNeeded();
        }
    }
};
