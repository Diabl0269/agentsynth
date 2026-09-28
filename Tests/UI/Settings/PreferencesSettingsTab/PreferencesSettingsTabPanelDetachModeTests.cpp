// PreferencesSettingsTabPanelDetachModeTests.cpp -- FRO336 (docs/mixer/panel.md): "When a panel
// opens in its own window" combo (Move it there / Show it in both places) -- default value,
// getter/setter round trip, and persistence across a fresh PreferencesSettingsTab reading the same
// ApplicationProperties file, same shape as PreferencesSettingsTabMixerPlacementTests.cpp (the
// group it sits directly beneath).
#include "PreferencesSettingsTabTestFixture.h"

TEST_F(PreferencesSettingsTabTest, DefaultsToMovePlacesMode) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getPanelDetachMode(), "move");
}

TEST_F(PreferencesSettingsTabTest, SetPanelDetachModeRoundTrips) {
    PreferencesSettingsTab tab(appProperties);

    tab.setPanelDetachMode("both");
    EXPECT_EQ(tab.getPanelDetachMode(), "both");

    tab.setPanelDetachMode("move");
    EXPECT_EQ(tab.getPanelDetachMode(), "move");
}

TEST_F(PreferencesSettingsTabTest, PanelDetachModePersistsAcrossApplicationPropertiesReload) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setPanelDetachMode("both");
    }
    // A fresh tab reading the SAME ApplicationProperties must restore the persisted choice -- same
    // contract as every other preference here (kPanelDetachModeKey = "detachedPanelBothPlaces").
    PreferencesSettingsTab tab2(appProperties);
    EXPECT_EQ(tab2.getPanelDetachMode(), "both");
}

TEST_F(PreferencesSettingsTabTest, UnknownPersistedPanelDetachModeFallsBackToMove) {
    appProperties.getUserSettings()->setValue("detachedPanelBothPlaces", "notARealValue");
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getPanelDetachMode(), "move");
}
