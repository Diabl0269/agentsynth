// PreferencesSettingsTabMixerPlacementTests.cpp -- FRO12 (P9-6, docs/mixer/panel.md): the Mixer
// placement combo (Tab beside the Timeline / Own panel / Window) -- default value, getter/setter
// round trip, and persistence across a fresh PreferencesSettingsTab reading the same
// ApplicationProperties file (same shape as the per-type default track preset combos it sits next
// to -- see PreferencesSettingsTabMixerDefaults.cpp).
#include "PreferencesSettingsTabTestFixture.h"

TEST_F(PreferencesSettingsTabTest, DefaultsToTabPlacement) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getMixerPlacement(), "tab");
}

TEST_F(PreferencesSettingsTabTest, SetMixerPlacementRoundTrips) {
    PreferencesSettingsTab tab(appProperties);

    tab.setMixerPlacement("ownPanel");
    EXPECT_EQ(tab.getMixerPlacement(), "ownPanel");

    tab.setMixerPlacement("window");
    EXPECT_EQ(tab.getMixerPlacement(), "window");

    tab.setMixerPlacement("tab");
    EXPECT_EQ(tab.getMixerPlacement(), "tab");
}

TEST_F(PreferencesSettingsTabTest, MixerPlacementPersistsAcrossApplicationPropertiesReload) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setMixerPlacement("window");
    }
    // A fresh tab reading the SAME ApplicationProperties must restore the persisted choice --
    // same contract as every other preference here (kMixerPlacementKey = "mixerPlacement").
    PreferencesSettingsTab tab2(appProperties);
    EXPECT_EQ(tab2.getMixerPlacement(), "window");
}

TEST_F(PreferencesSettingsTabTest, UnknownPersistedValueFallsBackToTab) {
    appProperties.getUserSettings()->setValue("mixerPlacement", "notARealValue");
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getMixerPlacement(), "tab");
}
