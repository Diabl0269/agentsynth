// PreferencesSettingsTabInfoTooltipsTests.cpp (docs/layout/animation.md#tooltips): the "Show info tooltips" row --
// default on, getter/setter round trip, persistence across a fresh tab on the same ApplicationProperties, and the live
// read by a tooltip window that never hears from the tab directly.
#include "PreferencesSettingsTabTestFixture.h"
#include "UI/Layout/AppTooltipWindow.h"

TEST_F(PreferencesSettingsTabTest, ShowInfoTooltipsDefaultsToOn) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isShowInfoTooltipsEnabled());
}

TEST_F(PreferencesSettingsTabTest, ShowInfoTooltipsRoundTripsAndPersists) {
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setShowInfoTooltipsEnabled(false);
        EXPECT_FALSE(tab.isShowInfoTooltipsEnabled());
    }
    PreferencesSettingsTab tab2(appProperties);
    EXPECT_FALSE(tab2.isShowInfoTooltipsEnabled());
    tab2.setShowInfoTooltipsEnabled(true);
    EXPECT_TRUE(appProperties.getUserSettings()->getBoolValue(synth::ui::kShowInfoTooltipsKey, false));
}

TEST_F(PreferencesSettingsTabTest, ClickingShowInfoTooltipsTurnsInfoTooltipsOffInAWindowAtOnce) {
    juce::Component parent;
    synth::ui::AppTooltipWindow window(&parent, &appProperties);
    juce::TextButton ordinary("ordinary");
    ordinary.setTooltip("Explains the control");
    PreferencesSettingsTab tab(appProperties);
    ASSERT_FALSE(window.suppresses(ordinary));

    auto* toggle = findToggleByText(tab, "Show info tooltips");
    ASSERT_NE(toggle, nullptr);
    toggle->setToggleState(false, juce::sendNotification); // what a click does
    EXPECT_TRUE(window.suppresses(ordinary)) << "no restart, no push: the window reads the saved setting";
    EXPECT_TRUE(synth::ui::isHelperTooltip(*toggle)) << "its own tooltip must still say how to bring tips back";
}
