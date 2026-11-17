// PreferencesSettingsTabPluginSleepTests.cpp: the "Unload plugins while the Mac sleeps" row -- default on, persisted
// under the key the sleep guard reads, and sitting under the Plugins category.
#include "Plugin/Hosting/HostedPluginSleepGuard.h"
#include "PreferencesSettingsTabTestFixture.h"

TEST_F(PreferencesSettingsTabTest, UnloadPluginsDuringSleepDefaultsToOn) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_TRUE(tab.isUnloadPluginsDuringSleepEnabled());
}

TEST_F(PreferencesSettingsTabTest, UnloadPluginsDuringSleepClickPersistsTheGuardsKey) {
    {
        PreferencesSettingsTab tab(appProperties);
        auto* toggle = findToggleByText(tab, "Unload plugins while the Mac sleeps");
        ASSERT_NE(toggle, nullptr);
        EXPECT_FALSE(toggle->getTooltip().isEmpty());
        EXPECT_EQ(toggle->getTitle(), "Unload plugins while the Mac sleeps");
        toggle->setToggleState(false, juce::sendNotification); // what a click does
    }
    EXPECT_FALSE(appProperties.getUserSettings()->getBoolValue(synth::kUnloadPluginsDuringSleepKey, true));
    PreferencesSettingsTab reopened(appProperties);
    EXPECT_FALSE(reopened.isUnloadPluginsDuringSleepEnabled());
    reopened.setUnloadPluginsDuringSleepEnabled(true);
    EXPECT_TRUE(appProperties.getUserSettings()->getBoolValue(synth::kUnloadPluginsDuringSleepKey, false));
}

TEST_F(PreferencesSettingsTabTest, UnloadPluginsDuringSleepSitsUnderThePluginsCategory) {
    using Category = PreferencesSettingsTab::Category;
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 900);
    tab.setSelectedCategory(Category::Plugins);
    auto* toggle = findToggleByText(tab, "Unload plugins while the Mac sleeps");
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_EQ(PreferencesSettingsTab::categoryName(Category::Plugins), "Plugins");
    tab.setSelectedCategory(Category::Privacy);
    EXPECT_FALSE(toggle->isVisible());
}
