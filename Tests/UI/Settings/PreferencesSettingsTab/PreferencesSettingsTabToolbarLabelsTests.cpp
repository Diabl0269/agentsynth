// PreferencesSettingsTabToolbarLabelsTests.cpp (docs/layout/settings-preferences.md): the "Show button labels" row --
// default on, getter/setter round trip, persistence across a fresh tab, and the click path a person takes.
#include "PreferencesSettingsTabTestFixture.h"
#include "UI/Chrome/CanvasToolbar/ToolbarLabelsPreference.h"

TEST_F(PreferencesSettingsTabTest, ShowButtonLabelsDefaultsToOn) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isShowToolbarLabelsEnabled());
    EXPECT_TRUE(synth::ui::showToolbarLabels(appProperties.getUserSettings()));
}

TEST_F(PreferencesSettingsTabTest, ShowButtonLabelsRoundTripsAndPersists) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setShowToolbarLabelsEnabled(false);
        EXPECT_FALSE(tab.isShowToolbarLabelsEnabled());
    }
    EXPECT_FALSE(synth::ui::showToolbarLabels(appProperties.getUserSettings()));
    PreferencesSettingsTab tab2(appProperties);
    EXPECT_FALSE(tab2.isShowToolbarLabelsEnabled());
}

TEST_F(PreferencesSettingsTabTest, ClickingShowButtonLabelsWritesTheSetting) {
    PreferencesSettingsTab tab(appProperties);
    auto* toggle = findToggleByText(tab, "Show button labels");
    ASSERT_NE(toggle, nullptr);
    EXPECT_EQ(toggle->getTooltip(), "Show text under the top bar and canvas toolbar buttons");
    toggle->setToggleState(false, juce::sendNotification); // what a click does
    EXPECT_FALSE(appProperties.getUserSettings()->getBoolValue(synth::ui::kShowToolbarLabelsKey, true));
}
